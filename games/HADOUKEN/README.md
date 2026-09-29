# HADOUKEN

**A video clip on an ESP32-C6 Fiesta medal.** Not an emulator: Street Fighter
II is a 10 MHz 68000 driving five megabytes of graphics through hardware this
board cannot approach. So this plays its attract mode instead — an MJPEG + MP3
clip, letterboxed 4:3 on the portrait panel, on a loop — because some attract
modes are more attractive than others, and this one looks good on a lapel.

Anyone building their own medal can put whatever clip they like here.

---

## 🎮 Controls

| Control | What it does |
|---|---|
| **Both buttons together** | Sound off and on |
| **Middle button, hold 10 seconds** | Back to the PELLETINO menu |
| **Power button, hold 1 second** | Power off |

There is nothing to play. It loops.

---

## 🎬 Making a clip

The player wants a Motion-JPEG stream and an MP3 stream, packed into one file.
The panel is 240 wide, so a 4:3 clip is 240×180 and a 16:9 one 240×136 — set
`VIDEO_H` in `main/egg.cpp` to match, it is 180 as shipped.

```sh
# video: 240x180, 12 fps, JPEG quality 11 (lower is better and bigger)
ffmpeg -ss 12 -to 67 -i attract.mp4 \
    -vf "scale=240:180:flags=lanczos,fps=12" \
    -c:v mjpeg -q:v 11 -pix_fmt yuvj420p -an clip.mjpeg
# audio: mono, 22.05 kHz, 32 kbps is plenty for a 1-inch speaker
ffmpeg -ss 12 -to 67 -i attract.mp4 -vn -ac 1 -ar 22050 -c:a libmp3lame -b:a 32k clip.mp3
python3 tools/pack_media.py media.bin 12 clip.mjpeg clip.mp3
```

`pack_media.py` prints the frame count and the largest frame. The player holds
one frame at a time in a 16 KB buffer and indexes up to 1024 frames; at these
settings frames are around 6–8 KB. Roughly: **12 fps at 240×180 costs about
75 KB per second**, so a minute is 4.5 MB.

Under PELLETINO, drop the file at `games/HADOUKEN/media.bin` and set `data_kb`
in `games.toml` to its size rounded up — the launcher lays out a `media`
partition next to the game's slot and flashes the file into it. The file being
there is what puts the game in the build, the way a ROM zip does for the others.

---

## 🔨 Building on its own

```sh
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker build
cd build_docker && esptool --chip esp32c6 -p /dev/cu.usbmodemXXXX -b 460800 \
    write_flash @flash_args 0x90000 ../media.bin
```

The standalone `partitions.csv` gives the clip 4.5 MB at 0x90000.

---

## 🔬 How it works

The clip is read straight out of flash a frame at a time. Each JPEG is decoded
by the TJpgDec in the C6's ROM into sixteen-row strips that go to the panel as
they complete, so no whole frame is ever in RAM; the MP3 is decoded by
libhelix a frame ahead of the picture and resampled into the audio HAL's stream
ring. The frame clock is wall time against the packed frame rate, so a slow
frame is dropped rather than letting the picture drift from the sound.

The player was Star Wars' easter egg. It became its own thing when the medal
grew a menu.

## 📄 Legal

No video is included. Street Fighter II is © Capcom; the clip is yours to
supply and your responsibility. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
for the MP3 decoder's licence. Not affiliated with Capcom or the Fiesta San
Antonio Commission.

## 📜 License

[0BSD](LICENSE) for the project's own code; libhelix-mp3 is RPSL.
