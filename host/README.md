# host — see the wheel, and hear the music, without the hardware

## The wheel

`preview` compiles the launcher's real menu on a desktop and writes what the
panel would show, at the real 240×280, using the real artwork blob.

```sh
cd host && make
./preview ../lcd/marquees.bin /tmp/wheel
```

One PPM per game at rest, a step of the wheel frame by frame, and the states the
menu has (not installed, mid-hold, muted with a flat battery, launching, a
message).

Nothing here reimplements the layout: `main/menu.cpp`, `components/fest` and
`components/mqart` are compiled as they are, and the only things stubbed are the
flash partition (read from the blob file), the panel (collected into an image),
the buttons and the battery. If it looks wrong here, it looks wrong on the
device.

It is also the check on the artwork reader. Add `--scramble` and every
picture's sizes, offsets and pixels are corrupted before the launcher sees
them; it is built with the address sanitiser and has to get through that
without reading out of bounds.

```sh
./preview ../lcd/marquees.bin /tmp/wheel_bad --scramble
```

## The music

`music/run.sh <file> [track] [out.wav]` plays an NSF or MIDI file through the
launcher's own player and checks that what comes out is music; `music/audition.sh`
writes a file's tracks out as WAVs so you can choose one.

`music/sfx.sh [dir]` does the same for the sound effects: that each starts at
once, is as bright as it should be, dies away and ends, clips rather than wraps
over loud music, and leaves the music untouched afterwards. Give it a directory
and it writes each effect there as a WAV.

## High scores

`hiscore/run.sh` tests the score keeper against a machine that is forty bytes of
memory: that nothing is restored or saved before a game has set its table up,
that what was saved comes back, that a score being run up is not written to
flash on every point, and that leaving for the menu saves at once.

Each game's own harness (`games/<game>/host/harness`) keeps scores too, in a
file instead of flash, so a game can be checked from one run to the next:

```sh
cd games/GIRDER/host && make
PELLETINO_SCORES=/tmp/dk.bin PELLETINO_POKE="20:60b9=45,60ba=12" ./harness /tmp/dk 40
PELLETINO_SCORES=/tmp/dk.bin ./harness /tmp/dk 40      # "restored", and reads 124550
```

| Variable | Does |
|---|---|
| `PELLETINO_SCORES=<file>` | keep scores in this file between runs |
| `PELLETINO_POKE="20:60b9=45,..."` | at 20 seconds, write these bytes (hex), to stand in for a score |
| `PELLETINO_FIND=12000` | at the end, list everywhere in memory that could be that number |
| `PELLETINO_WATCH=4030` | print that byte whenever it changes |
