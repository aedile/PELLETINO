# Splash music

Put a Standard MIDI File here as `splash.mid` and it is embedded in the
firmware on the next `./pelletino build`. Without one the splash runs silent —
the build does not fail.

```sh
cp ~/Downloads/something.mid music/splash.mid
./pelletino build && ./pelletino flash
```

**Nothing in this directory is committed** (`.gitignore` excludes it). We ship no
music for the same reason we ship no ROMs and no marquee art: it belongs to
whoever wrote it. Sourcing a file you are entitled to use is your part.

If you want something unencumbered, traditional and folk melodies are in the
public domain as compositions — a *Jarabe Tapatío* or similar suits a Fiesta
medal and carries no rights problem. Plenty of freely-licensed chiptune exists
too.

## What plays well

Three tone channels, so at most three notes sound at once and a fourth steals
the quietest voice. Dense orchestral files turn to mush; something written for
a chip, or a simple lead-plus-accompaniment arrangement, sounds right. Format 0
and format 1 files both work. Percussion (MIDI channel 10) is dropped.
