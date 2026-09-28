# Music

The launcher plays one tune, the `splash` one, through attract mode, the menu and
the credits. A second slot, `credits`, is optional: fill it and the Credits entry
in the menu plays that instead.
**Neither ships with the project.** Music belongs to whoever wrote it, so this
directory is empty in the repository and everything you put in it stays on your
machine — `.gitignore` excludes all of it except this file and the example.

With no music the screens run silent. The build does not fail.

## Adding a tune

```sh
tools/add_music.py splash  ~/Downloads/some-game.nsf  3
tools/add_music.py credits ~/Downloads/something.mid
./pelletino build && ./pelletino flash
```

The first argument is the slot, `splash` or `credits`. The second is a file, or a
URL, that you supply: an `.nsf`, a `.mid`, or a `.zip` / `.7z` holding one. The
number is the track, for NSF files, which usually hold a game's whole soundtrack.

`tools/add_music.py splash --remove` takes a tune back out.

## Then say who wrote it

Copy `credits.example.txt` to `credits.txt` and fill it in. Whatever is in that
file is shown under **MUSIC** in the credits roll on the device, so the composer
is named on the device that is playing their work.

Twenty-six characters to a line. Start a line with `~` to set it dimmer.

## NSF or MIDI

| | NSF | MIDI |
|---|---|---|
| Played on | an emulated NES sound chip (2A03) | an emulated AY-3-8910 |
| Voices | two pulse, triangle, noise, samples | three square waves |
| Sounds like | the console it came from | a reduction of whatever you gave it |
| Memory | 10 KB | roughly ten times the file |

**NSF is the better of the two by a distance.** It is the game's own music driver
running on the chip it was written for, so it sounds the way it did. A MIDI file
has to be squeezed onto three square waves: something written for a chip survives
that, dense piano or orchestral music does not. Percussion (MIDI channel 10) is
dropped.

NSF files that use an expansion sound chip — VRC6, FDS, N163 and the like — still
play, without those channels.

## Choosing a track

An NSF's tracks are numbered from 1 and are not labelled, so finding the one you
want means listening. This renders thirty seconds of a track to a WAV file on
your own machine:

```sh
host/music/run.sh music/splash.nsf 5 /tmp/track5.wav && afplay /tmp/track5.wav
```

The number lives in `music/splash.track` (or `credits.track`); edit it, or run
`add_music.py` again.

## The same test, as a test

`host/music/run.sh <file> [track]` with no output file runs three checks on the
audio itself: that it makes sound, that the sound changes from moment to moment,
and that it stops when told to. A stuck note fails the second.
