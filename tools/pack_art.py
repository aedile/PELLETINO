#!/usr/bin/env python3
"""
Fit the wheel's artwork to the panel and pack it into the mqart blob.

  python3 tools/pack_art.py            # art/logo, art/snap -> lcd/marquees.bin

Everything is stored the way the launcher draws it: one byte per pixel, an
index into its 6x6x5 colour cube (components/fest), dithered here so the
firmware never has to. Index 255 is transparent.

Per game:
  logo   at three sizes, one for each place a logo rests on the wheel, scaled
         here with a proper filter and dimmed the further it sits from the
         middle. The firmware only scales while the wheel is turning.
  snap   a screenshot filling the panel, already dimmed, in SNAP_COLOURS colours
         of its own rather than the cube's: a dim picture lands on the cube's
         bottom two levels and dithers to noise, which does not compress.
         Its pixels are indices 199.. into the launcher's palette.
Either may be missing (w = 0): the launcher writes the title in text and draws
its own backdrop.

Blob layout, little-endian:
    "MQ04" | u16 count | u16 entry_size | entry[count] | images
    entry  = 12s rom | 12s boot | 24s title | 24s caption | image[4]
    image  = u16 w | u16 h | u32 offset | u32 length        (3 logos, then the snap)
A snap begins with its colours, SNAP_COLOURS x (r, g, b). Then, for any
image, u32 row_offset[h] (from the start of the image) followed by the
rows, each run-length coded:
    control < 128    copy the next control+1 bytes
    control >= 128   repeat the next byte control-126 times

The boot label is the partition the launcher chain-boots for this entry; it equals
the ROM name except for a shared slot (Pac-Man rides Ms. Pac-Man's image), where it
is the slot owner's label, and for something built into the launcher, where it
starts with '@'.
"""
import os, struct, sys
from PIL import Image, ImageEnhance
import configure                      # single source of truth for what is in the build

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC   = os.path.join(ROOT, 'art')
PREV  = os.path.join(ROOT, 'lcd', 'preview')
OUT   = os.path.join(ROOT, 'lcd', 'marquees.bin')
W, H  = 240, 280
LOGO_BOXES = [(216, 96), (140, 58), (96, 40)]      # selected, its neighbours, the far pair
LOGO_DIM   = [1.0, 0.55, 0.30]
SNAP_DIM   = 0.30
SNAP_COLOURS = 56                                  # palette entries 199..254
SNAP_BASE  = 199
CLEAR      = 255
ENTRY = struct.Struct('<12s12s24s24s' + 'HHII' * 4)

def cube_palette():
    pal = []
    for i in range(180): pal += [(i // 30) * 51, ((i // 5) % 6) * 51, (i % 5) * 63]
    p = Image.new('P', (1, 1)); p.putpalette(pal + [0, 0, 0] * 76); return p
CUBE = cube_palette()

def to_cube(rgb):
    return rgb.quantize(palette=CUBE, dither=Image.Dither.FLOYDSTEINBERG)

def rle_row(row):
    out, i, n = bytearray(), 0, len(row)
    while i < n:
        run = 1
        while i + run < n and run < 129 and row[i + run] == row[i]: run += 1
        if run >= 2:
            out += bytes((run + 126, row[i])); i += run
            continue
        j = i + 1                                   # literals, up to the next run
        while j < n and j - i < 128 and not (j + 1 < n and row[j] == row[j + 1]): j += 1
        out += bytes((j - i - 1,)) + row[i:j]; i = j
    return bytes(out)

def unrle_row(data, w):                             # the firmware's decoder, for the self-check
    out, i = bytearray(), 0
    while len(out) < w:
        c = data[i]; i += 1
        if c < 128: out += data[i:i + c + 1]; i += c + 1
        else:       out += bytes((data[i],)) * (c - 126); i += 1
    return bytes(out)

def encode(px, w, h, head=b''):
    rows = [rle_row(px[y * w:(y + 1) * w]) for y in range(h)]
    table, off = [], len(head) + 4 * h
    for r in rows: table.append(off); off += len(r)
    return head + struct.pack(f'<{h}I', *table) + b''.join(rows)

def logos(rom):
    path = os.path.join(SRC, 'logo', rom + '.png')
    if not os.path.exists(path): return [None] * 3
    src = Image.open(path).convert('RGBA')
    src = src.crop(src.getbbox() or (0, 0, src.width, src.height))
    out = []
    for (bw, bh), dim in zip(LOGO_BOXES, LOGO_DIM):
        k = min(bw / src.width, bh / src.height)
        im = src.resize((max(1, round(src.width * k)), max(1, round(src.height * k))), Image.LANCZOS)
        idx = bytearray(to_cube(ImageEnhance.Brightness(im.convert('RGB')).enhance(dim)).tobytes())
        for i, a in enumerate(im.getchannel('A').tobytes()):
            if a <= 110: idx[i] = CLEAR               # the panel has no partial transparency
        out.append((bytes(idx), im.width, im.height, b''))
    return out

def snap(rom):
    path = os.path.join(SRC, 'snap', rom + '.png')
    if not os.path.exists(path): return None
    s = Image.open(path).convert('RGB')
    k = max(W / s.width, H / s.height)                # fill the panel, crop what hangs over
    s = s.resize((round(s.width * k), round(s.height * k)), Image.NEAREST)
    x, y = (s.width - W) // 2, (s.height - H) // 2
    s = ImageEnhance.Brightness(s.crop((x, y, x + W, y + H))).enhance(SNAP_DIM)
    q = s.quantize(colors=SNAP_COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q.convert('RGB').save(os.path.join(PREV, rom + '.png'))
    pal = bytes((q.getpalette() + [0] * 3 * SNAP_COLOURS)[:3 * SNAP_COLOURS])
    px = bytearray(q.tobytes())
    for i, v in enumerate(px):
        assert v < SNAP_COLOURS
        px[i] = SNAP_BASE + v
    return (bytes(px), W, H, pal)

def main():
    os.makedirs(PREV, exist_ok=True)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    _, rows, _ = configure.resolve(configure.load())
    if not rows:
        sys.exit('no games in this build - put an approved ROM zip in roms/')
    # the wheel runs by title, not by ROM name - 'sf2' sorts before 'starwars' but
    # Street Fighter II comes after Star Wars - with the launcher's own entries last
    rows = sorted(rows, key=lambda r: (bool(r.get('builtin')), r['title'].lower()))

    cur = 8 + ENTRY.size * len(rows)
    entries, blobs = [], []
    for r in rows:
        rom, fields = r['rom'], []
        for img in (logos(rom) + [snap(rom)]) if not r.get('builtin') else [None] * 4:
            if img is None: fields += [0, 0, 0, 0]; continue
            px, w, h, head = img
            data = encode(px, w, h, head)
            for y in range(h):                        # what the firmware will read back
                o = struct.unpack_from('<I', data, len(head) + 4 * y)[0]
                assert unrle_row(data[o:], w) == px[y * w:(y + 1) * w], (rom, y)
            data += b'\0' * (-len(data) % 4)
            fields += [w, h, cur, len(data)]
            blobs.append(data); cur += len(data)
        entries.append(ENTRY.pack(rom.encode(), r['boot'].encode(), r['title'].encode(),
                                  r.get('by', '').encode(), *fields))
        print(f'  {rom:<11}{r["title"]:<20}logo {fields[0]:>3}x{fields[1]:<3} '
              f'{(fields[3] + fields[7] + fields[11]) / 1024:>5.1f}K   snap {fields[15] / 1024:>5.1f}K')

    with open(OUT, 'wb') as f:
        f.write(b'MQ04' + struct.pack('<HH', len(rows), ENTRY.size))
        f.write(b''.join(entries))
        f.write(b''.join(blobs))

    size = os.path.getsize(OUT)
    print(f'\n{len(rows)} entries -> {OUT} ({size/1024:.1f} KB)')
    print(f'mqart partition must be at least 0x{(size + 0xFFFF) & ~0xFFFF:X}')

if __name__ == '__main__':
    main()
