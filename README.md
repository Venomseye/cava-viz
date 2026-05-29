# viz — Terminal Audio Visualizer

A real-time terminal audio visualizer built on the [CAVA](https://github.com/karlstav/cava) FFT algorithm.
Renders stereo frequency bars with 24-bit truecolor gradients, sub-cell Unicode precision,
and live config reload — no restart needed to change settings.

```
  Fire  █▆▄▃▂▁ ▂▄▇█▅▃ ▁▃▅▇█▆▄▂ ▁▂▄▆████▆▄▂▁
        ████████████████████████████████████████
```

---

## Features

- **12 themes** with perceptual HSV-arc gradients — Fire, Plasma, Neon, Teal, Sunset, Candy,
  Aurora, Inferno, White, Rose, Mermaid, Vapor
- **24-bit truecolor** on Konsole, GNOME Terminal, kitty, Alacritty, iTerm2, Windows Terminal
  and anything that sets `COLORTERM=truecolor`
- **Stereo mirror layout** — left and right channels mirrored from the centre
- **Sub-cell precision** using Unicode eighth-block characters (▁▂▃▄▅▆▇█) for smooth bar tips
- **Dual FFT** — separate bass and mid/treble transforms with log-distributed frequency bins
- **Live config reload** — edit `~/.config/cava-viz/config` while running; inotify picks up
  changes instantly with no restart
- **PipeWire and PulseAudio** backends with automatic detection and reconnect watchdog
- **Auto-sensitivity** — overshoot-based feedback keeps peaks near 90% without clipping
- **A-weighting** — IEC 61672 perceptual frequency weighting (optional)
- **Auto-mono** — collapses stereo to mono when L/R correlation is sustained above 97%

---

## Requirements

| Library | Debian / Ubuntu | Arch | Fedora |
|---|---|---|---|
| C++17 compiler | `build-essential` | `base-devel` | `gcc-c++` |
| CMake ≥ 3.16 | `cmake` | `cmake` | `cmake` |
| ncursesw | `libncursesw5-dev` | `ncurses` | `ncurses-devel` |
| FFTW3 | `libfftw3-dev` | `fftw` | `fftw-devel` |
| PipeWire *(optional)* | `libpipewire-0.3-dev` | `pipewire` | `pipewire-devel` |
| PulseAudio *(optional)* | `libpulse-dev` | `libpulse` | `pulseaudio-libs-devel` |

At least one audio backend is required.

---

## Install

### One-command install (recommended)

Detects your distro, installs dependencies, builds, and installs to `/usr/local/bin/viz`:

```bash
git clone https://github.com/venomseye/cava-viz
cd cava-viz
./install.sh
```

Install to a custom prefix (no `sudo` needed for `~/.local`):

```bash
INSTALL_PREFIX=~/.local ./install.sh
```

### Manual build

```bash
git clone https://github.com/venomseye/cava-viz
cd cava-viz

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run directly without installing
./build/viz

# Or install
sudo cmake --install build
```

### Build options

```bash
# Disable a backend
cmake -B build -DENABLE_PIPEWIRE=OFF
cmake -B build -DENABLE_PULSEAUDIO=OFF

# Debug build — enables AddressSanitizer and UBSan
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

### Uninstall

```bash
./uninstall.sh
```

Prompts before removing saved config and state files.

---

## Usage

```
viz [options]

  -b <backend>   Audio backend: auto | pipewire | pulse  (default: auto)
  -s <source>    Explicit audio source / device name
  -M             Capture microphone instead of loopback monitor
  -r <Hz>        Sample rate (default: 44100)
  -t <0-11>      Start on this theme index
  -f <n>         Target FPS (default: from config, fallback 60)
  -w             Auto bar width based on terminal columns
  -h             Show help
```

**Examples**

```bash
# Auto-detect backend and source
viz

# Force PipeWire with a specific monitor
viz -b pipewire -s "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor"

# Microphone input via PulseAudio
viz -b pulse -M

# Auto-sized bars, Neon theme
viz -w -t 2
```

---

## Keybindings

| Key | Action |
|---|---|
| `t` | Next theme |
| `g` | Cycle gap width — 0 → 1 → 2 → 0 |
| `]` | Increase bar width |
| `[` | Decrease bar width |
| `↑` | Increase sensitivity (disables auto) |
| `↓` | Decrease sensitivity (disables auto) |
| `a` | Toggle auto-sensitivity |
| `s` | Toggle stereo / mono |
| `c` | Toggle colour cycle (hue rotates over time) |
| `v` | Toggle per-bar colour (bass→base, treble→tip) |
| `w` | Toggle A-weighting (IEC 61672 perceptual) |
| `n` | Toggle auto-mono (collapse on high L/R correlation) |
| `h` | Toggle HUD pin (always visible) |
| `q` | Quit |

---

## Config file

**Location:** `~/.config/cava-viz/config`

Created automatically on first run with all fields documented inline.
Changes are applied **instantly** while `viz` is running — no restart needed.

```ini
# ── Visual ────────────────────────────────────────────────────────────────────
# 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy
# 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor
theme          = 0
bar_width      = 2        # 1–8
gap_width      = 1        # 0–2
hud_pinned     = 0        # 1 = HUD always visible

# ── Rendering modes ───────────────────────────────────────────────────────────
colour_cycle   = 0        # slowly rotate gradient hue over time
per_bar_colour = 0        # map colour to bar index (bass=base, treble=tip)

# ── Audio ─────────────────────────────────────────────────────────────────────
stereo         = 1        # 0 = mono
high_cutoff    = 20000    # Hz, 1000–24000

# ── FFT / Smoothing ───────────────────────────────────────────────────────────
gravity        = 1.00     # fall speed: 0.1 (floaty) – 5.0 (instant)
monstercat     = 1.50     # adjacent-bar spread: 0.0 (off) – 5.0
rise_factor    = 0.30     # attack smoothing: 0.0 (instant) – 0.95 (very slow)
bass_smooth    = 0.00     # extra bass decay: 0.0 (off) – 1.0

# ── Audio processing ──────────────────────────────────────────────────────────
a_weighting    = 0        # IEC 61672 perceptual frequency weighting
noise_gate     = 0.020    # snap to zero below this level: 0.0–0.2
auto_mono      = 0        # collapse to mono on high L/R correlation

# ── Sensitivity ───────────────────────────────────────────────────────────────
sensitivity    = 1.50     # 0.2–8.0
auto_sens      = 1        # auto-adjust to keep peaks near 90%

# ── Performance ───────────────────────────────────────────────────────────────
fps            = 60       # 10–240
```

---

## Themes

| # | Name | Colour arc |
|---|---|---|
| 0 | **Fire** | vivid-orange → orange-red → red → hot-pink |
| 1 | **Plasma** | blue-violet → electric-blue → neon-cyan |
| 2 | **Neon** | electric-magenta → violet → electric-blue |
| 3 | **Teal** | dark-teal → teal → bright-aqua |
| 4 | **Sunset** | amber → orange-red → crimson → deep-violet |
| 5 | **Candy** | hot-rose → magenta → violet |
| 6 | **Aurora** | sea-green → teal → sky-blue → soft-violet |
| 7 | **Inferno** | blood-red → orange → amber → bright-gold |
| 8 | **White** | solid bright white |
| 9 | **Rose** | crimson-rose → coral → salmon → orchid |
| 10 | **Mermaid** | ocean-blue → teal → periwinkle → orchid |
| 11 | **Vapor** | neon-purple → violet-magenta → hot-pink |

---

## Troubleshooting

### Wrong colours on Konsole / GNOME Terminal

`viz` automatically switches `TERM` to `xterm-direct` before starting ncurses so that
`init_color()` works correctly. If colours still look wrong (raw xterm-256 cube colours
instead of the theme gradient), check that `xterm-direct` is in your terminfo database:

```bash
infocmp xterm-direct 2>&1 | head -2
```

If it is missing, update ncurses:

```bash
# Debian / Ubuntu
sudo apt install libncurses6

# Arch
sudo pacman -S ncurses
```

### No audio — bars stay at zero

```bash
# List available monitor sources
pactl list short sources | grep monitor

# Run with an explicit source
viz -s "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor"
```

`viz` auto-retries every 5 seconds of silence and reconnects automatically when
the default sink changes (e.g. plugging in headphones), so most device switches
are handled without restarting.

### Build fails — no backend found

```bash
# Debian / Ubuntu
sudo apt install libpipewire-0.3-dev libpulse-dev

# Arch
sudo pacman -S pipewire libpulse

# Fedora
sudo dnf install pipewire-devel pulseaudio-libs-devel
```

### Binary not found after install

The default install prefix is `/usr/local`. If `viz` is not on your `PATH`:

```bash
export PATH="$PATH:/usr/local/bin"
```

Or install to a prefix already on your path:

```bash
INSTALL_PREFIX=~/.local ./install.sh
```

---

## File locations

| Path | Purpose |
|---|---|
| `~/.config/cava-viz/config` | User config — edit live |
| `~/.local/state/cava-viz/state` | Last used audio source (auto-saved) |

Both directories are created automatically on first run.
Use `./uninstall.sh` to remove the binary and optionally clean up these files.

---

## License

MIT
