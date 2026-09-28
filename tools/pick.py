#!/usr/bin/env python3
"""
Choose which games go into a build - the "which ones are you picking" step.

More games are approved (and you may have more ROMs) than fit on the medal:
16 app slots at most, and a 16 MB flash the launcher, artwork and slots share.
This walks you through a pick list and prices it live - slots used and flash
used - so you can see a build fit before you commit to it.

    python3 tools/pick.py            # interactive
    python3 tools/pick.py --all      # pick everything that fits, in list order
    python3 tools/pick.py --clear    # forget the pick list (back to "all present")

The result is written to selection.txt, which configure.py honours. With no
selection.txt, every game whose ROM is present is included (the old behaviour),
so picking is entirely optional - reach for it only when a build overflows.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import configure as C

BOLD = '\033[1m'; DIM = '\033[2m'; GRN = '\033[32m'; RED = '\033[31m'; YEL = '\033[33m'; OFF = '\033[0m'


def price(cfg, picked):
    """Return (rows_on, nslots, used_bytes, flash_bytes, over) for a candidate set."""
    b, rows, _ = C.resolve(cfg, picked=picked)
    art_kb, _ = C.mqart_kb(len(rows))
    _, end = C.build_table(dict(b), [dict(r) for r in rows], art_kb)
    flash = b.get('flash_mb', 16) * 1024 * C.K
    nslots = sum(1 for r in rows if not r.get('owner'))
    over = end > flash or nslots > C.OTA_MAX
    return rows, nslots, end, flash, over


def candidates(cfg):
    """Every game whose payload is present - the universe you may pick from - each
    with the slot cost it adds (0 for a rider that shares a slot)."""
    _, rows, skipped = C.resolve(cfg, picked=None)      # None = everything present
    present = rows + [r for r in skipped
                      if r['why'].startswith(('roms/', 'GHOSTERAMA', 'shares')) or 'not picked' in r['why']]
    # de-dup, keep games.toml order
    seen, out = set(), []
    order = [g['rom'] for g in cfg.get('game', [])]
    by_rom = {r['rom']: r for r in present}
    for rom in order:
        if rom in by_rom and rom not in seen:
            seen.add(rom); out.append(by_rom[rom])
    return out


def fmt_budget(nslots, used, flash, over):
    mb = used / 1024 / C.K
    free = flash / 1024 / C.K - mb
    col = RED if over else GRN
    warn = f'  {RED}OVER BUDGET{OFF}' if over else ''
    return (f'{col}slots {nslots}/{C.OTA_MAX}   flash {mb:.2f}/{flash/1024/C.K:.0f} MB'
            f'   {free:.2f} MB free{OFF}{warn}')


def render(cfg, cand, chosen):
    rows, nslots, used, flash, over = price(cfg, chosen)
    on = {r['rom'] for r in rows}
    os.system('clear' if os.name != 'nt' else 'cls')
    print(f'\n  {BOLD}PELLETINO - pick your build{OFF}\n')
    for i, r in enumerate(cand, 1):
        picked = r['rom'] in chosen
        mark = f'{GRN}[x]{OFF}' if picked else '[ ]'
        rider = r.get('owner')
        cost = f'{DIM}rides {rider}{OFF}' if rider else f'{r["slot_kb"]:>5} KB'
        # a picked game that still isn't in `on` was squeezed out by the budget
        dropped = f'  {YEL}(does not fit){OFF}' if picked and r['rom'] not in on else ''
        print(f'   {i:>2}. {mark} {r["title"]:<20} {cost}   {DIM}{r["rom"]}{OFF}{dropped}')
    print(f'\n  {fmt_budget(nslots, used, flash, over)}\n')
    print(f'  {DIM}number toggle   a all-that-fit   n none   s save   q quit{OFF}')


def interactive(cfg, cand, chosen):
    while True:
        render(cfg, cand, chosen)
        try:
            cmd = input('  > ').strip().lower()
        except (EOFError, KeyboardInterrupt):
            print('\n  (no changes saved)\n'); return None
        if cmd in ('q', 'quit'):
            print('\n  (no changes saved)\n'); return None
        if cmd in ('s', 'save'):
            return chosen
        if cmd in ('n', 'none'):
            chosen = set(); continue
        if cmd in ('a', 'all'):
            chosen = greedy_fit(cfg, cand); continue
        if cmd.isdigit():
            k = int(cmd)
            if 1 <= k <= len(cand):
                rom = cand[k - 1]['rom']
                chosen = set(chosen) ^ {rom}
            continue


def greedy_fit(cfg, cand):
    """Add games in list order, keeping each only if the build still fits."""
    chosen = set()
    for r in cand:
        trial = chosen | {r['rom']}
        _, _, _, _, over = price(cfg, trial)
        if not over:
            chosen = trial
    return chosen


def save(chosen, cand):
    order = [r['rom'] for r in cand if r['rom'] in chosen]
    with open(C.SEL, 'w') as f:
        f.write('# Games picked for this build - edit or regenerate with `pelletino pick`.\n')
        f.write('# One MAME ROM name per line. Delete this file to include every ROM present.\n')
        for rom in order:
            f.write(rom + '\n')
    print(f'\n  {GRN}saved{OFF} {len(order)} game(s) to selection.txt - now run ./pelletino build\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true', help='pick everything that fits, non-interactively')
    ap.add_argument('--clear', action='store_true', help='remove selection.txt (build all present)')
    args = ap.parse_args()

    cfg = C.load()

    if args.clear:
        if os.path.exists(C.SEL):
            os.remove(C.SEL); print('\n  removed selection.txt - the build now includes every ROM present\n')
        else:
            print('\n  no selection.txt - already building every ROM present\n')
        return

    cand = candidates(cfg)
    if not cand:
        sys.exit('\n  no ROMs present to pick from - put approved ROM zips in roms/\n')

    # start from the existing selection if there is one, else everything present
    existing = C.load_selection()
    chosen = set(existing) if existing is not None else {r['rom'] for r in cand}

    if args.all:
        chosen = greedy_fit(cfg, cand)
        save(chosen, cand)
        return

    result = interactive(cfg, cand, chosen)
    if result is not None:
        save(result, cand)


if __name__ == '__main__':
    main()
