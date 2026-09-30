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
  opaque    64 KB    one bit a tile: set where the tile has no transparent pixel (32 KB used)
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

# The sets this knows, from MAME's cps1.cpp. Every one is a 90629B board with the STF29
# graphics PAL, so the layout is the same; what differs is the program, the CPS-B chip the
# program expects, and what the labels say. The graphics come in two labellings that hold
# the same data.
GFX_W = ['sf2-5m.4a', 'sf2-7m.6a', 'sf2-1m.3a', 'sf2-3m.5a',
         'sf2-6m.4c', 'sf2-8m.6c', 'sf2-2m.3c', 'sf2-4m.5c',
         'sf2-13m.4d', 'sf2-15m.6d', 'sf2-9m.3d', 'sf2-11m.5d']
GFX_J = ['sf2_06.8a', 'sf2_08.10a', 'sf2_05.7a', 'sf2_07.9a',
         'sf2_15.8c', 'sf2_17.10c', 'sf2_14.7c', 'sf2_16.9c',
         'sf2_25.8d', 'sf2_27.10d', 'sf2_24.7d', 'sf2_26.9d']
OKI_W = ['sf2_18.11c', 'sf2_19.12c']
OKI_J = ['sf2j_18.11c', 'sf2j_19.12c']

# CPS-B chips: id register, id value, layer control, priority masks, palette control,
# layer enable bits (scroll1, scroll2, scroll3), and where the kick buttons are read
CPSB = {
    '05': (0x20, 0x0005, 0x28, (0x2a, 0x2c, 0x2e, 0x30), 0x32, (0x02, 0x08, 0x20), 0x36),
    '11': (0x32, 0x0401, 0x26, (0x28, 0x2a, 0x2c, 0x2e), 0x30, (0x08, 0x10, 0x20), 0x36),
    '12': (0x20, 0x0402, 0x2c, (0x2a, 0x28, 0x26, 0x24), 0x22, (0x02, 0x04, 0x08), 0x36),
    '13': (0x2e, 0x0403, 0x22, (0x24, 0x26, 0x28, 0x2a), 0x2c, (0x20, 0x02, 0x04), 0x36),
    '14': (0x1e, 0x0404, 0x12, (0x14, 0x16, 0x18, 0x1a), 0x1c, (0x08, 0x20, 0x10), 0x36),
    '15': (0x0e, 0x0405, 0x02, (0x04, 0x06, 0x08, 0x0a), 0x0c, (0x04, 0x02, 0x20), 0x36),
    '17': (0x08, 0x0407, 0x14, (0x12, 0x10, 0x0e, 0x0c), 0x0a, (0x08, 0x14, 0x02), 0x36),
    '18': (0x10, 0x0408, 0x1c, (0x1a, 0x18, 0x16, 0x14), 0x12, (0x10, 0x08, 0x02), 0x3c),
}

def prog(stem, rev, tail):
    """the eight program ROMs in board order 30 37 31 38 28 35 29 36"""
    return [f'{stem}_30{rev}.11e', f'{stem}_37{rev}.11f', f'{stem}_31{rev}.12e', f'{stem}_38{rev}.12f',
            f'{stem}_28{rev}.9e', f'{stem}_35{rev}.9f'] + tail

T_29B = ['sf2_29b.10e', 'sf2_36b.10f']
T_29A_J = ['sf2j_29a.10e', 'sf2j_36a.10f']

# set name: (CPS-B, program ROMs, graphics ROMs, sound program, sample ROMs)
SETS = {
    'sf2':   ('11', prog('sf2e', 'g', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ea': ('17', ['sf2e_30a.11e', 'sf2e_37a.11f', 'sf2e_31a.12e', 'sf2e_38a.12f', 'sf2_28a.9e', 'sf2_35a.9f', 'sf2_29.10e', 'sf2_36.10f'], GFX_W, 'sf2_9.12a', OKI_W),
    'sf2eb': ('17', ['sf2e_30b.11e', 'sf2e_37b.11f', 'sf2e_31b.12e', 'sf2e_38b.12f', 'sf2_28b.9e', 'sf2_35b.9f'] + T_29B, GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ua': ('17', prog('sf2u', 'a', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ub': ('17', prog('sf2u', 'b', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2uc': ('12', prog('sf2u', 'c', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ud': ('05', prog('sf2u', 'd', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ue': ('18', prog('sf2u', 'e', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2uf': ('15', prog('sf2u', 'f', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ug': ('11', prog('sf2u', 'g', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2uh': ('13', prog('sf2u', 'h', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2ui': ('14', prog('sf2u', 'i', T_29B), GFX_W, 'sf2_9.12a', OKI_W),
    'sf2uk': ('17', prog('sf2u', 'k', ['sf2u_29a.10e', 'sf2u_36a.10f']), GFX_W, 'sf2_09.12a', OKI_W),
    'sf2um': ('17', prog('sf-2u', 'm', ['sf-2u_29m.10e', 'sf-2u_36m.10f']), GFX_W, 'sf2_09.12a', OKI_W),
    'sf2ja': ('17', prog('sf2j', 'a', T_29A_J), GFX_J, 'sf2j_09.12a', OKI_J),
    'sf2jc': ('12', prog('sf2j', 'c', T_29A_J), GFX_J, 'sf2_09.12a', OKI_J),
    'sf2jf': ('15', prog('sf2j', 'f', T_29A_J), GFX_J, 'sf2_09.12a', OKI_W),
    'sf2jh': ('13', prog('sf2j', 'h', T_29A_J), GFX_J, 'sf2_09.12a', OKI_W),
    'sf2jl': ('17', prog('sf-2', 'l', T_29A_J), GFX_J, 'sf2j_09.12a', OKI_J),
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
    for name, (cpsb, progs, gfx, z80, oki) in SETS.items():
        if all(p in have for p in progs):
            missing = [n for n in gfx + [z80] + oki if n not in have]
            if missing:
                sys.exit(f'{name}: the program ROMs are here but not {", ".join(missing)}')
            return name, cpsb, progs, gfx, z80, oki
    progs = sorted(n for n in have if n.startswith('sf') and ('_30' in n or '_37' in n))
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

def opaque_bits(gfx):
    """One bit per tile, set if none of its pixels is pen 15 (transparent). Three maps: 8x8
    tiles as the pairs the hardware keeps them in (two bits a pair, left half then right),
    16x16 tiles, 32x32 tiles."""
    x = int.from_bytes(gfx, 'little')
    f = x & (x >> 1) & (x >> 2) & (x >> 3) & int.from_bytes(b'\x11' * len(gfx), 'little')
    flags = f.to_bytes(len(gfx), 'little')        # a set nibble low bit where a pixel is 15
    def bits(pred, count):
        out = bytearray((count + 7) // 8)
        for i in range(count):
            if pred(i):
                out[i >> 3] |= 1 << (i & 7)
        return bytes(out)
    zero64, zero128, zero512 = bytes(32), bytes(128), bytes(512)
    def half8(i):
        t, side = i >> 1, (i & 1) * 4
        base = t * 64
        return all(flags[base + r * 8 + side:base + r * 8 + side + 4] == zero64[:4] for r in range(8))
    m8 = bits(half8, len(gfx) // 32)
    m16 = bits(lambda i: flags[i * 128:i * 128 + 128] == zero128, len(gfx) // 128)
    m32 = bits(lambda i: flags[i * 512:i * 512 + 512] == zero512, len(gfx) // 512)
    return m8, m16, m32

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = Source(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / 'roms.bin'
    name, cpsb, prog_names, gfx_names, z80_name, oki_names = which_set(src)
    print(f'set: {name} (CPS-B-{cpsb})')

    prog = bytearray(0x100000)
    for i in range(4):
        even = src.read(prog_names[2 * i], 0x20000)      # the high byte of each word
        odd = src.read(prog_names[2 * i + 1], 0x20000)
        base = i * 0x40000
        prog[base:base + 0x40000:2] = odd                # little-endian: low byte first
        prog[base + 1:base + 0x40000:2] = even

    planar = bytearray(0x600000)
    for i, n in enumerate(gfx_names):
        d = src.read(n, 0x80000)
        base = (i // 4) * 0x200000
        lane = (i % 4) * 2
        planar[base + lane:base + 0x200000:8] = d[0::2]
        planar[base + lane + 1:base + 0x200000:8] = d[1::2]
    print('  converting graphics to 4 bits a pixel...')
    gfx = chunky(planar)

    print('  finding the tiles with no transparent pixel...')
    m8, m16, m32 = opaque_bits(gfx)
    opaque = m8 + m16 + m32

    z80 = src.read(z80_name, 0x10000)
    oki = b''.join(src.read(n, 0x20000) for n in oki_names)

    c = CPSB[cpsb]
    off = 0
    sections = []
    for data in (prog, gfx, z80, opaque, oki):
        sections.append((off, len(data)))
        off += len(data)
    opaque_off = sections[3][0]
    sections[3:4] = []
    oki_pad = 64 * K - len(opaque)
    sections[3] = (sections[3][0] + oki_pad, sections[3][1])
    header = struct.pack('<II8I4BH4B3B3x16sI', 0x42324653, 3,
                         *[v for s in sections for v in s],
                         c[0], c[2], c[4], c[6], c[1], *c[3], *c[5],
                         name.encode(), opaque_off)
    blob = bytes(prog) + gfx + z80 + opaque.ljust(64 * K, b'\xff') + oki + header.ljust(4 * K, b'\xff')
    out.write_bytes(blob)
    print(f'wrote {out}: {len(blob)} bytes ({len(blob) / K:.0f} KB); the data partition needs '
          f'data_kb >= {-(-len(blob) // (64 * K)) * 64}')

if __name__ == '__main__':
    main()
