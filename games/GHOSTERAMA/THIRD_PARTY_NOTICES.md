# Third-party code

## Marat Fayzullin's Z80 emulator (non-commercial)

`components/z80_cpu/src/Z80.c`, `Z80.h`, `Codes.h`, `CodesCB.h`, `CodesED.h`,
`CodesXCB.h`, `CodesXX.h` and `Tables.h` are the portable Z80 emulator by Marat
Fayzullin, copyright (C) Marat Fayzullin 1994-2007, from
http://fms.komkon.org/EMUL8/. Its terms, from the source headers: "You are not
allowed to distribute this software commercially. Please, notify me, if you make
any changes to this file." The files are unmodified, and the same copy as the
other Z80 games here carry in `core/z80/`; the project builds them with
`LSB_FIRST` defined. Our own wrapper is `components/z80_cpu/src/z80_cpu.c`.

A GHOSTERAMA image embeds the emulator, so it may be shared but not sold.

## Everything else

`components/pacman_hw/` (the Pac-Man machine model, video and input) and the
rest of `main/` and `components/` were written for this project and are 0BSD,
like the launcher. They cite no third-party source.

See `../../LICENSING.md` for how this fits the bundle as a whole.
