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
