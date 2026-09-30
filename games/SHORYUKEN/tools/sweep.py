#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Build, flash and bench a list of configurations, and write the table.

usage: tools/sweep.py <port> [--out results.csv] [--only N,N,...] [--list] [knob=value ...]
       tools/sweep.py --markdown results.csv      (the table, as the README has it)

Every combination of VIDEO_MODE, FRAME_SKIP, SOUND and SOUND_RATE that means something:
two video modes, four frame skips, and the sound off or on at three rates with and without
the samples. 56 builds, about three minutes each. Knobs given on the command line are
applied to every one of them (FRAME_SKIP_AUTO=0 is, unless you say otherwise, so that
FRAME_SKIP means what it says).

Each row is the "bench" line the firmware prints: the stages averaged over 25 seconds of
the first attract fight. Two columns are worked out here:

  need   what the stages other than idle come to, scaled to the machine running at its
         proper 59.64 frames a second. Under 1000 ms it holds real time, over it cannot.
  speed  how fast the machine actually ran, as a percentage of the real one.

A row can be reproduced on its own with the command in its last column.
"""
import csv, re, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REAL_FPS = 59.637

def configs():
    out = []
    for vmode in ('SCALE', 'CROP'):
        for skip in (0, 1, 2, 3):
            out.append(dict(VIDEO_MODE=vmode, FRAME_SKIP=skip, SOUND='OFF', SOUND_RATE=22050))
            for sound in ('FM', 'FM_ADPCM'):
                for rate in (11025, 22050, 44100):
                    out.append(dict(VIDEO_MODE=vmode, FRAME_SKIP=skip, SOUND=sound, SOUND_RATE=rate))
    return out

def parse(line):
    d = {}
    for k, v in re.findall(r'(\w+)=(\S+)', line):
        d[k] = v[:-2] if v.endswith('ms') else v
    return d

def run(port, knobs):
    args = [f'{k}={v}' for k, v in knobs.items()]
    p = subprocess.run([str(HERE / 'bench.sh'), port, *args], capture_output=True, text=True)
    lines = [l for l in p.stdout.splitlines() if l.startswith('bench fps=')]
    if not lines:
        tail = '\n'.join((p.stdout + p.stderr).splitlines()[-15:])
        return None, tail
    return parse(lines[-1]), None

def derive(r):
    busy = sum(float(r[k]) for k in ('m68k', 'z80', 'ym', 'video', 'strips', 'input', 'other'))
    emu = float(r['emu'])
    r['speed'] = f'{100 * emu / REAL_FPS:.0f}'
    r['need'] = f'{busy * REAL_FPS / emu:.0f}' if emu > 0 else ''
    return r

COLS = ['vmode', 'skip', 'sound', 'rate', 'fps', 'emu', 'speed', 'm68k', 'z80', 'ym', 'video', 'strips', 'idle',
        'need', 'heap', 'minheap', 'under']

def markdown(path):
    """results.csv as the README's table"""
    rows = list(csv.DictReader(open(path)))
    head = ('| # | video | skip | sound | rate | drawn fps | machine fps | speed | 68000 | Z80 | sound out | '
            'video | strips | idle | needs | free heap |')
    print(head)
    print('|' + '---|' * (head.count('|') - 1))
    for r in rows:
        if r['fps'] == 'failed':
            print(f"| {r['n']} | {r['vmode']} | {r['skip']} | {r['sound']} | {r['rate']} | failed |" + ' |' * 10)
            continue
        snd = r['sound'] != 'OFF'
        print(f"| {r['n']} | {r['vmode']} | {r['skip']} | {r['sound']} | {r['rate'] if snd else '-'} | {r['fps']} | {r['emu']} | "
              f"{r['speed']}% | {float(r['m68k']):.0f} | {float(r['z80']):.0f} | {float(r['ym']):.0f} | {float(r['video']):.0f} | "
              f"{float(r['strips']):.0f} | {float(r['idle']):.0f} | {r['need']} | {int(r['minheap']) // 1024} KB |")

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == '--markdown':
        markdown(sys.argv[2])
        return
    port = sys.argv[1]
    out = HERE.parent / 'results.csv'
    only = None
    extra = {'FRAME_SKIP_AUTO': '0'}
    args = sys.argv[2:]
    while args:
        a = args.pop(0)
        if a == '--out': out = Path(args.pop(0))
        elif a == '--only': only = [int(x) for x in args.pop(0).split(',')]
        elif a == '--list':
            for i, c in enumerate(configs()): print(i, ' '.join(f'{k}={v}' for k, v in c.items()))
            return
        elif '=' in a:
            k, v = a.split('=', 1); extra[k] = v
    todo = [(i, c) for i, c in enumerate(configs()) if only is None or i in only]
    new = not out.exists()
    with open(out, 'a', newline='') as f:
        w = csv.writer(f)
        if new:
            w.writerow(['n'] + COLS + ['command'])
        for i, c in todo:
            knobs = {**c, **extra}
            r, err = run(port, knobs)
            cmd = 'tools/bench.sh PORT ' + ' '.join(f'{k}={v}' for k, v in knobs.items())
            if r is None:
                print(f'{i}: FAILED {knobs}\n{err}', flush=True)
                w.writerow([i] + [c.get('VIDEO_MODE'), c.get('FRAME_SKIP'), c.get('SOUND'), c.get('SOUND_RATE')] + ['failed'] * (len(COLS) - 4) + [cmd])
            else:
                r = derive(r)
                print(f'{i}: ' + ' '.join(f'{k}={r[k]}' for k in COLS), flush=True)
                w.writerow([i] + [r[k] for k in COLS] + [cmd])
            f.flush()

if __name__ == '__main__':
    main()
