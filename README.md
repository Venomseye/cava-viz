# cava-viz

A terminal audio visualizer built in C++ using the [CAVA](https://github.com/karlstav/cava) algorithm. Displays a real-time stereo mirror spectrum with smooth per-row color gradients, sub-cell Unicode bar rendering, and live config reload.

![themes: Fire, Plasma, Neon, Teal, Sunset, Candy, Aurora, Inferno, White, Rose, Mermaid, Vapor]

---

## Features

- **CAVA-faithful FFT** — dual bass/mid FFT, log-distributed frequency bars, per-bar EQ, Monstercat smoothing, and auto-sensitivity (overshoot-based)
- **12 color themes** — smooth RGB gradients sampled per terminal row, no color banding
- **Stereo mirror layout** — left channel mirrors right, bass bars in the center, treble outward, always pixel-perfectly centered
- **Sub-cell precision** — 8 Unicode block characters (▁▂▃▄▅▆▇█) per terminal row for smooth bar heights
- **Live config reload** — edit the config file while running; changes apply instantly (Linux)
- **PipeWire + PulseAudio** — auto-detects system audio loopback; falls back gracefully
- **Silent watchdog** — reconnects automatically if the audio source changes or dies

---

## Requirements

| Dependency | Notes |
|---|---|
| `libncurses` | Terminal rendering |
| `libfftw3` | FFT processing |
| `libpipewire` | PipeWire audio (optional) |
| `libpulse` | PulseAudio audio (optional) |
| C++17 compiler | `g++` or `clang++` |
| CMake ≥ 3.16 | Build system |

---

## Build

```bash
git clone https://github.com/yourname/cava-viz
cd cava-viz
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Install system-wide:

```bash
sudo make install
```

Or run directly from the build directory:

```bash
./cava-viz
```

---

## Usage

```
cava-viz [OPTIONS]

Options:
  -b <pulse|pipewire|auto>   Audio backend (default: auto)
  -s <source>                Explicit source device name
  -M                         Use microphone instead of system audio
  -r <Hz>                    Sample rate (default: 44100)
  -t <0-11>                  Initial theme index
  -f <n>                     Target FPS (default: 60)
  -w                         Force auto bar width based on terminal width
  -h                         Show help
```

---

## Keys

| Key | Action |
|-----|--------|
| `q` | Quit |
| `t` | Cycle to next theme |
| `g` | Cycle gap between bars (0 → 1 → 2 → 0) |
| `]` | Increase bar width |
| `[` | Decrease bar width |
| `↑` / `↓` | Increase / decrease sensitivity (disables auto-sens) |
| `a` | Toggle auto-sensitivity |

---

## Themes

| # | Name | Description |
|---|------|-------------|
| 0 | Fire | Dark amber → orange → red → electric pink |
| 1 | Plasma | Deep crimson → violet → blue → cyan |
| 2 | Neon | Deep indigo → violet → magenta → pale pink |
| 3 | Teal | Midnight → teal → vivid cyan |
| 4 | Sunset | Deep violet → scarlet → gold |
| 5 | Candy | Hot rose → vivid purple → sky blue |
| 6 | Aurora | Forest green → teal → deep violet |
| 7 | Inferno | Near black → red → orange → pale gold |
| 8 | White | Charcoal → white |
| 9 | Rose | Deep wine → rose → blush |
| 10 | Mermaid | Deep ocean → aqua teal → purple coral |
| 11 | Vapor | Deep navy → magenta → hot pink → cyan |

---

## Config

Settings are saved automatically on exit to:

```
~/.config/cava-viz/config
```

Example config file:

```ini
# cava-viz
theme       = 0
bar_width   = 2
gap_width   = 1
sensitivity = 1.50
auto_sens   = 1
stereo      = 1
last_source = alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
```

All fields are optional — missing keys fall back to defaults. On Linux, editing and saving this file while cava-viz is running applies changes instantly without restarting.

---

## Architecture

```
main.cpp              — event loop, input handling, audio watchdog, inotify reload
renderer.h/cpp        — ncurses drawing: gradient colors, bar columns, HUD, centering
fft_processor.h/cpp   — CAVA algorithm port: dual FFT, EQ, smoothing, auto-sens
config.h/cpp          — flat key=value config load/save
audio_capture.h       — abstract audio capture interface
pulse_capture.h/cpp   — PulseAudio backend
pipewire_capture.h/cpp— PipeWire backend
```

---

## License

MIT
