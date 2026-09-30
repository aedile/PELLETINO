#!/usr/bin/env python3
"""Convert P6 PPM files to PNG using only the standard library."""
import sys, zlib, struct
def convert(src, dst):
    data = open(src, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split()); px = parts[3]
    raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)
for f in sys.argv[1:]:
    convert(f, f.rsplit('.', 1)[0] + '.png')
