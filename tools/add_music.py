#!/usr/bin/env python3
"""
Install a tune into one of the launcher's two music slots.

    tools/add_music.py splash  ~/Downloads/some-game.nsf  3
    tools/add_music.py credits https://example.org/some-archive.7z  12
    tools/add_music.py splash  --remove

The source is a file or a URL that YOU supply: an .nsf, a .mid, or a .zip/.7z
holding one. The optional number is the track within an NSF (they usually hold a
whole game's soundtrack); leave it off to play the file's default.

Nothing installed here is ever committed - music/ is gitignored apart from its
README. What you play, and whether you are entitled to it, is between you and
whoever owns it. Say who wrote it in music/credits.txt; the credits roll on the
medal shows that file.
"""
import os, shutil, subprocess, sys, tempfile, urllib.request

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MUSIC = os.path.join(ROOT, 'music')
SLOTS = ('splash', 'credits')
KINDS = ('.nsf', '.mid')

def die(msg):
    print(f'\n  error: {msg}\n', file=sys.stderr); sys.exit(1)

def clear(slot):
    for ext in KINDS + ('.track',):
        p = os.path.join(MUSIC, slot + ext)
        if os.path.exists(p): os.remove(p)

def find_tune(path, tmp):
    """The tune itself, unpacking an archive if that is what we were given."""
    ext = os.path.splitext(path)[1].lower()
    if ext == '.midi': ext = '.mid'
    if ext in KINDS: return path, ext
    if ext in ('.zip', '.7z'):
        # bsdtar reads both, and ships with macOS and most Linux distributions
        if subprocess.run(['tar', '-xf', path, '-C', tmp]).returncode != 0:
            die(f'could not unpack {os.path.basename(path)} (is it a real {ext} archive?)')
        found = sorted(os.path.join(r, f) for r, _, fs in os.walk(tmp) for f in fs
                       if os.path.splitext(f)[1].lower() in KINDS + ('.midi',))
        if not found: die('no .nsf or .mid inside that archive')
        if len(found) > 1:
            print('  several tunes in the archive; taking the first:')
            for f in found: print('    ' + os.path.relpath(f, tmp))
        e = os.path.splitext(found[0])[1].lower()
        return found[0], '.mid' if e == '.midi' else e
    die(f'do not know what to do with a {ext or "file with no extension"}')

def describe(path, ext):
    d = open(path, 'rb').read()
    if ext == '.nsf':
        if d[:5] != b'NESM\x1a': die('that .nsf does not start with an NSF header')
        field = lambda o: d[o:o+32].split(b'\0')[0].decode('latin1').strip()
        return (f'NSF: "{field(0x0e)}", {field(0x4e)}, {d[6]} tracks'
                + (', uses expansion sound (those channels will be missing)' if d[0x7b] else '')), d[6]
    if d[:4] != b'MThd': die('that .mid does not start with a MIDI header')
    return f'MIDI: {len(d)} bytes', 0

def main():
    a = sys.argv[1:]
    if len(a) < 2 or a[0] not in SLOTS:
        print(__doc__); sys.exit(2)
    slot, src = a[0], a[1]
    os.makedirs(MUSIC, exist_ok=True)
    if src == '--remove':
        clear(slot); print(f'  {slot}: music removed; that screen will be silent'); return
    track = a[2] if len(a) > 2 else None
    if track is not None and not track.isdigit(): die('the track must be a number')

    with tempfile.TemporaryDirectory() as tmp:
        if src.startswith(('http://', 'https://')):
            name = os.path.basename(urllib.request.urlparse(src).path) or 'download'
            local = os.path.join(tmp, urllib.request.unquote(name))
            print(f'  fetching {urllib.request.unquote(name)}')
            req = urllib.request.Request(src, headers={'User-Agent': 'pelletino-add-music'})
            with urllib.request.urlopen(req, timeout=60) as r, open(local, 'wb') as f:
                shutil.copyfileobj(r, f)
            src = local
        if not os.path.exists(src): die(f'{src} does not exist')
        unpack = os.path.join(tmp, 'x'); os.makedirs(unpack)
        tune, ext = find_tune(src, unpack)
        what, ntracks = describe(tune, ext)
        if track and ntracks and not 1 <= int(track) <= ntracks:
            die(f'track {track} asked for, but the file has {ntracks}')
        clear(slot)
        shutil.copyfile(tune, os.path.join(MUSIC, slot + ext))
        if track and ext == '.nsf':
            open(os.path.join(MUSIC, slot + '.track'), 'w').write(track + '\n')

    print(f'  {slot}: {what}' + (f', playing track {track}' if track and ext == ".nsf" else ''))
    print(f'  installed as music/{slot}{ext} (never committed)')
    print('  now say who wrote it in music/credits.txt, then ./pelletino build')

if __name__ == '__main__':
    main()
