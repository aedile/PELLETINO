#!/usr/bin/env python3
"""
One command from a folder of ROMs to a flashed medal.

    ./install.sh                 everything, and flash the medal if one is plugged in
    ./install.sh --no-flash      build only
    ./install.sh --no-art        skip fetching artwork
    ./install.sh /dev/cu.usb...  flash a particular port

What it does, in order:

  1. checks the machine has what it needs, and says how to get what it lacks
  2. finds the ROM zips you put in roms/
  3. converts each game's ROMs and builds its firmware
  4. if more games were supplied than fit, keeps as many as fit
  5. fetches logos and screenshots for the menu
  6. builds the launcher and the partition table around the games that built
  7. flashes all of it

A game that fails is left out and named at the end with the reason; it does not
stop the others. Run it again whenever you add or remove a ROM.
"""
import argparse, glob, os, shutil, subprocess, sys, tempfile, time, zipfile

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Checked before anything else is imported: the project's own tools need 3.11, and
# someone on an older Python should be told so, not shown a traceback.
if sys.version_info < (3, 11):
    sys.exit(f'\n  cannot continue: Python 3.11 or newer is needed (this is {sys.version.split()[0]}).\n'
             f'  Install a newer Python and run this again.\n')

sys.path.insert(0, os.path.join(ROOT, 'tools'))
import configure as C                                    # noqa: E402
import pick as P                                         # noqa: E402

IDF_IMAGE = 'espressif/idf:v5.3.4'
LOGS      = os.path.join(ROOT, 'build', 'install-logs')
TTY       = sys.stdout.isatty()
B, D, R, G, Y, X = (('\033[1m', '\033[2m', '\033[31m', '\033[32m', '\033[33m', '\033[0m') if TTY else ('',) * 6)

def say(msg):  print(f'\n{B}==>{X} {msg}', flush=True)
def note(msg): print(f'    {msg}', flush=True)
def die(msg, fix=None):
    print(f'\n{R}  cannot continue:{X} {msg}')
    if fix: print(f'  {fix}')
    print(); sys.exit(1)

# ---------------------------------------------------------------- 1. preflight

def preflight(want_flash):
    say('checking this machine')
    if not shutil.which('docker'):
        die('Docker is not installed.', 'Get Docker Desktop from https://www.docker.com/ - the whole toolchain runs inside it.')
    if subprocess.run(['docker', 'info'], capture_output=True).returncode != 0:
        die('Docker is installed but not running.', 'Start Docker Desktop, wait for it to settle, and run this again.')
    note('Docker is running')
    if want_flash and not esptool():
        die('esptool is not installed, and it is what writes to the medal.',
            'Install it with:  brew install esptool    (or: pip install esptool)\n'
            '  or build without flashing:  ./install.sh --no-flash')
    if want_flash: note('esptool is installed')

def esptool():
    return shutil.which('esptool.py') or shutil.which('esptool')

def find_port():
    for pat in ('/dev/cu.usbmodem*', '/dev/ttyACM*', '/dev/cu.usbserial*', '/dev/ttyUSB*'):
        hits = sorted(glob.glob(pat))
        if hits: return hits[0]
    return None

# ---------------------------------------------------------------- 2. what was supplied

def survey(cfg):
    """Group the approved games by the project that builds them."""
    roms_dir = os.path.join(ROOT, 'roms')
    os.makedirs(roms_dir, exist_ok=True)
    present = {f[:-4] for f in os.listdir(roms_dir) if f.lower().endswith('.zip')}
    known = {g['rom']: g for g in cfg.get('game', [])}
    projects = {}                                        # project -> every rom it builds from
    for g in cfg.get('game', []):
        if g.get('builtin') or g.get('data_file') or not g.get('project'): continue
        if g.get('enabled') is False: continue
        projects.setdefault(g['project'], []).append(g['rom'])
    return present, known, projects

# ---------------------------------------------------------------- 3. convert and build

def run(cmd, log, cwd=None):
    with open(log, 'a') as f:
        f.write(f'\n$ {" ".join(cmd)}\n'); f.flush()
        return subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT).returncode

def last_lines(log, n=3):
    try:
        lines = [l.rstrip() for l in open(log, errors='replace') if l.strip()]
    except OSError:
        return ''
    keep = [l for l in lines if 'error' in l.lower() or 'missing' in l.lower() or 'not found' in l.lower() or 'crc' in l.lower()]
    return ' / '.join((keep or lines)[-n:])[:300]

def convert(project, roms, tmp, log):
    conv = os.path.join(ROOT, 'games', project, 'tools', 'convert_roms.py')
    if not os.path.exists(conv): return f'no converter at games/{project}/tools/convert_roms.py'
    several = len(roms) > 1
    for rom in roms:
        where = os.path.join(tmp, rom)
        os.makedirs(where, exist_ok=True)
        try:
            with zipfile.ZipFile(os.path.join(ROOT, 'roms', rom + '.zip')) as z:
                for m in z.infolist():                   # flatten: some zips nest a folder
                    if m.is_dir(): continue
                    with z.open(m) as src, open(os.path.join(where, os.path.basename(m.filename)), 'wb') as dst:
                        shutil.copyfileobj(src, dst)
        except zipfile.BadZipFile:
            return f'roms/{rom}.zip is not a valid zip file'
        cmd = [sys.executable, conv, where] + (['--game', rom] if several else [])
        if run(cmd, log, cwd=os.path.join(ROOT, 'games', project)) != 0:
            return f'roms/{rom}.zip was not accepted: {last_lines(log)}'
    return None

def build(project, log):
    gdir = os.path.join(ROOT, 'games', project)
    cmd = ['docker', 'run', '--rm', '-v', f'{gdir}:/project', '-w', '/project',
           # one compiler cache for every game: they share most of ESP-IDF, so after the
           # first game the rest compile mostly from cache
           '-e', 'IDF_CCACHE_ENABLE=1', '-e', 'CCACHE_DIR=/ccache', '-v', 'pelletino_ccache:/ccache',
           IDF_IMAGE, 'idf.py', '-B', 'build_docker', '-DIDF_TARGET=esp32c6', 'build']
    if run(cmd, log) != 0:
        return f'firmware did not build: {last_lines(log)}'
    return None

# ---------------------------------------------------------------- the whole thing

def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('port', nargs='?')
    ap.add_argument('--no-flash', action='store_true')
    ap.add_argument('--no-art', action='store_true')
    ap.add_argument('-h', '--help', action='store_true')
    a = ap.parse_args()
    if a.help: print(__doc__); return

    started = time.time()
    preflight(not a.no_flash)
    cfg = C.load()
    present, known, projects = survey(cfg)

    say('looking in roms/')
    approved = sorted(r for r in present if r in known)
    strangers = sorted(r for r in present if r not in known)
    if not approved:
        print(f'\n  There are no ROMs in roms/ that this project knows how to run.\n'
              f'  Put zip files there using their MAME names - for example roms/galaga.zip -\n'
              f'  and run this again. To see every name it accepts:  ./pelletino games\n')
        if strangers: print(f'  Found but not supported: {", ".join(strangers)}\n')
        sys.exit(1)
    note(f'{len(approved)} game ROM{"s" if len(approved) != 1 else ""} found: ' + ', '.join(known[r].get('title', r) for r in approved))
    if strangers: note(f'{D}not supported, ignored: {", ".join(strangers)}{X}')

    # which projects can be built from what is here
    todo, skipped = [], {}
    for project, roms in projects.items():
        have = [r for r in roms if r in present]
        if not have: continue
        lacking = [r for r in roms if r not in present]
        if lacking:
            for r in have:
                skipped[r] = (f'shares one firmware image with {", ".join(known[x].get("title", x) for x in lacking)}, '
                              f'which needs roms/{lacking[0]}.zip as well')
            continue
        todo.append((project, roms))

    # What fits depends on each game's slot size, which games.toml already knows - so
    # choose first and build only what is going on the medal. A game that then fails
    # to build gives its place up, and the choice is made again without it.
    os.makedirs(LOGS, exist_ok=True)
    project_of = {r: p for p, roms in todo for r in roms}
    roms_of = dict(todo)
    buildable = set(project_of)
    had_pick = C.load_selection()
    built, failed, tried = set(), set(), set()
    first_pass = True
    with tempfile.TemporaryDirectory() as tmp:
        while True:
            cand = [r for r in P.candidates(cfg)
                    if (r['rom'] in buildable and r['rom'] not in failed) or r.get('builtin') or r.get('data_file')]
            if had_pick is not None:
                chosen = {r['rom'] for r in cand if r['rom'] in set(had_pick) or r.get('builtin')}
            else:
                chosen = P.greedy_fit(cfg, cand)
            wanted = []
            for r in cand:
                p = project_of.get(r['rom'])
                if r['rom'] in chosen and p and p not in tried and p not in wanted: wanted.append(p)
            if not wanted: break

            if first_pass:
                say(f'converting ROMs and building firmware for {len(wanted)} game image{"s" if len(wanted) != 1 else ""}')
                note(f'{D}the first build downloads the toolchain and compiles everything; later ones are much faster{X}')
                if had_pick is not None: note('building the games in your selection.txt (delete it to let the installer choose)')
                first_pass = False
            else:
                say(f'a game dropped out, so {len(wanted)} more fit{"s" if len(wanted) == 1 else ""}')
            for i, project in enumerate(wanted, 1):
                roms = roms_of[project]
                tried.add(project)
                title = ' + '.join(known[r].get('title', r) for r in roms)
                log = os.path.join(LOGS, project + '.log')
                open(log, 'w').close()
                print(f'    [{i:>2}/{len(wanted)}] {title:<32}', end='', flush=True)
                t0 = time.time()
                why = convert(project, roms, os.path.join(tmp, project), log) or build(project, log)
                if why:
                    print(f'{R}failed{X}')
                    failed.update(roms)
                    for r in roms: skipped[r] = why + f'   (log: build/install-logs/{project}.log)'
                else:
                    print(f'{G}ok{X}  {D}{int(time.time() - t0)} s{X}')
                    built.update(roms)

    if not built:
        print(f'\n{R}  No game built.{X} The reasons:')
        for r, why in skipped.items(): print(f'    {known[r].get("title", r)}: {why}')
        print(); sys.exit(1)

    # ---- 4. what is going on the medal
    chosen = {r for r in chosen if r in built or not project_of.get(r)}
    for r in sorted(buildable - chosen - failed):
        skipped[r] = ('not in your selection.txt - add it with ./pelletino pick' if had_pick is not None
                      else 'there was no room left for it - choose which games go on with ./pelletino pick')
    # selection.txt is a list of games; what is built into the launcher is always there
    P.save({r['rom'] for r in cand if r['rom'] in chosen and not r.get('builtin')}, cand)

    # ---- 5-6. artwork, launcher
    venv_py = sys.executable
    if not a.no_art:
        say('fetching logos and screenshots')
        note(f'{D}from a third-party archive; a game with no artwork gets its title in text instead{X}')
        if subprocess.run([venv_py, os.path.join(ROOT, 'tools', 'fetch_art.py')], cwd=ROOT).returncode != 0:
            note(f'{Y}artwork could not be fetched - carrying on without{X}')
    say('building the launcher')
    if subprocess.run([os.path.join(ROOT, 'pelletino'), 'build'], cwd=ROOT).returncode != 0:
        die('the launcher did not build.', 'Run ./pelletino build to see the error in full.')

    # ---- 7. flash
    flashed = False
    if not a.no_flash:
        port = a.port or find_port()
        if not port:
            say('no medal found')
            note('Plug it in with a USB-C cable that carries data, then run:  ./pelletino flash')
        else:
            say(f'flashing the medal on {port}')
            flashed = subprocess.run([os.path.join(ROOT, 'tools', 'flash_all.sh'), port], cwd=ROOT).returncode == 0
            if not flashed: note(f'{R}flashing failed{X} - check the cable and try:  ./pelletino flash {port}')

    # ---- summary
    mins = (time.time() - started) / 60
    on = [known[r['rom']].get('title', r['rom']) for r in cand if r['rom'] in chosen and not r.get('builtin')]
    print(f'\n{B}  {"On the medal" if flashed else "Built"}: {len(on)} game{"s" if len(on) != 1 else ""}{X}  {D}({mins:.0f} min){X}')
    for t in on: print(f'    {G}*{X} {t}')
    if skipped:
        print(f'\n{B}  Left out:{X}')
        for r, why in skipped.items(): print(f'    {Y}-{X} {known[r].get("title", r)}: {why}')
    music = [s for s in ('splash', 'credits') if not any(os.path.exists(os.path.join(ROOT, 'music', s + e)) for e in ('.nsf', '.mid'))]
    if music:
        print(f'\n  {D}No music supplied for: {", ".join(music)}. Those screens are silent. See music/README.md.{X}')
    if not flashed and not a.no_flash:
        print(f'\n  Not flashed yet. When the medal is plugged in:  ./pelletino flash')
    print()

if __name__ == '__main__':
    main()
