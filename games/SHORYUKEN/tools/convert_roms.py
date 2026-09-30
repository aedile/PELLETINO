#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Turn a MAME Street Fighter II: The World Warrior ROM set into roms.bin for SHORYUKEN.

usage: convert_roms.py <romdir or sf2.zip> [roms.bin]     (default: ../roms.bin)

roms.bin is flashed into a data partition and read in place, so it is laid out the way
the emulator reads it, not the way the chips hold it:

  program   1 MB     the eight 68000 ROMs interleaved, each 16-bit word stored little-endian
                     so the RISC-V fetches an opcode with one aligned load
  graphics  6 MB     the twelve mask ROMs interleaved as the board addresses them, and
                     converted from four bit planes to four bits a pixel: eight pixels are
                     one little-endian 32-bit word, leftmost pixel in the low nibble
  z80       64 KB    as the chip holds it: 32 KB fixed, then two 16 KB banks
  oki       256 KB   the two sample ROMs, end to end
  header    4 KB     cps1_blob_t (core/cps1.h): where everything is, and which CPS-B chip

The order matters. The ESP32-C6 can have 8 MB of flash mapped at a time, in 64 KB pages,
and the application's own code is part of that. So the header is last, where it costs no
page (it is read, not mapped), and the samples are next to last, so that the mapping can
stop short of them too: they are read a few hundred bytes at a time as they are played.
That leaves the application fifteen pages instead of ten.

Why the graphics are laid out this way. Drawing a tile reads a few consecutive rows of it,
and the flash cache works in 32-byte lines. On the board a row of a tile is already next to
the row below it (8 bytes a row for a 16x16 tile, 16 for a 32x32), and the converter keeps
that: the 128 bytes of a 16x16 tile are four cache lines, and drawing the tile top to bottom
walks them in order. What it changes is inside the row. The chips keep the four planes of
eight pixels in four separate bytes, which costs four shifts and four masks a pixel to
undo; here a pixel is one shift and one mask, and an all-transparent group of eight is one
compare with 0xffffffff.

Nothing this writes may be committed: roms.bin is the ROMs.
"""
import struct, sys, zipfile, zlib
from pathlib import Path

K = 1024

# The sets this knows. Every one is a 90629B board with the STF29 graphics PAL, so the
# layout is the same; what differs is the program and the CPS-B chip the program expects.
#   (30, 37, 31, 38, 28, 35, 29, 36) are the program ROM positions, even byte first.
GFX = ['sf2-5m.4a', 'sf2-7m.6a', 'sf2-1m.3a', 'sf2-3m.5a',
       'sf2-6m.4c', 'sf2-8m.6c', 'sf2-2m.3c', 'sf2-4m.5c',
       'sf2-13m.4d', 'sf2-15m.6d', 'sf2-9m.3d', 'sf2-11m.5d']
Z80 = 'sf2_9.12a'
OKI = ['sf2_18.11c', 'sf2_19.12c']

# CPS-B chips: id register, id value, layer control, priority masks, palette control,
# layer enable bits (scroll1, scroll2, scroll3)
CPSB = {
    '05': (0x20, 0x0005, 0x28, (0x2a, 0x2c, 0x2e, 0x30), 0x32, (0x02, 0x08, 0x20)),
    '11': (0x32, 0x0401, 0x26, (0x28, 0x2a, 0x2c, 0x2e), 0x30, (0x08, 0x10, 0x20)),
    '12': (0x20, 0x0402, 0x2c, (0x2a, 0x28, 0x26, 0x24), 0x22, (0x02, 0x04, 0x08)),
    '13': (0x2e, 0x0403, 0x22, (0x24, 0x26, 0x28, 0x2a), 0x2c, (0x20, 0x02, 0x04)),
    '14': (0x1e, 0x0404, 0x12, (0x14, 0x16, 0x18, 0x1a), 0x1c, (0x08, 0x20, 0x10)),
    '15': (0x0e, 0x0405, 0x02, (0x04, 0x06, 0x08, 0x0a), 0x0c, (0x04, 0x02, 0x20)),
    '17': (0x08, 0x0407, 0x14, (0x12, 0x10, 0x0e, 0x0c), 0x0a, (0x08, 0x14, 0x02)),
}
IN2_REG = 0x36     # the C632 PAL puts the three kick buttons here on all of them

# set name: (CPS-B, the eight program ROMs in board order 30 37 31 38 28 35 29 36)
SETS = {
    'sf2':   ('11', ['sf2e_30g.11e', 'sf2e_37g.11f', 'sf2e_31g.12e', 'sf2e_38g.12f',
                     'sf2e_28g.9e', 'sf2e_35g.9f', 'sf2_29b.10e', 'sf2_36b.10f']),
    'sf2ua': ('17', ['sf2u_30a.11e', 'sf2u_37a.11f', 'sf2u_31a.12e', 'sf2u_38a.12f',
                     'sf2u_28a.9e', 'sf2u_35a.9f', 'sf2_29b.10e', 'sf2_36b.10f']),
    'sf2ub': ('17', ['sf2u_30b.11e', 'sf2u_37b.11f', 'sf2u_31b.12e', 'sf2u_38b.12f',
                     'sf2u_28b.9e', 'sf2u_35b.9f', 'sf2_29b.10e', 'sf2_36b.10f']),
    'sf2ja': ('17', ['sf2j_30a.11e', 'sf2j_37a.11f', 'sf2j_31a.12e', 'sf2j_38a.12f',
                     'sf2j_28a.9e', 'sf2j_35a.9f', 'sf2_29b.10e', 'sf2_36b.10f']),
}

class Source:
    def __init__(self, path):
        self.path = Path(path)
        self.zip = zipfile.ZipFile(self.path) if self.path.is_file() else None
        if not self.zip and not self.path.is_dir():
            sys.exit(f'{path}: not a folder and not a zip')
    def names(self):
        if self.zip:
            return {Path(n).name for n in self.zip.namelist()}
        return {p.name for p in self.path.iterdir()}
    def read(self, name, size):
        if self.zip:
            hit = [n for n in self.zip.namelist() if Path(n).name == name]
            data = self.zip.read(hit[0]) if hit else None
        else:
            p = self.path / name
            data = p.read_bytes() if p.exists() else None
        if data is None:
            sys.exit(f'{name}: not found in {self.path}')
        if len(data) != size:
            sys.exit(f'{name}: {len(data)} bytes, expected {size}')
        print(f'  {name:<14} {len(data):>7} bytes  crc32 {zlib.crc32(data) & 0xffffffff:08x}')
        return data

def which_set(src):
    have = src.names()
    for name, (cpsb, prog) in SETS.items():
        if all(p in have for p in prog):
            return name, cpsb, prog
    progs = sorted(n for n in have if n.startswith('sf2') and ('_30' in n or '_37' in n))
    sys.exit('This is not a Street Fighter II: The World Warrior set this converter knows.\n'
             f'  program ROMs found: {", ".join(progs) if progs else "none"}\n'
             f'  sets understood:    {", ".join(SETS)}\n'
             'Champion Edition, Hyper Fighting and the bootlegs are different boards with a\n'
             'different graphics layout, and are refused rather than guessed at.')

def chunky(planar):
    """4 planes x 8 pixels in 4 bytes -> 8 pixels x 4 bits in a little-endian word."""
    spread = []
    for b in range(256):
        v = 0
        for x in range(8):
            if b & (0x80 >> x):
                v |= 1 << (4 * x)
        spread.append(v)
    out = bytearray(len(planar))
    s = spread
    pk = struct.pack_into
    for i in range(0, len(planar), 4):
        # byte 0 is plane 3 (the pixel's low bit) ... byte 3 is plane 0 (its high bit)
        pk('<I', out, i, s[planar[i]] | (s[planar[i + 1]] << 1) | (s[planar[i + 2]] << 2) | (s[planar[i + 3]] << 3))
    return bytes(out)

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = Source(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / 'roms.bin'
    name, cpsb, prog_names = which_set(src)
    print(f'set: {name} (CPS-B-{cpsb})')

    prog = bytearray(0x100000)
    for i in range(4):
        even = src.read(prog_names[2 * i], 0x20000)      # the high byte of each word
        odd = src.read(prog_names[2 * i + 1], 0x20000)
        base = i * 0x40000
        prog[base:base + 0x40000:2] = odd                # little-endian: low byte first
        prog[base + 1:base + 0x40000:2] = even

    planar = bytearray(0x600000)
    for i, n in enumerate(GFX):
        d = src.read(n, 0x80000)
        base = (i // 4) * 0x200000
        lane = (i % 4) * 2
        planar[base + lane:base + 0x200000:8] = d[0::2]
        planar[base + lane + 1:base + 0x200000:8] = d[1::2]
    print('  converting graphics to 4 bits a pixel...')
    gfx = chunky(planar)

    z80 = src.read(Z80, 0x10000)
    oki = b''.join(src.read(n, 0x20000) for n in OKI)

    c = CPSB[cpsb]
    off = 0
    sections = []
    for data in (prog, gfx, z80, oki):
        sections.append((off, len(data)))
        off += len(data)
    header = struct.pack('<II8I4BH4B3B3x16s', 0x42324653, 2,
                         *[v for s in sections for v in s],
                         c[0], c[2], c[4], IN2_REG, c[1], *c[3], *c[5],
                         name.encode())
    blob = bytes(prog) + gfx + z80 + oki + header.ljust(4 * K, b'\xff')
    out.write_bytes(blob)
    print(f'wrote {out}: {len(blob)} bytes ({len(blob) / K:.0f} KB); the data partition needs '
          f'data_kb >= {-(-len(blob) // (64 * K)) * 64}')

if __name__ == '__main__':
    main()
