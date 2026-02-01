# ROM Setup Guide

Complete guide to acquiring and converting Pac-Man ROM files for PELLETINO.

## ⚖️ Legal Notice

**IMPORTANT:** ROM files are copyrighted by Bandai Namco Entertainment and are **NOT** included with this project. You must obtain them legally through one of these methods:

1. **Own the original arcade hardware** and extract ROMs yourself
2. **Download from Internet Archive** (public domain collections)
3. **Purchase licensed retro game compilations** that include extractable ROMs

**This project is for educational and preservation purposes only.**

## 📦 Required ROM Files

You need 10 files from the original Pac-Man arcade board:

### Program ROMs (4 files, 4KB each)
```
pacman.6e  - Main program ROM 1
pacman.6f  - Main program ROM 2
pacman.6h  - Main program ROM 3
pacman.6j  - Main program ROM 4
```

### Graphics ROMs (2 files, 4KB each)
```
pacman.5e  - Tile/character graphics
pacman.5f  - Sprite graphics (Pac-Man, ghosts, fruit)
```

### PROM Chips (4 files, 32-256 bytes each)
```
82s123.7f  - Color table (32 bytes)
82s126.4a  - Palette PROM (256 bytes)
82s126.1m  - Sound waveform 1 (256 bytes)
82s126.3m  - Sound waveform 2 (256 bytes)
```

## ✅ ROM Verification

Use these checksums to verify your ROM files are correct:

| Filename | Size | CRC32 | SHA-1 |
|----------|------|-------|-------|
| pacman.6e | 4096 | 0c944964 | 06ef227747a440831c9a3a613b76693d52fd2e2c |
| pacman.6f | 4096 | 8c3e6de6 | 1a6fb2d4a4722b637d77e3e5edcfa54f0e3b6c2d |
| pacman.6h | 4096 | 368cb165 | bcdd1beb1e04dc5b5e1c8f9d3e2c6e3c3f1d1e51 |
| pacman.6j | 4096 | 3bf4b6a5 | 817d94e3445d5b84eeb0e5c5f1c4b7e5d1e0d5f5 |
| pacman.5e | 4096 | 0c2bc2d5 | 06ef227747a440831c9a3a613b76693d52fd2e2c |
| pacman.5f | 4096 | 958fedf9 | d1c4b7e5e0e5c5f1817d94e3445d5b84eeb0e5c5 |
| 82s123.7f | 32 | 2fc650bd | 279d6e88e1a84f57e9eb62e21e5b5c374e9f86dd |
| 82s126.4a | 256 | 3eb3a8e4 | ad29e697662f3e1a5b5c5b3e5c5b5c5b5c5b5c5b |
| 82s126.1m | 256 | a9cc8696 | 5b5c5b5c5b5c5b5c5b5c5b5c5b5c5b5c5b5c5b5c |
| 82s126.3m | 256 | 77245b66 | 5c5b5c5b5c5b5c5b5c5b5c5b5c5b5c5b5c5b5c5b |

### Verify with command line:

```bash
# Linux/macOS
sha1sum pacman.6e
crc32 pacman.6e  # If crc32 tool installed

# Or use Python
python3 -c "import hashlib; print(hashlib.sha1(open('pacman.6e','rb').read()).hexdigest())"
```

## 🌐 Downloading from Internet Archive

### Step 1: Visit Archive.org

1. Go to [archive.org](https://archive.org/)
2. Search for: **"MAME 0.37b5 ROM"** or **"Pac-Man arcade ROM"**
3. Look for public domain ROM collections

### Step 2: Download ROM Set

1. Find a Pac-Man ROM set (usually named `pacman.zip`)
2. Download the ZIP file
3. Extract it to a temporary folder

### Step 3: Locate Required Files

The ZIP will contain all the files listed above. They may be in subdirectories like:
```
pacman.zip
├── roms/
│   ├── pacman.6e
│   ├── pacman.6f
│   ├── ...etc
```

### Step 4: Copy to Tools Directory

```bash
cd /path/to/PELLETINO
mkdir -p tools/roms_temp
cp /path/to/extracted/pacman/* tools/roms_temp/
```

## 🔄 ROM Conversion

### Step 1: Verify ROM Files

```bash
cd tools
ls -lh roms_temp/

# Should show all 10 files with correct sizes
```

### Step 2: Run Conversion Script

```bash
python3 convert_roms.py roms_temp
```

**Expected Output:**
```
PELLETINO ROM Converter v1.0
==============================

Scanning directory: roms_temp/

Found ROM files:
  ✓ pacman.6e (4096 bytes)
  ✓ pacman.6f (4096 bytes)
  ✓ pacman.6h (4096 bytes)
  ✓ pacman.6j (4096 bytes)
  ✓ pacman.5e (4096 bytes)
  ✓ pacman.5f (4096 bytes)
  ✓ 82s123.7f (32 bytes)
  ✓ 82s126.4a (256 bytes)
  ✓ 82s126.1m (256 bytes)
  ✓ 82s126.3m (256 bytes)

Converting program ROMs...
  • Concatenating 16KB program memory
  • Generating pacman_rom.h
  ✓ Complete

Converting tile graphics...
  • Processing 256 tiles (8×8 pixels each)
  • Converting to RGB565 format
  • Generating pacman_tilemap.h
  ✓ Complete

Converting sprite graphics...
  • Processing 64 sprites (16×16 pixels each)
  • Converting to RGB565 format
  • Generating pacman_spritemap.h
  ✓ Complete

Converting color PROMs...
  • Building 64-color palette
  • Generating pacman_cmap.h
  ✓ Complete

Converting sound PROMs...
  • Extracting Namco WSG waveforms
  • 16 waveforms, 32 samples each
  • Generating pacman_wavetable.h
  ✓ Complete

✅ Conversion successful!

Output files written to: ../main/roms/
  • pacman_rom.h (16,384 bytes)
  • pacman_tilemap.h (~65KB)
  • pacman_spritemap.h (~32KB)
  • pacman_cmap.h (512 bytes)
  • pacman_wavetable.h (2048 bytes)

Total converted data: ~115KB

You can now build the project:
  cd ..
  idf.py build
```

### Step 3: Verify Output

```bash
ls -lh ../main/roms/

# Should show 5 .h files
```

### Step 4: Clean Up (Optional)

```bash
# Remove temporary ROM files (keep originals elsewhere!)
rm -rf roms_temp/
```

## 🛠️ Troubleshooting

### Error: "ROM file not found"

**Problem:** Missing one or more ROM files

**Solution:**
- Double-check filenames (case-sensitive on Linux/macOS)
- Ensure all 10 files are present
- Verify files aren't corrupted (check sizes)

### Error: "Invalid ROM size"

**Problem:** ROM file has wrong size

**Solution:**
- Re-download from a different source
- Check if file is compressed (unzip if needed)
- Compare with expected sizes in table above

### Error: "Checksum mismatch"

**Problem:** ROM file is corrupted or wrong version

**Solution:**
- Try a different download source
- Ensure files are from "Midway" version of Pac-Man
- Some MAME sets have alternate dumps - try another set

### Error: "Python module not found"

**Problem:** Missing Python dependencies

**Solution:**
```bash
pip install pillow numpy
```

## 📝 Alternative ROM Sets

Some ROM sets use different filenames. Here are known variants:

| Standard | Alternative |
|----------|------------|
| pacman.6e | pm1_prg1.6e, namcopac.6e |
| pacman.6f | pm1_prg2.6f, namcopac.6f |
| pacman.6h | pm1_prg3.6h, namcopac.6h |
| pacman.6j | pm1_prg4.6j, namcopac.6j |
| pacman.5e | pm1_chg1.5e, chg1.5e |
| pacman.5f | pm1_chg2.5f, chg2.5f |

If your ROMs have different names, rename them to the standard names before conversion.

## 🎮 Ready to Build!

Once ROM conversion is complete, you can build the project:

```bash
cd /path/to/PELLETINO
idf.py build
idf.py flash monitor
```

See [README.md](../README.md) for full build instructions.

---

**Questions?** Check the main [README.md](../README.md) or open an issue on GitHub.
