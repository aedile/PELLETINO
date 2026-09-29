# Licensing

PELLETINO is free for everyone. It is not for sale, and because of the
third-party emulator cores some games are built from, it cannot be — see
"The one catch" below.

This file summarises the whole bundle. Each game under `games/` carries its own
`LICENSE` and (where it has third-party code) `THIRD_PARTY_NOTICES.md`; those are
the authoritative word for that game. Nothing here relaxes any of their terms.

## The code written for this project: 0BSD

The launcher, the shared components, the build and asset tools, the host
harnesses, and the emulator logic written for this project are released under
the Zero-Clause BSD license (`LICENSE` at the root, and each game's `LICENSE`).
That is the most permissive license there is: use it, change it, ship it, no
attribution required.

## The one catch: the assembled bundle is free but non-commercial

Most of the games are built on **Marat Fayzullin's Z80 emulator**, which is
"freeware for non-commercial use" — it may be shared but **not sold**. Because
those games embed it, any collection that includes them is non-commercial too.
That matches the intent of the project (free to everyone), but it means you may
not sell PELLETINO, a medal flashed with it, or any build containing a
Fayzullin-Z80 game.

Games that embed the Fayzullin Z80 (non-commercial):

> Pole Position (QUALIFIER), Galaga
> (SWARMFIGHTER), Dig Dug (STRATUM), Donkey Kong (GIRDER), Frogger (RIBBIT),
> Rally-X (SMOKESCREEN), Arkanoid (VAUS), Mario Bros (PLUMBER), Mr. Do! (BIGTOP),
> Time Pilot (CHRONO), Root Beer Tapper (KEG), Moon Patrol (BUGGY), Space
> Invaders (PHALANX), Galaxian (ARMADA), Gyruss (TOCCATA).

Pac-Man / Ms. Pac-Man (GHOSTERAMA) also **builds against** the Fayzullin Z80, so
a flashed GHOSTERAMA image is non-commercial too — but this repository does not
ship that code for it. `games/GHOSTERAMA/components/z80_cpu/src/` is gitignored
except for our own wrapper, and you supply the emulator yourself.

## Emulator cores and MAME

Most machine cores under `games/*/core/` were written for this project with
MAME's drivers used as **hardware documentation** — memory maps, interrupt
timing, palette decoding, PROM contents and measured sound levels. Those are
facts about the silicon rather than copyrightable expression, and the cores are
independent implementations, typically a fraction of the size of the MAME driver
they were learned from.

This was checked rather than assumed. Eleven cores were compared line-for-line
against the current MAME source they cite:

| core | ours | MAME driver | identical lines |
|---|---|---|---|
| missile.c | 260 | atari/missile.cpp (1421) | 0 |
| centiped.c | 243 | atari/centiped.cpp (2398) | 0 |
| galaxian.c | 140 | galaxian/galaxian.cpp (17305) | 0 |
| polepos.c | 470 | namco/polepos.cpp (2581) | 0 |
| mrdo.c | 159 | universal/mrdo.cpp (533) | 0 |
| timeplt.c | 221 | konami/timeplt.cpp (1034) | 0 |
| rallyx.c | 161 | namco/rallyx.cpp (1559) | 0 |
| mario.c | 200 | nintendo/mario.cpp (1125) | 0 |
| btime.c | 165 | dataeast/btime.cpp (3293) | 0 |
| joust.c | 282 | williams/williams.cpp (4052) | 0 |
| mpatrol.c | 147 | irem/m52.cpp (1182) | 0 |

No shared line of substance in any of them. Every MAME driver involved is
**BSD-3-Clause**, so even on the most conservative reading — that a core counts
as derivative — the obligation is attribution and notice retention, not copyleft
and not a commercial restriction. Each game's `THIRD_PARTY_NOTICES.md` names the
drivers it was written from and carries that notice.

A few files are **not** independent implementations but straight ports of MAME
code, and say so in their headers, which carry a BSD-3-Clause line rather than
the project's 0BSD one:

| file | from |
|---|---|
| `QUALIFIER/core/z8000/z8002.c` | generated from MAME's Z8000 core (`z8000ops.hxx`, `z8000tbl.hxx`, `z8000.cpp`) |
| `SPINDLE`, `TRENCHRUNNER`, `WALKERRUN` `core/avg.c` | `avgdvg.cpp`, the Atari Analog Vector Generator |
| `SPINDLE/core/mathbox.c` | `mathbox.cpp` |
| `TRENCHRUNNER`, `WALKERRUN` `core/starwars.c` | `starwars.cpp`, `starwars_m.cpp` |
| `TRENCHRUNNER`, `WALKERRUN` `core/tms5220.c` | `tms5220.cpp` and the coefficient tables in `tms5110r.hxx` |
| `WALKERRUN/core/slapstic.c` | the pre-2022 `slapstic.cpp` |

BSD-3-Clause is permissive: those files may be used and sold, with the notice
kept. They do not change the bundle's licensing.

## GHOSTERAMA and Galagino: unresolved

Three files in `games/GHOSTERAMA` say in their headers that they were "ported
from Galagino" (Till Harbaum's ESP32 arcade emulator, the project that inspired
this one): `components/pacman_hw/src/pacman_hw.cpp`, `pacman_video.cpp` and
`components/audio_hal/src/namco_wsg.cpp`; `tools/convert_roms.py` is based on
Galagino's ROM converter. **Galagino publishes no license file**, which by
default means its author keeps all rights. Until he grants one, or confirms
those files are independent work, they are excluded from GHOSTERAMA's 0BSD
grant (see its `LICENSE` and `THIRD_PARTY_NOTICES.md`) and a flashed GHOSTERAMA
image should be treated as "share with his goodwill", not as licensed. The
other 25 games do not use any Galagino code.

## The launcher's own third-party code

### TinyMidiLoader (zlib)

`components/chiptune/include/tml.h` is TinyMidiLoader v0.7 by Bernhard Schelling,
part of [TinySoundFont](https://github.com/schellingb/TinySoundFont), used under
the zlib licence:

> Copyright (C) 2017, 2018, 2020 Bernhard Schelling
>
> This software is provided 'as-is', without any express or implied warranty. In
> no event will the authors be held liable for any damages arising from the use
> of this software. Permission is granted to anyone to use this software for any
> purpose, including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions: 1. The origin of this software
> must not be misrepresented; you must not claim that you wrote the original
> software. 2. Altered source versions must be plainly marked as such, and must
> not be misrepresented as being the original software. 3. This notice may not be
> removed or altered from any source distribution.

The file is vendored unaltered and keeps its own header notice. It parses the
MIDI files; the AY-3-8910 playback around it is this project's own and 0BSD.

### The NES sound chip and NSF player: ours

`components/chiptune/src/apu2a03.c` and `nsf.c` were written for this project so
that the launcher could play NSF music **without** an NES emulator. The obvious
one to borrow, nofrendo, is GPL and would have relicensed the launcher; this is
0BSD like the rest of it. The 6502 underneath is `m6502fast.h`, the same core the
Atari games here use.

The zlib licence is permissive and carries no copyleft or non-commercial term, so
it does not change the bundle's licensing.

## GPL-3.0 games (the 6809 titles)

Four games use the **vecx MC6809 core**, which is GPL-3.0. Those game images are
therefore effectively GPL-3.0: if you distribute one of them, you must offer its
source under the GPL, which this repository provides (`games/<game>/` plus the
`LICENSES/GPL-3.0.txt` each ships). GPL-3.0 is compatible with the 0BSD code
around it.

> Star Wars (TRENCHRUNNER), Gyruss (TOCCATA — also non-commercial, above),
> Joust (OSTRICH), Empire Strikes Back (WALKERRUN).

**Gyruss is a conflict, not a combination.** TOCCATA links the GPL-3.0 6809
(for the KONAMI-1 sub-CPU) with the Fayzullin Z80 (for the main CPU) in one
firmware. GPL-3.0 section 10 forbids imposing further restrictions on the
combined work, and "non-commercial only" is exactly such a restriction, so a
flashed TOCCATA image cannot be distributed under the GPL's terms at all. The
source files can sit side by side in this repository, and you can build the
image for yourself, but do not pass a built TOCCATA image to anyone else. The
fix is a permissively licensed 6809 or a permissively licensed Z80 for that one
game; neither is done yet.

## RealNetworks RPSL (the Street Fighter II video)

The MP3 side of the Street Fighter II video "game" (HADOUKEN) uses RealNetworks'
Helix fixed-point MP3 decoder under the RealNetworks Public Source License. That
component keeps its own notice and source-availability terms; see the component
directory and HADOUKEN's `THIRD_PARTY_NOTICES.md`. The RPSL is generally held to
be incompatible with the GPL, which is why the easter-egg clip player that used
to sit in the Star Wars and Empire Strikes Back projects (both GPL-3.0) has been
removed: HADOUKEN carries no GPL code, so it is the only place Helix lives.

## Fully permissive games (BSD-3-Clause / MIT — these could even be sold)

These games use only permissive MAME-derived or original code, with no
non-commercial or copyleft core:

> Centipede (CHILOPODA), Tempest (SPINDLE), Asteroids (AEROLITE), Missile
> Command (SILO), Burger Time (GRIDDLE), Lunar Lander (REGOLITH).

The non-commercial restriction on the bundle comes only from bundling them with
the Fayzullin-Z80 games; on their own they carry no such limit.

## Game ROMs, artwork and music: not here, your responsibility

This project distributes **no game ROMs, no artwork and no music**, and hosts none. The ROMs are
copyrighted by their owners; so are the logos and screenshots. The two photographs and the 3D
models under `games/GHOSTERAMA/` show the 1.0 medal, and the medal and its printed insert carry
Pac-Man's likeness; they are documentation of the build, not artwork offered for reuse.
`./pelletino art` can download those from a third-party archive on your request,
and you supply the ROMs yourself. The music is whatever you
install with `tools/add_music.py` (see `music/README.md`); nothing is committed, and the
composition and the particular sequence are both someone's to license. What you do with copyrighted ROMs and
art, and whether you are entitled to them, is between you and their owners.

---

Copyright (C) 2026 Jesse Castro. The project's own code is 0BSD; third-party
components keep the licenses named in each game's `THIRD_PARTY_NOTICES.md`.
