# Third-party code

## vecx 6809 core (GPL-3.0)

`core/e6809.c` and `core/e6809.h` are the MC6809 emulator from vecx, the
Vectrex emulator by Valavan Manohararajah, as maintained at
https://github.com/jhawthorn/vecx. vecx is distributed under the GNU General
Public License version 3; the full text is in `LICENSES/GPL-3.0.txt`. The
copy here is TRENCHRUNNER's, with its modifications: bus accessors and the
interrupt line can be supplied as macros by the including file so they inline,
and there is a program counter accessor.

## MAME (BSD-3-Clause)

The machine model in `core/joust.c` (memory map, the ROM bank over the video
RAM, the PIAs and their interrupt wiring, the scanline interrupts, the blitter
and its timing), the video in `core/joust_video.c` (bitmap layout, palette
resistor weights) and the sound board in `core/joust_sound.c` (its map and
clock) are written from MAME's `src/mame/williams/williams.cpp`,
`williams_m.cpp`, `williams_v.cpp` and `williamsblitter.cpp`. Copyright Aaron
Giles, Michael Soderstrom, Marc LaFontaine, Sean Riddle and the MAME team; used
under the BSD-3-Clause license. `core/m6800.h` is this project's own 6800.

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

## ROMs

No game ROMs are included in this repository, and none ever will be. Supplying
them is up to whoever builds the thing.
