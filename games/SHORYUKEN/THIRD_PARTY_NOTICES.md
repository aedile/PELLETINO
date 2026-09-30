# Third-party code

## Musashi, the 68000 (MIT)

`core/m68k/` is Musashi 4.10 by Karl Stenerud, from
https://github.com/kstenerud/Musashi (commit 313ebf1), used under the MIT
license:

> Copyright © 1998-2001 Karl Stenerud
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
> THE SOFTWARE.

It is modified. `core/m68k/CHANGES.md` lists every change and why it was made:
the opcode tables are constants worked out ahead of time rather than built in
RAM at start-up, the FPU and MMU are not built, and memory is reached through
inline functions. `gen/m68k_in.c` and `gen/m68kmake.c` are Musashi's own and
unmodified; `m68kops.c` is what `tools/gen_m68k.py` makes from them.

## Marat Fayzullin's Z80 emulator (non-commercial)

`core/z80/` is the portable Z80 emulator by Marat Fayzullin, copyright (C)
Marat Fayzullin 1994-2007, from http://fms.komkon.org/EMUL8/. Its terms, from
the source headers: "You are not allowed to distribute this software
commercially. Please, notify me, if you make any changes to this file." The
files are unmodified; the project builds them with `LSB_FIRST` and `EXECZ80`
defined. The skipping of the sound program's idle loop is done by not calling
the emulator, in `core/cps1_sound.c`, and changes nothing in it.

## ymfm, for the YM2151's tables (BSD-3-Clause)

`core/ym2151_tables.h` holds five tables copied from ymfm by Aaron Giles,
https://github.com/aaronsgiles/ymfm: the chip's sine and power ROMs, its
envelope increments, its detune table, and the phase steps measured from a
real chip by David Viens. `core/ym2151.c` is this project's own YM2151,
written with ymfm as the description of how the chip behaves; it shares no
code with it. ymfm is used under the BSD-3-Clause license:

> Copyright (c) 2021, Aaron Giles
> All rights reserved.
>
> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice,
>    this list of conditions and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above copyright notice,
>    this list of conditions and the following disclaimer in the documentation
>    and/or other materials provided with the distribution.
> 3. Neither the name of the copyright holder nor the names of its
>    contributors may be used to endorse or promote products derived from
>    this software without specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
> AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
> IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
> ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
> LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
> CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
> SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
> INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
> CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
> ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
> POSSIBILITY OF SUCH DAMAGE.

MAME's older YM2151, by Jarek Burczynski, is GPL. It was not used, and was not
read.

## MAME (BSD-3-Clause)

The machine in `core/cps1.c` (the memory map, the input ports and DIP
switches, the vertical blank interrupt and how it is acknowledged, the sound
latches), the video in `core/cps1_video.c` (the CPS-A and CPS-B registers, the
CPS-B-11 chip's register positions and layer enable bits, the STF29 PAL's
mapping of tile codes to ROMs, the tile and sprite formats, the palette format
and how it is copied, row scroll, the priority masks), the sound section in
`core/cps1_sound.c` (the Z80's memory map, the clocks, the mixing levels) and
`core/okim6295.c` (the command protocol, the phrase table, the volume table)
are written from MAME's `src/mame/capcom/cps1.cpp`, `cps1_v.cpp` and `cps1.h`
and `src/devices/sound/okim6295.cpp` and `okiadpcm.cpp`. The table of ROM sets
and CPS-B chips in `tools/convert_roms.py` is from the same place. Copyright
Paul Leaman, Mirko Buffoni, Aaron Giles, Andrew Gardner and the MAME team; used
under the BSD-3-Clause license. No function is ported: the files were written
from the driver as a description of the hardware, and share no line with it.

## ROMs

No game ROMs are included in this repository, and none ever will be. Supplying
them is up to whoever builds the thing.
