# cava-viz

**cava-viz** is a high-performance terminal audio visualizer written in C++, based on the CAVA algorithm.

It renders a smooth, real-time stereo spectrum with gradient colors, sub-cell precision, and clean, centered output.

---

## Preview

> Themes: Fire · Plasma · Neon · Teal · Sunset · Candy · Aurora · Inferno · White · Rose · Mermaid · Vapor

<!-- Add your GIF/screenshot here -->

---

## Features

- **CAVA-faithful FFT**
  - Dual bass/mid FFT
  - Log-distributed frequency bands
  - Per-band EQ
  - Monstercat smoothing
  - Auto sensitivity (overshoot-based)

- **Smooth rendering**
  - Unicode bars (`▁▂▃▄▅▆▇█`)
  - Sub-cell vertical precision
  - No jitter, no flicker

- **12 gradient themes**
  - Per-row RGB interpolation
  - Clean transitions without banding

- **Stereo mirror layout**
  - Bass centered, treble expands outward
  - Perfect symmetry

- **Live config reload (Linux)**

- **Robust audio**
  - PipeWire + PulseAudio
  - Auto-detect + fallback
  - Auto-reconnect watchdog

---

## Install (Recommended)

```bash
git clone https://github.com/venomseye/cava-viz
cd cava-viz
chmod +x install.sh uninstall.sh
./install.sh
````

The installer will:

* Detect your distro (**Arch / Debian / Fedora**)
* Install missing dependencies
* Build using **Ninja** (if available)
* Install the binary to:

```
/usr/local/bin/viz
```

---

### Custom install location

```bash
INSTALL_PREFIX=$HOME/.local ./install
```

Then ensure:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

---

## Uninstall

```bash
./uninstall
```

* Removes the installed binary (`viz`)
* Keeps your config intact

---

## Manual Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

---

## Usage

```
viz [OPTIONS]

Options:
  -b <pulse|pipewire|auto>   Audio backend (default: auto)
  -s <source>                Explicit source device
  -M                         Use microphone input
  -r <Hz>                    Sample rate (default: 44100)
  -t <0-11>                  Theme index
  -f <n>                     Target FPS (default: 60)
  -w                         Auto bar width
  -h                         Show help
```

---

## Keybindings

| Key       | Action                  |
| --------- | ----------------------- |
| `q`       | Quit                    |
| `t`       | Next theme              |
| `g`       | Cycle gap               |
| `]` / `[` | Adjust bar width        |
| `↑` / `↓` | Adjust sensitivity      |
| `a`       | Toggle auto sensitivity |

---

## Themes

Fire · Plasma · Neon · Teal · Sunset · Candy · Aurora · Inferno · White · Rose · Mermaid · Vapor

---

## Config

Config file:

```
~/.config/viz/config
```

Example:

```ini
theme       = 0
bar_width   = 2
gap_width   = 1
sensitivity = 1.50
auto_sens   = 1
stereo      = 1
last_source = auto
```

* All fields optional
* Live reload supported (Linux)

---

## Dependencies

* `cmake`
* `pkg-config`
* `fftw3`
* `ncursesw`
* `pipewire` *(optional)*
* `pulseaudio` *(optional)*

Handled automatically by `./install` on supported systems.

---

## Architecture

```
main.cpp              — event loop, input, reload, watchdog
renderer.*            — ncurses drawing, gradients, layout
fft_processor.*       — FFT + smoothing + auto-sens
config.*              — config load/save
audio_capture.*       — abstraction layer
pulse_capture.*       — PulseAudio backend
pipewire_capture.*    — PipeWire backend
```

---
