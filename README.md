# Terminal Audio Visualizer - Platform-Specific Setup Guide

A professional terminal-based audio visualizer with support for multiple operating systems and audio backends.

**Current Status:**
- ✅ **Arch Linux** - Fully tested and stable (PulseAudio)
- ✅ **Debian/Ubuntu** - Newly added support

---

## What's Fixed/Improved

### 1. **New Debian/Ubuntu Support**
Added complete installation support for Debian-based distributions:
- `install_debian.sh` - Full dependency installation and build
- `uninstall_debian.sh` - Clean uninstall script
- Uses `apt-get` instead of `pacman` (Arch-specific)
- Includes all required development packages


**Issues Fixed:**
- Missing architecture detection for Apple Silicon
- Better Homebrew bootstrap error handling
- Improved audio setup documentation
- Cross-architecture compilation flags

### 2. **Enhanced CMakeLists.txt**
**Improvements:**
- ✅ Platform-specific source file selection (visualizer.cpp vs visualizer-windows.cpp)
- ✅ Distribution detection on Linux
- ✅ Proper WASAPI detection for Windows
- ✅ CoreAudio detection for macOS
- ✅ Better dependency error messages
- ✅ Cleaner build output with platform info

---

## Installation Instructions

### Arch Linux (Original - No Changes)
```bash
chmod +x install_linux.sh
./install_linux.sh
```

### Debian / Ubuntu (New)
```bash
chmod +x install_debian.sh
sudo ./install_debian.sh
```

**Supported versions:**
- Debian 11+
- Ubuntu 20.04 LTS+
- Linux Mint (based on Ubuntu)
- Pop!_OS
- Elementary OS


---

## Directory Structure

```
cli_visualizer/
├── visualizer.cpp                 # Linux(PulseAudio)
├── CMakeLists.txt                 # Enhanced build configuration
├── install_linux.sh               # Arch Linux installation
├── install_debian.sh              # Debian/Ubuntu installation (NEW)
├── uninstall_linux.sh             # Arch uninstall
└── uninstall_debian.sh            # Debian uninstall (NEW)
```

---

## Platform-Specific Features

### Audio Input

| Platform | Backend | Status |
|----------|---------|--------|
| Arch Linux | PulseAudio | ✅ Tested & verified |
| Debian/Ubuntu | PulseAudio | ✅ New (matches Arch) |

### Volume Control

| Platform | Method | Status |
|----------|--------|--------|
| Arch Linux | ALSA mixer | ✅ Verified |
| Debian/Ubuntu | ALSA mixer | ✅ Should work |

### UI Library

| Platform | Library | Notes |
|----------|---------|-------|
| Arch Linux | ncurses | ✅ Wide character support |
| Debian/Ubuntu | ncurses | ✅ Wide character support |

---

## Dependencies

### Arch Linux
- `base-devel` - Build tools
- `cmake` - Build system
- `fftw` - FFT library
- `libpulse` - PulseAudio
- `alsa-lib` - ALSA (optional)
- `ncurses` - Terminal UI

### Debian/Ubuntu
- `build-essential` - Build tools
- `cmake` - Build system
- `libfftw3-dev` - FFT library
- `libpulse-dev` - PulseAudio development
- `libasound2-dev` - ALSA development
- `libncurses-dev` - ncurses development
- `pkg-config` - Package configuration

---

## Configuration

Settings are auto-saved to:
- **Linux**: `~/.config/visualizer.conf`
  
### Keyboard Controls
```
1-5    Visualization styles (Filled, Mirror, Bounce, Classic, ClassicFill)
m      Toggle Mono/Stereo mode
t/T    Next/Previous theme
+/-    Adjust sensitivity
[/]    Peak fall speed
s      Peak dots on/off
b      Blur/trail effect
</> Volume control (platform-dependent)
v      VU meter display
r      Reset to defaults
q      Quit
```

---

## Troubleshooting

### "Dependencies not found"
**Linux**: Make sure your package manager is updated
```bash
# Arch
sudo pacman -Syu

# Debian/Ubuntu
sudo apt-get update
```

### "Audio not captured"
- **Linux**: Ensure PulseAudio is running
  ```bash
  pactl info
  ```

### "CMake configuration failed"
- Delete `build/` directory and retry
- Ensure all dependencies are installed
- Run cmake with verbose output:
  ```bash
  cmake .. -DCMAKE_BUILD_TYPE=Release --debug-output
  ```

## Building from Source Manually

If scripts don't work, you can build manually:

```bash
# Create and enter build directory
mkdir build && cd build

# Linux
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
sudo cmake --install .
---

## Uninstallation

### Arch Linux
```bash
./uninstall_linux.sh
```

### Debian/Ubuntu
```bash
sudo ./uninstall_debian.sh
```
## Testing Recommendations

### For Debian/Ubuntu Users
1. Test on clean install if possible
2. Verify all dependencies install correctly
3. Test audio capture with various PulseAudio configurations
4. Check ALSA mixer integration for volume control

---

## Version Information

- **Base Version**: 1.0.0
- **Build System**: CMake 3.15+
- **C++ Standard**: C++17
- **Arch Linux**: Verified working
- **Debian/Ubuntu**: New support added
---

## License & Credits

Created as a terminal audio visualization tool with cross-platform support.

**Key Libraries:**
- FFTW3 - Fast Fourier Transform
- PulseAudio - Audio capture
- ALSA - Optional volume control (Linux)
- ncurses/PDCurses - Terminal UI

---

## Support Notes

- **Arch Linux**: Primary development platform, fully tested
- **Debian/Ubuntu**: Uses same audio backend as Arch (PulseAudio), should work seamlessly

For issues on untested platforms, please review the specific installation scripts and error messages carefully.
