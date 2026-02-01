# Changelog

All notable changes to PELLETINO will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Comprehensive README with Mermaid diagrams
- ROM setup guide with legal acquisition instructions
- Contributing guidelines for developers
- Enhanced documentation structure
- CHANGELOG.md for tracking changes

### Changed
- Removed Ms. Pac-Man support (focused on Pac-Man only)
- Updated all references to remove TRENCHRUNNER mentions
- Improved .gitignore to exclude all ROM-related files
- Cleaned up AGENTS.md to remove user-specific paths
- Updated README with detailed build instructions
- Improved documentation formatting and clarity

### Removed
- `mspacman/` directory and all Ms. Pac-Man ROM headers
- Hard-coded user paths from documentation
- References to sister projects

## [0.1.0] - Initial Release

### Added
- Z80 CPU emulation core
- ST7789 display driver (240×280 resolution)
- ES8311 audio codec support
- Namco WSG wavetable synthesis
- GPIO button input handling
- QMI8658 IMU tilt control (optional)
- ROM conversion tools (Python scripts)
- Full Pac-Man arcade game logic
- DMA-accelerated SPI for smooth graphics
- I2S audio output with authentic sounds

### Hardware Support
- ESP32-C6 RISC-V @ 160MHz
- Waveshare ESP32-C6-LCD-1.69 development board
- 512KB SRAM, 4MB Flash
- 60 FPS gameplay performance

### Documentation
- Initial README with setup instructions
- Hardware requirements documentation
- Build system configuration (ESP-IDF)

---

**Note:** This is an educational project for arcade preservation and embedded systems learning. ROM files must be obtained legally and are not included with this software.
