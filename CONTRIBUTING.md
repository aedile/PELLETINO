# Contributing to PELLETINO

Thank you for your interest in contributing to PELLETINO! This document provides guidelines for contributing to the project.

## 🎯 Project Goals

PELLETINO aims to provide:
- Authentic Pac-Man arcade experience on ESP32-C6
- Educational resource for embedded emulation techniques
- Clean, well-documented codebase for learning

## 📋 Getting Started

1. **Read the Documentation**
   - [README.md](README.md) - Project overview and build instructions
   - [docs/ROM_SETUP.md](docs/ROM_SETUP.md) - ROM acquisition and conversion
   - [AGENTS.md](AGENTS.md) - Quick reference for development

2. **Set Up Development Environment**
   ```bash
   # Install ESP-IDF 5.x
   # See: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/

   # Clone the repository
   git clone https://github.com/yourusername/PELLETINO.git
   cd PELLETINO

   # Build the project
   idf.py set-target esp32c6
   idf.py build
   ```

3. **Understand the Architecture**
   - Review the Mermaid diagrams in README.md
   - Read component headers for API documentation
   - Study the main game loop in `main/main.c`

## 🔧 Development Workflow

### Branch Strategy

- `main` - Stable releases only
- `develop` - Integration branch
- `feature/your-feature` - Feature branches
- `bugfix/issue-number` - Bug fixes

### Making Changes

1. **Create a Feature Branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make Your Changes**
   - Follow the coding style (see below)
   - Add comments for complex logic
   - Update documentation if needed

3. **Test Thoroughly**
   ```bash
   # Clean build
   idf.py fullclean build

   # Flash and test
   idf.py flash monitor

   # Test all features:
   # - Gameplay (movement, eating dots, ghosts)
   # - Sound (all sound effects)
   # - Graphics (no glitches, smooth 60 FPS)
   # - Input (buttons, IMU if enabled)
   ```

4. **Commit Your Changes**
   ```bash
   git add .
   git commit -m "feat: Add awesome new feature"
   ```

5. **Push and Create PR**
   ```bash
   git push origin feature/your-feature-name
   # Then create Pull Request on GitHub
   ```

## 📝 Coding Standards

### C/C++ Style

```c
// Use descriptive names
void render_sprites(sprite_t *sprites, int count);

// Constants in UPPER_CASE
#define MAX_SPRITES 64
#define SCREEN_WIDTH 240

// Functions: lowercase with underscores
int calculate_score(int dots, int ghosts);

// Structs: lowercase with _t suffix
typedef struct {
    int x, y;
    uint8_t color;
} sprite_t;

// Comments for non-obvious code
// Calculate sprite position using Pac-Man's original addressing scheme
int sprite_pos = (y << 8) | x;
```

### File Organization

```
component_name/
├── include/
│   └── component_name.h      # Public API
├── src/
│   ├── component_name.c      # Implementation
│   └── component_internal.h  # Private headers
└── CMakeLists.txt            # Component build config
```

### Documentation

- Add header comments to all public functions
- Use Doxygen-style comments where appropriate
- Update README.md for user-visible changes
- Add inline comments for complex algorithms

Example:
```c
/**
 * @brief Render a sprite to the display buffer
 * 
 * @param sprite Pointer to sprite data
 * @param x X coordinate (0-239)
 * @param y Y coordinate (0-279)
 * @param flip_x Flip sprite horizontally
 * @param flip_y Flip sprite vertically
 */
void render_sprite(const sprite_t *sprite, int x, int y, bool flip_x, bool flip_y);
```

## 🐛 Reporting Bugs

### Before Submitting

1. Check existing issues
2. Verify it's not a ROM issue (try different ROM source)
3. Test with latest `main` branch
4. Gather debug information

### Bug Report Template

```markdown
**Description:**
Clear description of the bug

**Steps to Reproduce:**
1. Flash firmware
2. Start game
3. Press button X
4. Observe incorrect behavior

**Expected Behavior:**
What should happen

**Actual Behavior:**
What actually happens

**Environment:**
- ESP-IDF Version: 5.3.1
- Board: ESP32-C6-DevKit
- ROM Set: Archive.org MAME 0.37b5

**Logs:**
```
Paste relevant serial monitor output
```

**Screenshots/Video:**
If applicable
```

## ✨ Feature Requests

We welcome feature ideas! Please open an issue with:

1. **Use Case:** What problem does it solve?
2. **Proposed Solution:** How should it work?
3. **Alternatives:** Other ways to achieve the goal?
4. **Priority:** Nice-to-have or critical?

## 🧪 Testing Guidelines

### Manual Testing Checklist

- [ ] Game boots and runs
- [ ] All 4 ghosts behave correctly
- [ ] Pac-Man movement (all 4 directions)
- [ ] Dot eating and scoring
- [ ] Power pellets work
- [ ] Ghosts turn blue and can be eaten
- [ ] Fruit appears and can be collected
- [ ] Death animation plays
- [ ] Level transitions correctly
- [ ] All sound effects play
- [ ] No graphical glitches
- [ ] Runs at stable 60 FPS

### Performance Testing

```bash
# Monitor FPS and memory
idf.py monitor

# Look for logs like:
# I (1234) PELLETINO: FPS: 60.2, Free heap: 256KB
```

## 📜 Legal Considerations

### ROM Files

- **NEVER** commit ROM files to the repository
- **NEVER** include ROM data in pull requests
- Only generated header files (already in .gitignore)
- See [docs/ROM_SETUP.md](docs/ROM_SETUP.md) for legal acquisition

### Code Licensing

- Ensure your contributions are your own work
- Credit third-party code appropriately
- Follow existing licenses for modified code

### Attribution

If using code from other projects:
```c
// Based on implementation from XYZ project
// Original author: John Doe
// License: MIT
// Modified for PELLETINO
```

## 🎓 Learning Resources

### ESP-IDF Documentation
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [FreeRTOS Kernel](https://www.freertos.org/Documentation/RTOS_book.html)

### Z80 Emulation
- [Z80 CPU User Manual](http://www.zilog.com/docs/z80/um0080.pdf)
- [Pac-Man Hardware](http://www.lomont.org/Software/Games/PacMan/PacmanEmulation.pdf)

### Graphics Programming
- [ST7789 Datasheet](https://www.rhydolabz.com/documents/33/ST7789.pdf)
- [Tile-based rendering](https://en.wikipedia.org/wiki/Tile-based_video_game)

## 💬 Communication

- **Issues:** Bug reports and feature requests
- **Pull Requests:** Code contributions
- **Discussions:** General questions and ideas

## 🙏 Recognition

Contributors will be added to the README.md credits section.

Thank you for contributing to PELLETINO! 🎮
