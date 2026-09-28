#!/usr/bin/env python3
"""Fetch marquee art for the games you have, from a third-party archive.

This project hosts no game art. This pulls it, on demand, from a community
archive of arcade marquees organised by MAME name, the same way Galagino points
you at a search engine for ROMs - we distribute nothing, we point at where it
lives, and running this is your call.

  ./pelletino art            fetch for every enabled game that lacks a PNG
  ./pelletino art <rom>...   fetch just these

The default source is a collection of the real illustrated arcade marquees -
the wide painted header art, the same style the bundled games already use (the
existing marquees/*.png came from here; frogger.png is byte-identical). Files
are named by the MAME short name, so a game's ROM name is usually the file name;
the few exceptions are in NAMES below. Whatever comes down is saved at its full
resolution to marquees/<rom>.png, and tools/pack_marquees.py fits it to the
medal's box when it builds the blob.

Override the source with PELLETINO_ART_BASE (a URL with {name} for the file
name, url-encoded), e.g. libretro's title screens:
  PELLETINO_ART_BASE='https://raw.githubusercontent.com/libretro-thumbnails/MAME/master/Named_Titles/{name}.png'
"""
import os, sys, urllib.parse, urllib.request, io
from PIL import Image
import configure

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART  = os.path.join(ROOT, 'marquees')

BASE = os.environ.get('PELLETINO_ART_BASE',
    'https://raw.githubusercontent.com/arcadeforge/regamebox_marquees_arcade/HEAD/marquees/{name}.png')

# The archive names a few games differently from our ROM name. Anything not
# listed is tried as the ROM name itself, and reported if it 404s.
NAMES = {
    'arkanoidu': 'arkanoid',
}

def fetch(rom):
    name = NAMES.get(rom, rom)
    url = BASE.format(name=urllib.parse.quote(name))
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            data = r.read()
    except Exception as e:
        print(f'  {rom:<11} could not fetch ({e}); name tried: "{name}"')
        print(f'             set the name in tools/fetch_marquees.py NAMES, or drop marquees/{rom}.png yourself')
        return False
    try:
        im = Image.open(io.BytesIO(data)).convert('RGB')
    except Exception as e:
        print(f'  {rom:<11} what came back is not an image ({e}); name tried: "{name}"')
        return False
    im.save(os.path.join(ART, rom + '.png'))     # full resolution; pack_marquees fits it to the box
    print(f'  {rom:<11} fetched -> marquees/{rom}.png  ({im.width}x{im.height})')
    return True

def main():
    os.makedirs(ART, exist_ok=True)
    want = sys.argv[1:]
    if not want:
        cfg = configure.load(); _, rows, _ = configure.resolve(cfg)
        want = [r['rom'] for r in rows if not os.path.exists(os.path.join(ART, r['rom'] + '.png'))]
        if not want:
            print('  every enabled game already has a marquee'); return
    for rom in want:
        fetch(rom)

if __name__ == '__main__':
    main()
