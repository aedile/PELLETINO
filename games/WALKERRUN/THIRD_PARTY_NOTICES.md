# Third-party code

## vecx 6809 core (GPL-3.0)

`core/e6809.c` and `core/e6809.h` are the MC6809 emulator from vecx, the
Vectrex emulator by Valavan Manohararajah, as maintained at
https://github.com/jhawthorn/vecx. vecx is distributed under the GNU General
Public License version 3; the full text is in `LICENSES/GPL-3.0.txt`.

Modifications made here: bus accessors and the interrupt line can be supplied
as macros by the including file so they inline; a chunked `e6809_run()` with a
per-instruction break check; a program counter accessor; the `ea` variable is
initialized. Both CPUs of the Star Wars board are instances of this core.

## MAME (BSD-3-Clause)

The following are ports to plain C of code from MAME, https://www.mamedev.org,
used under the BSD-3-Clause license:

* `core/avg.c`: the Atari Analog Vector Generator state machine and the Star
  Wars color/intensity handling, from `src/devices/video/avgdvg.cpp`
  (copyright Mathis Rosenhauer, Eric Smith and the MAME team).
* `core/starwars.c`: the matrix processor, divider, memory map and I/O
  behavior, from `src/mame/atari/starwars.cpp` and `starwars_m.cpp`
  (copyright Steve Baines and Frank Palazzolo).
* `core/tms5220.c`: the TMS5220 synthesis algorithm from
  `src/devices/sound/tms5220.cpp` (copyright Frank Palazzolo, Aaron Giles,
  Jonathan Gevaryahu, Raphael Nabet, Couriersud, Michael Zapf) and the
  coefficient tables from `src/devices/sound/tms5110r.hxx`.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## POKEY

`core/pokey.c` is an original implementation written for this project. Its
approach, one divide-by-N counter per channel with polynomial noise sampled
from free-running counters, follows the design Ron Fries described for his
1996 POKEY sound emulator, but no code from it is used.

## Hardware drivers

The ST7789 display, QMI8658 IMU and ES8311 codec drivers under `components/`
come from PELLETINO, https://github.com/aedile/PELLETINO, by the same
author, and are covered by this project's 0BSD license.
