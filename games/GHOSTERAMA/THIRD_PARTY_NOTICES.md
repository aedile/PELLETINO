# Third-party code

## Marat Fayzullin's Z80 emulator (non-commercial) — not distributed here

GHOSTERAMA builds against the portable Z80 emulator by Marat Fayzullin,
copyright (C) Marat Fayzullin 1994-2007, from http://fms.komkon.org/EMUL8/.
Its terms, from the source headers: "You are not allowed to distribute this
software commercially. Please, notify me, if you make any changes to this
file."

**This repository does not ship those files.**
`components/z80_cpu/src/.gitignore` excludes them; only our own wrapper,
`components/z80_cpu/src/z80_cpu.c`, is committed. Drop `Z80.c`, `Z80.h`,
`Codes.h`, `CodesCB.h`, `CodesED.h`, `CodesXCB.h`, `CodesXX.h` and `Tables.h`
into that directory yourself and build with `LSB_FIRST` defined.

A GHOSTERAMA image built that way still embeds the emulator, so the resulting
binary may be shared but not sold.

## Galagino (Till Harbaum) — no license file

Three files say in their headers that they were ported from Galagino,
https://github.com/harbaum/galagino, by Till Harbaum:
`components/pacman_hw/src/pacman_hw.cpp` (memory map and I/O),
`components/pacman_hw/src/pacman_video.cpp` (tile and sprite rendering) and
`components/audio_hal/src/namco_wsg.cpp` (the Namco WSG sound generator).
`tools/convert_roms.py` is based on Galagino's ROM conversion scripts.

Galagino's repository carries **no license file**, so by default its author
keeps all rights in it. Galagino is credited as the source these files were
learned from, and the wording "ported from" in those headers is the author's
own; until Till Harbaum grants a license or confirms the files are independent
work, treat those four files as **not** covered by the 0BSD grant in LICENSE.

## Everything else

The rest of `main/` and `components/` was written for this project and is
0BSD, like the launcher.

See `../../LICENSING.md` for how this fits the bundle as a whole.
