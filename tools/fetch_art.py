#!/usr/bin/env python3
"""Fetch the wheel's artwork for the games you have, from a third-party archive.

This project hosts no game art. This pulls it, on demand, from Arcade Database
(adb.arcadeitalia.net), which files everything under the MAME name - the same
way Galagino points you at a search engine for ROMs. We distribute nothing, we
point at where it lives, and running this is your call.

  ./pelletino art            fetch for every enabled game that lacks artwork
  ./pelletino art <rom>...   fetch just these, again

Two pictures per game, saved at full size and fitted to the panel later by
tools/pack_art.py:

  art/logo/<rom>.png   the game's logo on a transparent background
  art/snap/<rom>.png   a screenshot, shown dimmed behind the wheel

Both are optional. A game with no logo gets its title in text, a game with no
screenshot gets the launcher's own backdrop, and you can drop your own PNGs
into those folders instead of fetching - nothing here overwrites a file that
is already there unless you name the game.

Override the source with PELLETINO_ART_BASE, a URL with {kind} (decals for
logos, ingames for screenshots) and {name} in it.
"""
import io, os, sys, urllib.parse, urllib.request
from PIL import Image
import configure

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART  = os.path.join(ROOT, 'art')

BASE = os.environ.get('PELLETINO_ART_BASE',
    'https://adb.arcadeitalia.net/media/mame.current/{kind}/{name}.png')
KINDS = {'logo': 'decals', 'snap': 'ingames'}      # ours -> the archive's

# The archive files a regional set under its parent. Anything not listed is
# tried as the ROM name itself, and reported if it is not there.
NAMES = {
    'arkanoidu': 'arkanoid',
    'mpatrolw':  'mpatrol',
}

def fetch(rom, what):
    name = NAMES.get(rom, rom)
    url = BASE.format(kind=KINDS[what], name=urllib.parse.quote(name))
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'pelletino-fetch-art'})
        with urllib.request.urlopen(req, timeout=20) as r:
            data = r.read()
        im = Image.open(io.BytesIO(data)); im.load()
    except Exception as e:
        print(f'  {rom:<11}{what:<5} not fetched ({e}); name tried: "{name}"')
        return False
    im.save(os.path.join(ART, what, rom + '.png'))
    print(f'  {rom:<11}{what:<5} {im.width}x{im.height}')
    return True

def main():
    for what in KINDS: os.makedirs(os.path.join(ART, what), exist_ok=True)
    named = sys.argv[1:]
    if named:
        roms = named
    else:
        _, rows, _ = configure.resolve(configure.load())
        roms = [r['rom'] for r in rows if not r.get('builtin')]     # part of the launcher: nothing to find
    missed = 0
    for rom in roms:
        for what in KINDS:
            if named or not os.path.exists(os.path.join(ART, what, rom + '.png')):
                missed += not fetch(rom, what)
    if missed:
        print(f'\n  {missed} missing. Add the archive\'s name for the game to NAMES in tools/fetch_art.py,')
        print('  or put your own PNG in art/logo/ or art/snap/. The build works without them.')

if __name__ == '__main__':
    main()
