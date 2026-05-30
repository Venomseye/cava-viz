# cava-viz

A terminal audio visualizer built on the [CAVA](https://github.com/karlstav/cava) algorithm — dual-FFT analysis, Monstercat smoothing, per-bar EQ, and autosensitivity, rendered in ncurses with truecolor gradients.

```
 ▁         ▄                       ▂
 █  ▇  ▃   █  ▆  ▂              ▅  █  ▃
 █  █  █   █  █  █  ▄  ▂  ▁  ▂  █  █  █
─────────────────────────────────────────
 [pw] alsa_output.pci-0000_00_1f.3.monitor   Neon  60fps
```

---

## Features

- **CAVA-faithful algorithm** — dual-FFT, per-bar frequency EQ, Monstercat bar spreading, and autosensitivity derived directly from CAVA's source
- **Truecolor gradients** — 12 built-in themes with smooth HSV arcs; graceful fallback to 256-color and 8-color terminals
- **User-defined themes** — write a simple `.theme` file with hex color stops; the visualizer hot-reloads them instantly while running
- **Stereo visualization** — side-by-side left/right channels with a mono collapse option
- **Live config reload** — edit `~/.config/cava-viz/config` while running; inotify picks up changes in under a second
- **Low CPU footprint** — ncurses dirty-region rendering, frame-deadline sleep via `clock_nanosleep`, and throttled color rebuilds
- **Dual audio backend** — PipeWire and PulseAudio; auto-selects at startup, falls back gracefully, reconnects on device loss
- **HUD** — displays audio source, backend, active theme, FPS, sensitivity, and stereo state; pinnable or auto-hiding

---

## Requirements

| Dependency | Package (Arch) | Package (Debian/Ubuntu) |
|---|---|---|
| C++17 compiler | `gcc` / `clang` | `g++` / `clang++` |
| CMake ≥ 3.16 | `cmake` | `cmake` |
| Ninja (optional, faster) | `ninja` | `ninja-build` |
| FFTW3 | `fftw` | `libfftw3-dev` |
| ncursesw | `ncurses` | `libncursesw5-dev` |
| PipeWire *(optional)* | `pipewire` | `libpipewire-0.3-dev` |
| PulseAudio *(optional)* | `libpulse` | `libpulse-dev` |

At least one audio backend (PipeWire or PulseAudio) must be present.

---

## Installation

```bash
git clone https://github.com/venomseye/cava-viz.git
cd cava-viz
chmod +x install.sh uninstall.sh
./install.sh
```

The script configures with CMake, builds with Ninja (or Make as a fallback), and installs the `viz` binary to `/usr/local/bin`.

**Options:**

```bash
./install.sh                  # incremental build (fast on rebuilds)
./install.sh --clean          # wipe build dir first, then build
INSTALL_PREFIX=~/.local ./install.sh   # install to a custom prefix
```

**To uninstall:**

```bash
./uninstall.sh
```

**Manual build** (if you prefer to drive CMake yourself):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

---

## Usage

```
viz [OPTIONS]
```

| Flag | Description | Default |
|---|---|---|
| `-b <pulse\|pipewire\|auto>` | Audio backend | `auto` |
| `-s <source>` | Explicit capture device | *(auto-detect monitor)* |
| `-M` | Capture from microphone instead of monitor | off |
| `-r <Hz>` | Sample rate | `44100` |
| `-t <0–11>` | Starting theme index | `0` (Fire) |
| `-f <n>` | Target FPS | `60` |
| `-w` | Auto bar width (fills terminal) | off |
| `-V` | Print version and exit | |
| `-h` | Print help and exit | |

**Examples:**

```bash
viz                           # auto-detect backend and source
viz -b pipewire               # force PipeWire
viz -s bluez_output.AA_BB.monitor   # specific source
viz -M                        # microphone visualizer
viz -t 2 -f 30 -w             # Neon theme, 30 fps, auto width
```

---

## Keybindings

| Key | Action |
|---|---|
| `q` | Quit |
| `t` | Cycle to next theme (built-in then user-defined) |
| `g` | Cycle gap width: 0 → 1 → 2 |
| `]` / `[` | Increase / decrease bar width |
| `↑` / `↓` | Manually adjust sensitivity |
| `a` | Toggle autosensitivity |
| `s` | Toggle stereo / mono |
| `h` | Toggle HUD pin (always visible vs auto-hide) |
| `c` | Toggle colour cycle (hue rotation over time) |
| `v` | Toggle per-bar colour (maps colour to bar index) |
| `w` | Toggle A-weighting (IEC 61672 perceptual curve) |
| `n` | Toggle auto-mono (collapses stereo when L≈R) |

All settings are persisted to `~/.config/cava-viz/config` on every keypress.

---

## Configuration

The config file is created on first run at `~/.config/cava-viz/config`.
Edit it while the visualizer is running — changes are reloaded instantly.

```ini
# ~/.config/cava-viz/config

# ── Visual ────────────────────────────────────────────────────────────────
theme          = 2          # 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy
                            # 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor
                            # 12+ = user themes (alphabetical by filename)
bar_width      = 2
gap_width      = 1
hud_pinned     = 0

# ── Rendering modes ───────────────────────────────────────────────────────
colour_cycle   = 0          # slowly rotate gradient hue over time
per_bar_colour = 0          # map colour to bar position (bass→treble)

# ── Audio ─────────────────────────────────────────────────────────────────
stereo         = 1
high_cutoff    = 10000      # Hz — frequencies above this are ignored

# ── FFT / Smoothing ───────────────────────────────────────────────────────
gravity        = 1.00       # fall speed (0.1=slow, 5.0=instant)
monstercat     = 1.50       # bar spread (0=off)
rise_factor    = 0.90       # attack smoothing (0=instant, 0.95=very slow)
bass_smooth    = 0.10       # extra smoothing for bass bars

# ── Audio processing ──────────────────────────────────────────────────────
a_weighting    = 0          # IEC 61672 perceptual frequency weighting
noise_gate     = 0.020      # bars below this threshold snap to zero
auto_mono      = 0          # collapse stereo to mono when channels are correlated

# ── Sensitivity ───────────────────────────────────────────────────────────
sensitivity    = 1.00
auto_sens      = 1

# ── Performance ───────────────────────────────────────────────────────────
fps            = 60
```

---

## Themes

### Built-in

| # | Name | Character |
|---|---|---|
| 0 | Fire | Deep red → amber → pale yellow |
| 1 | Plasma | Magenta → violet → electric blue |
| 2 | Neon | Cyan → electric green |
| 3 | Teal | Deep teal → sky blue → white |
| 4 | Sunset | Deep purple → salmon → gold |
| 5 | Candy | Hot pink → lavender → mint |
| 6 | Aurora | Deep navy → emerald → cyan |
| 7 | Inferno | Black → deep red → bright orange |
| 8 | White | Cool grey → pure white |
| 9 | Rose | Dark maroon → rose → blush |
| 10 | Mermaid | Deep indigo → teal → seafoam |
| 11 | Vapor | Deep purple → pink → pale cyan |

### User-defined

Create `~/.config/cava-viz/themes/` and drop `.theme` files in it.
They are loaded alphabetically and cycle right after the built-ins with the `t` key.
Adding, editing, or removing a `.theme` file reloads all user themes instantly — no restart needed.

**Format:**

```ini
# ~/.config/cava-viz/themes/ocean.theme

name   = Ocean

stop_0 = 0.00  #003366
stop_1 = 0.40  #0055aa
stop_2 = 0.75  #00aaee
stop_3 = 1.00  #00ffcc
```

**Rules:**
- `pos` runs from `0.0` (bar bottom) to `1.0` (bar top)
- Colors are `#RRGGBB` hex (upper or lowercase)
- 2–8 stops per theme; stops can appear in any order
- `name` is optional — defaults to the filename without `.theme`
- Lines starting with `#` are comments

**More examples:**

```ini
# synthwave.theme
name   = Synthwave
stop_0 = 0.00  #1a0033
stop_1 = 0.35  #8800cc
stop_2 = 0.65  #ff00aa
stop_3 = 1.00  #ffffaa

# matrix.theme
name   = Matrix
stop_0 = 0.00  #001100
stop_1 = 0.50  #00aa00
stop_2 = 1.00  #ccffcc

# dracula.theme
name   = Dracula
stop_0 = 0.00  #282a36
stop_1 = 0.30  #6272a4
stop_2 = 0.65  #bd93f9
stop_3 = 1.00  #ff79c6
```

---

## File Locations

| Path | Purpose |
|---|---|
| `~/.config/cava-viz/config` | Main configuration (keybindings write here) |
| `~/.config/cava-viz/state` | Last-used audio source (persisted across restarts) |
| `~/.config/cava-viz/themes/` | User-defined `.theme` files |

---

## License

[MIT](LICENSE)
