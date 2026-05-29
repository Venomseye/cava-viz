# cava-viz

A terminal audio visualizer with 24-bit truecolor gradients, stereo mirror
layout, and live config reload.  Built on [CAVA](https://github.com/karlstav/cava)'s
FFT pipeline with a custom ncurses renderer.

```
 Fire ████████████████████████████████████████████████████████████
      █   █  ██ █ █   █ █ ██ █   █ ███  █    █ ██  █   █  █ ██  █
```

## Features

- **12 themes** — Fire, Plasma, Neon, Teal, Sunset, Candy, Aurora, Inferno,
  White, Rose, Mermaid, Vapor
- **24-bit truecolor** on Konsole, GNOME Terminal, kitty, Alacritty, iTerm2,
  Windows Terminal, and any terminal that sets `COLORTERM=truecolor`
- **Stereo mirror** layout — left and right channels mirrored from centre
- **Sub-cell precision** using Unicode eighth-block characters (▁▂▃▄▅▆▇█)
- **Live config reload** — edit `~/.config/cava-viz/config` while running;
  changes apply instantly via inotify without restarting
- **PipeWire and PulseAudio** backends with automatic detection
- **Auto-sensitivity** — tracks loudest bar and keeps peaks near 90%

## Requirements

| Package              | Debian/Ubuntu           | Arch              |
|----------------------|-------------------------|-------------------|
| C++17 compiler       | `build-essential`       | `base-devel`      |
| CMake ≥ 3.16         | `cmake`                 | `cmake`           |
| ncursesw             | `libncursesw5-dev`      | `ncurses`         |
| FFTW3                | `libfftw3-dev`          | `fftw`            |
| PipeWire *(optional)*| `libpipewire-0.3-dev`   | `pipewire`        |
| PulseAudio *(optional)*| `libpulse-dev`        | `libpulse`        |

At least one audio backend is required.

## Build

```bash
# Clone
git clone https://github.com/yourname/cava-viz
cd cava-viz

# Configure (both backends, release build)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run directly
./build/viz

# Or install to ~/.local/bin
cmake --install build --prefix ~/.local
```

### Build options

```bash
# Disable a backend
cmake -B build -DENABLE_PIPEWIRE=OFF
cmake -B build -DENABLE_PULSEAUDIO=OFF

# Debug build (ASan + UBSan enabled)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

## Usage

```
viz [options]

  -b <backend>   Backend: auto | pipewire | pulseaudio  (default: auto)
  -s <source>    Device or monitor source name
  -m             Capture microphone instead of loopback
  -w             Auto bar width based on terminal columns
  -h             Show help
```

**Examples**

```bash
# Auto-detect backend and source (most common)
viz

# Force PipeWire, specific monitor
viz -b pipewire -s "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor"

# Microphone input
viz -m

# Auto-size bars for current terminal
viz -w
```

## Keybindings

| Key       | Action                                      |
|-----------|---------------------------------------------|
| `t`       | Next theme                                  |
| `T`       | Previous theme                              |
| `+` / `=` | Increase sensitivity                        |
| `-`       | Decrease sensitivity                        |
| `a`       | Toggle auto-sensitivity                     |
| `[`       | Decrease bar width (1–8)                    |
| `]`       | Increase bar width (1–8)                    |
| `g`       | Cycle gap width (0 → 1 → 2 → 0)            |
| `c`       | Toggle colour cycle (hue rotates over time) |
| `b`       | Toggle per-bar colour (bass→treble mapping) |
| `h`       | Toggle HUD pin (always visible)             |
| `q` / `ESC` | Quit                                      |

## Config file

Location: `~/.config/cava-viz/config`
Created automatically on first run with all defaults documented inline.

```ini
# ── Visual ────────────────────────────────────────────────────────
# 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy
# 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor
theme          = 0
bar_width      = 2
gap_width      = 1
hud_pinned     = 0

# ── Rendering modes ───────────────────────────────────────────────
colour_cycle   = 0
per_bar_colour = 0

# ── Audio ─────────────────────────────────────────────────────────
stereo         = 1
high_cutoff    = 20000

# ── FFT / Smoothing ───────────────────────────────────────────────
gravity        = 1.00   # fall speed: 0.1 (slow) – 5.0 (instant)
monstercat     = 1.50   # bar spread: 0.0 (off) – 5.0
rise_factor    = 0.30   # attack: 0.0 (instant) – 0.95 (very slow)
bass_smooth    = 0.00   # extra bass smoothing: 0.0 – 1.0

# ── Audio processing ──────────────────────────────────────────────
a_weighting    = 0      # IEC 61672 perceptual weighting
noise_gate     = 0.020  # snap to zero below this level
auto_mono      = 0

# ── Sensitivity ───────────────────────────────────────────────────
sensitivity    = 1.50
auto_sens      = 1

# ── Performance ───────────────────────────────────────────────────
fps            = 60
```

All changes to this file are picked up **instantly** while `viz` is running.
No restart needed.

## Themes

| # | Name    | Arc                                       |
|---|---------|-------------------------------------------|
| 0 | Fire    | vivid-orange → red → hot-pink             |
| 1 | Plasma  | blue-violet → electric-blue → neon-cyan   |
| 2 | Neon    | electric-magenta → violet → electric-blue |
| 3 | Teal    | dark-teal → teal → bright-aqua            |
| 4 | Sunset  | amber → orange-red → crimson → violet     |
| 5 | Candy   | hot-rose → magenta → violet               |
| 6 | Aurora  | sea-green → teal → sky-blue → soft-violet |
| 7 | Inferno | blood-red → orange → amber → bright-gold  |
| 8 | White   | solid bright white                        |
| 9 | Rose    | crimson-rose → coral → salmon → orchid    |
|10 | Mermaid | ocean-blue → teal → periwinkle → orchid   |
|11 | Vapor   | neon-purple → violet-magenta → hot-pink   |

## Troubleshooting

**Bars show the wrong colours on Konsole / GNOME Terminal**

The app automatically sets `TERM=xterm-direct` before ncurses starts so that
`init_color()` works on terminals running under `TERM=xterm-256color`.  If
colours still look wrong, check that `xterm-direct` is in your terminfo
database:

```bash
infocmp xterm-direct 2>&1 | head -2
```

If it is missing, install a newer ncurses: `sudo apt install libncurses6` or
`sudo pacman -S ncurses`.

**No audio / bars stay at zero**

```bash
# List available sources
pactl list short sources | grep monitor

# Pass the source explicitly
viz -s "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor"
```

**Build fails — no backend found**

At least one of `libpipewire-0.3-dev` or `libpulse-dev` must be installed:

```bash
# Ubuntu / Debian
sudo apt install libpipewire-0.3-dev libspa-0.2-dev

# Arch
sudo pacman -S pipewire
```

## License

MIT
