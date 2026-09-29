#!/usr/bin/env python3
"""
Turn games.toml plus whatever is in roms/ into a partition table and a build manifest.

    python3 tools/configure.py            # report and generate
    python3 tools/configure.py --check    # report only, change nothing

A game is in the build when its ROM zip is present in roms/, unless games.toml
overrides that with `enabled`. Disabling a game FREES ITS SLOT - the partition
table is generated, not hand-maintained, so an eight-game build gets eight slots
and the rest of the flash stays empty.

Needs only the standard library.
"""
import argparse, json, os, sys, tomllib

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONF   = os.path.join(ROOT, 'games.toml')
ROMS   = os.path.join(ROOT, 'roms')
ART    = os.path.join(ROOT, 'art', 'logo')
GAMES  = os.path.join(ROOT, 'games')
BLOB   = os.path.join(ROOT, 'lcd', 'marquees.bin')
SEL    = os.path.join(ROOT, 'selection.txt')
PARTS  = os.path.join(ROOT, 'partitions.csv')
MANIF  = os.path.join(ROOT, 'build', 'manifest.json')

OTA_MAX  = 16          # ESP-IDF hard cap: ota_0 .. ota_15
K        = 1024
ALIGN    = 0x10000     # app partitions must start on a 64 KB boundary

# fixed head of the table
HEAD = [
    ('nvs',      'data', 'nvs',     0x9000,  0x6000),
    ('otadata',  'data', 'ota',     0xF000,  0x2000),
    ('phy_init', 'data', 'phy',     0x11000, 0x1000),
]

def die(msg):
    print(f'\n  error: {msg}\n', file=sys.stderr)
    sys.exit(1)

def load():
    if not os.path.exists(CONF):
        die('games.toml not found')
    with open(CONF, 'rb') as f:
        return tomllib.load(f)

def load_selection():
    """The optional pick list written by `pelletino pick`. One ROM name per line;
    blank lines and #comments ignored. Returns a set, or None when there is no file
    (in which case every game whose ROM is present is included, as before)."""
    if not os.path.exists(SEL):
        return None
    picked = set()
    for line in open(SEL):
        line = line.split('#', 1)[0].strip()
        if line:
            picked.add(line)
    return picked


def resolve(cfg, picked='auto'):
    """Decide what is in the build, and why.

    picked='auto' reads selection.txt (the normal path); pass a set (or None for
    'everything present') to evaluate a hypothetical selection - `pelletino pick`
    does this to price a candidate build."""
    b = cfg.get('build', {})
    default_slot = b.get('default_slot_kb', 768)
    if picked == 'auto':
        picked = load_selection()
    rows, skipped = [], []

    for g in cfg.get('game', []):
        rom   = g['rom']
        zip_p = os.path.join(ROMS, rom + '.zip')
        art_p = os.path.join(ART,  rom + '.png')
        has_rom, has_art = os.path.exists(zip_p), os.path.exists(art_p)

        # A game whose payload is a data file rather than a ROM - a video clip - is switched
        # on by that file existing under its project, the same way a ROM zip switches on an
        # emulated game: put the file there and it is in the build.
        data_file = g.get('data_file')
        data_p = os.path.join(GAMES, g.get('project') or '', data_file) if data_file else None
        has_payload = os.path.exists(data_p) if data_p else has_rom
        payload_desc = (f"{g.get('project')}/{data_file}" if data_p else f'roms/{rom}.zip')

        # A game may RIDE another game's slot: `boots = "<owner-rom>"` gives it a menu
        # entry and marquee but no partition of its own. It chain-boots the owner's
        # image (which must carry this ROM too - GHOSTERAMA bakes in both Pac-Men) and
        # records its own ROM as the selection so that image runs the right variant.
        # A built-in entry is handled by the launcher itself rather than by
        # chain-booting an image: it gets a menu entry and a marquee but no
        # partition. Its boot label carries '@' so the firmware can tell.
        builtin = g.get('builtin')
        owner = g.get('boots')
        boot  = ('@' + builtin) if builtin else (owner or rom)

        forced = g.get('enabled')
        if builtin:
            on, why = (forced is not False), 'built into the launcher'
        elif forced is None:
            on = has_payload and (picked is None or rom in picked)
        else:
            on = bool(forced)
        why = ('forced on' if forced is True else
               'disabled in games.toml' if forced is False else
               payload_desc if has_payload else ('no ROM' if not data_p else f'no {data_file}'))
        if forced is None and has_payload and picked is not None and rom not in picked:
            why = 'not picked (pelletino pick)'
        if owner:
            why = f'shares {owner}' + ('' if has_payload else f' (no roms/{rom}.zip)')
            if forced is None and picked is not None and rom not in picked:
                why = 'not picked (pelletino pick)'

        if forced is True and not has_payload:
            die(f'{rom} is forced on in games.toml but {payload_desc} is missing')
        # art is optional: a game without any is drawn with its title in text

        # every app partition must start on a 64 KB boundary, so a slot is a whole number of them
        slot_kb = g.get('slot_kb', default_slot)
        slot_kb = -(-slot_kb // 64) * 64
        rec = dict(rom=rom, title=g.get('title', rom), by=g.get('by', ''), project=g.get('project'),
                   binary=g.get('binary'),          # optional: where this game's .bin is, under the project
                   data_kb=g.get('data_kb', 0),     # optional: a data partition of its own, e.g. a video clip
                   data_file=g.get('data_file'),    # optional: the file to flash into it, under the project
                   boot=boot, owner=owner, builtin=builtin,   # boot label; owner set iff this entry rides another's slot
                   slot_kb=slot_kb, why=why, has_art=has_art)
        (rows if on else skipped).append(rec)

    # A shared entry only belongs in the build if its owner made it in - it has no
    # image of its own to fall back on.
    live = {r['rom'] for r in rows}
    kept = []
    for r in rows:
        if r['owner'] and r['owner'] not in live:
            r['why'] = f'{r["owner"]} not in build'
            skipped.append(r)
        else:
            kept.append(r)

    return b, kept, skipped

def mqart_kb(n):
    """Size the artwork partition: exact if the blob exists, else estimated."""
    if os.path.exists(BLOB):
        need = os.path.getsize(BLOB)
        exact = True
    else:
        need, exact = n * 30 * K, False           # ~30 KB per game
    kb = ((need + need // 4 + 0xFFFF) // 0x10000) * 64   # +25% headroom, 64 KB granules
    return max(kb, 64), exact

def build_table(b, rows, art_kb):
    parts = list(HEAD)
    off = 0x20000
    parts.append(('launcher', 'app', 'factory', off, b.get('launcher_kb', 512) * K))
    off += b.get('launcher_kb', 512) * K
    parts.append(('mqart', 'data', '0x40', off, art_kb * K))
    off += art_kb * K
    off = (off + ALIGN - 1) & ~(ALIGN - 1)

    slot = 0
    for r in rows:
        if r.get('owner') or r.get('builtin'):
            continue                       # rides another slot, or lives in the launcher
        parts.append((r['rom'], 'app', f'ota_{slot}', off, r['slot_kb'] * K))
        r['offset'] = off
        r['ota'] = slot
        off += r['slot_kb'] * K
        slot += 1
    # shared entries point at their owner's offset - no flash of their own
    by_rom = {r['rom']: r for r in rows}
    for r in rows:
        if r.get('builtin'):
            r['offset'] = 0
        elif r.get('owner'):
            o = by_rom.get(r['owner'])
            r['offset'] = o['offset'] if o else 0

    # A game may ask for a data partition of its own - a video clip, say. It is laid out
    # after the app slots and labelled "media", which is the label the player looks for,
    # so only one game in a build may have one.
    data_games = [r for r in rows if r.get('data_kb')]
    if len(data_games) > 1:
        die('only one game per build may have data_kb: ' + ', '.join(r['rom'] for r in data_games))
    for r in data_games:
        off = (off + ALIGN - 1) & ~(ALIGN - 1)
        parts.append(('media', 'data', '0x40', off, r['data_kb'] * K))
        r['data_offset'] = off
        off += r['data_kb'] * K
    return parts, off

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='report only, write nothing')
    args = ap.parse_args()

    cfg = load()
    b, rows, skipped = resolve(cfg)
    flash = b.get('flash_mb', 16) * 1024 * K

    if not [r for r in rows if not r.get('builtin')]:
        die('nothing to build - put an approved ROM zip in roms/ (see games.toml)')
    nslots = sum(1 for r in rows if not r.get('owner') and not r.get('builtin'))
    if nslots > OTA_MAX:
        die(f'{nslots} slots needed but ESP-IDF allows at most {OTA_MAX} '
            f'(ota_0..ota_{OTA_MAX-1}). Run ./pelletino pick to choose {OTA_MAX} of them.')

    art_kb, exact = mqart_kb(len(rows))
    parts, end = build_table(b, rows, art_kb)
    if end > flash:
        die(f'needs {end/1024/1024:.2f} MB but the flash is {flash/1024/1024:.0f} MB. '
            f'Run ./pelletino pick to choose a build that fits.')

    # --- report -------------------------------------------------------------
    print(f'\n  PELLETINO  -  {len(rows)} game{"" if len(rows)==1 else "s"} in this build\n')
    w = max(len(r['title']) for r in rows)
    for r in rows:
        proj = r['project'] or '-'
        if r.get('builtin'):
            tag = ' built'
            size = '     '
        elif r.get('owner'):
            tag = ' ->  '
            size = '     '
        else:
            tag = f'ota_{r["ota"]:<2}'
            size = f'{r["slot_kb"]:>5}'
        print(f'   {tag} {r["title"]:<{w}}  {size} KB  '
              f'0x{r["offset"]:06X}  {proj:<13} {r["why"]}')
    if skipped:
        print(f'\n  not in this build ({len(skipped)}):')
        for r in skipped:
            print(f'    {r["title"]:<{w}}  {r["why"]}')

    stray = sorted(f[:-4] for f in os.listdir(ROMS) if f.lower().endswith('.zip')) if os.path.isdir(ROMS) else []
    known = {g['rom'] for g in cfg.get('game', [])}
    extra = [s for s in stray if s not in known]
    if extra:
        print(f'\n  ignored, not on the approved list: {", ".join(extra)}')

    used = end / 1024 / K
    print(f'\n  launcher {b.get("launcher_kb",512)} KB'
          f'   artwork {art_kb} KB{"" if exact else " (estimated - run pack_art.py)"}'
          f'   slots {nslots}/{OTA_MAX}')
    print(f'  flash {used:.2f} / {flash/1024/K:.0f} MB   {flash/1024/K - used:.2f} MB free\n')

    if args.check:
        print('  --check: nothing written\n')
        return

    # --- emit ---------------------------------------------------------------
    out = ['# Generated by tools/configure.py from games.toml - DO NOT EDIT BY HAND.',
           '# Game slots are labelled with the ROM name; the launcher finds them by label.',
           '# Name,     Type, SubType,  Offset,    Size,      Flags']
    for n, t, s, o, z in parts:
        out.append(f'{n+",":<11}{t+",":<6}{s+",":<9}0x{o:06X},  0x{z:06X},')
    with open(PARTS, 'w') as f:
        f.write('\n'.join(out) + '\n')

    os.makedirs(os.path.dirname(MANIF), exist_ok=True)
    with open(MANIF, 'w') as f:
        json.dump({'games': [{k: r.get(k) for k in ('rom', 'title', 'project', 'binary', 'slot_kb', 'offset', 'ota', 'boot', 'owner', 'builtin', 'data_kb', 'data_file', 'data_offset')}
                             for r in rows],
                   'mqart_kb': art_kb, 'flash_mb': b.get('flash_mb', 16)}, f, indent=2)

    print(f'  wrote {os.path.relpath(PARTS, ROOT)} and {os.path.relpath(MANIF, ROOT)}\n')

if __name__ == '__main__':
    main()
