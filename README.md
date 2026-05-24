# cava-viz

**cava-viz** is a high-performance terminal audio visualizer written in C++, based on the CAVA algorithm. It renders a smooth, real-time stereo spectrum with gradient colours, sub-cell precision, and clean centred output.

---

## Preview

> Themes: Fire · Plasma · Neon · Teal · Sunset · Candy · Aurora · Inferno · White · Rose · Mermaid · Vapor

<!-- Add your GIF / screenshot here -->

---

## Features

### Audio & Algorithm
- Dual bass/mid FFT covering 50 Hz – 20 kHz with log-distributed bands
- Per-band EQ scaling (CAVA `(1/2^28) × freq^0.85` formula)
- **O(n) monstercat** — two-pass rolling-max, configurable strength (0 = off)
- **A-weighting** — optional IEC 61672 perceptual frequency weighting so mids read at their true loudness
- **Noise floor gate** — bars below a configurable threshold snap to zero; eliminates idle shimmer
- **Per-bar smoothing** — bass bars can carry heavier integral decay and slower fall independently of treble
- **Stereo correlation** — detects near-identical L/R signals and auto-collapses to mono visually
- **FPS-normalised smoothing** — gravity fall and integral decay are frame-rate independent; looks identical at 30, 60, or 120 fps
- **Auto-sensitivity** — starts at `1/manual_sensitivity` so the first frame is never overdriven; tuned init ramp, no initial blast

### Rendering
- Unicode sub-cell bars (`▁▂▃▄▅▆▇█`) — 8× vertical resolution per terminal row
- Incremental redraw — only changed cells are repainted each frame
- **Beat flash** — low-frequency energy pulse applies `A_BOLD` for one frame on kick drums
- **Outline mode** — draw only the topmost cell of each bar for a lighter, retro look
- **Colour cycle** — gradient hue rotates slowly over time, independent of theme
- **Per-bar colour** — gradient maps to bar index (bass = dark, treble = bright) instead of screen row

### Themes & Terminal Compatibility
- 12 hand-tuned RGB gradient themes
- Full truecolour rendering on any terminal that supports it — including **Konsole**, GNOME Terminal, Xfce Terminal, Terminator, kitty, Alacritty, and iTerm2 — detected via `COLORTERM`, `KONSOLE_VERSION`, `VTE_VERSION`, and other environment signals rather than relying on the unreliable `can_change_color()` terminfo flag
- 256-colour fallback palette for terminals without truecolour
- 8-colour fallback for basic terminals

### System
- Hot stereo/mono toggle — no restart needed; `reinit()` swaps channel count in-place
- HUD: backend · theme · bar count · source · width · sensitivity · mode flags · FPS; auto-hides after 3 s, pin with `h`
- Live config reload via inotify — edit the config file while running and changes apply instantly
- Watchdog: auto-reconnects on backend failure, source change, or 5 s RMS silence
- **Low CPU / thermal impact** — `clock_nanosleep` absolute-deadline frame limiter, `nice +5` process priority, and `wtimeout` aligned to the frame budget keep CPU cores in deep C-states between frames

---

## Install

```bash
git clone https://github.com/venomseye/cava-viz
cd cava-viz
chmod +x install.sh uninstall.sh
./install.sh
```

The installer:
- Detects your distro (**Arch / Debian / Ubuntu / Fedora**)
- Installs all required and optional dependencies automatically
- Builds with **Ninja** (falls back to Make if unavailable)
- Installs the binary to `/usr/local/bin/viz`

### Custom install prefix

```bash
INSTALL_PREFIX=$HOME/.local ./install.sh
export PATH="$HOME/.local/bin:$PATH"
```

---

## Uninstall

```bash
./uninstall.sh
```

Removes the binary, and optionally:
- Config at `~/.config/cava-viz/`
- Internal state at `~/.local/state/cava-viz/`

---

## Manual Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)
sudo cmake --install .
```

---

## Usage

```
viz [OPTIONS]

Options:
  -b <pulse|pipewire|auto>   Audio backend (default: auto)
  -s <source>                Explicit source device name
  -M                         Use microphone input
  -r <Hz>                    Sample rate (default: 44100)
  -t <0-11>                  Initial theme
  -f <n>                     Target FPS (default: 60)
  -w                         Auto bar width (scales with terminal width)
  -h                         Show help and exit
```

---

## Keybindings

| Key       | Action                                          |
| --------- | ----------------------------------------------- |
| `q`       | Quit                                            |
| `t`       | Next theme                                      |
| `g`       | Cycle gap width (0 → 1 → 2 → 0)                |
| `]` / `[` | Increase / decrease bar width                   |
| `↑` / `↓` | Adjust manual sensitivity                       |
| `a`       | Toggle auto-sensitivity                         |
| `s`       | Toggle stereo / mono (live — no restart)        |
| `h`       | Toggle HUD pin (always visible)                 |
| `o`       | Toggle outline mode (top cell only)             |
| `c`       | Toggle colour cycle (hue rotates over time)     |
| `v`       | Toggle per-bar colour (bass→treble colour map)  |
| `w`       | Toggle A-weighting (perceptual EQ)              |
| `n`       | Toggle auto-mono (collapse near-identical L/R)  |

All keybindings save to config immediately — settings survive crashes and restarts.

---

## Themes

| Index | Name    |
| ----- | ------- |
| 0     | Fire    |
| 1     | Plasma  |
| 2     | Neon    |
| 3     | Teal    |
| 4     | Sunset  |
| 5     | Candy   |
| 6     | Aurora  |
| 7     | Inferno |
| 8     | White   |
| 9     | Rose    |
| 10    | Mermaid |
| 11    | Vapor   |

Themes use smooth per-step RGB interpolation on truecolour terminals. On 256-colour terminals a hand-tuned palette fallback is used. Theme changes apply immediately and are saved to config.

---

## Config

**File:** `~/.config/cava-viz/config`

Created automatically on first run. Editing the file while `viz` is running applies changes instantly via inotify — no restart needed.

```ini
# ── Visual ────────────────────────────────────────────────────────────
# 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy
# 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor
theme          = 0
bar_width      = 2
gap_width      = 1
hud_pinned     = 0

# ── Rendering modes ───────────────────────────────────────────────────
# outline_mode: draw only the top cell of each bar
outline_mode   = 0
# colour_cycle: slowly rotate gradient hue over time
colour_cycle   = 0
# per_bar_colour: colour maps to bar index (bass=dark, treble=bright)
per_bar_colour = 0

# ── Audio ─────────────────────────────────────────────────────────────
stereo         = 1
high_cutoff    = 20000    # Hz, range 1000-24000

# ── FFT / Smoothing ───────────────────────────────────────────────────
# gravity: fall speed (0.1=slow, 1.0=CAVA default, 5.0=instant)
gravity        = 1.00
# monstercat: adjacent-bar spread (0=off, 1.5=CAVA default)
monstercat     = 1.50
# rise_factor: attack smoothing (0.0=instant snap, 0.95=very slow)
rise_factor    = 0.30
# bass_smooth: extra smoothing for bass bars only (0.0=off, 0.1-0.3 recommended)
bass_smooth    = 0.00

# ── Audio processing ──────────────────────────────────────────────────
# a_weighting: IEC 61672 perceptual frequency weighting
a_weighting    = 0
# noise_gate: bars below this (post-sensitivity) snap to zero (0.0-0.2)
noise_gate     = 0.020
# auto_mono: collapse stereo to mono when L/R correlation is very high
auto_mono      = 0

# ── Sensitivity ───────────────────────────────────────────────────────
sensitivity    = 1.50
auto_sens      = 1

# ── Performance ───────────────────────────────────────────────────────
fps            = 60
```

> `last_source` is not in the user config. It is stored in `~/.local/state/cava-viz/state` so the config file stays clean and hand-editable.

---

## Dependencies

| Package           | Required | Debian / Ubuntu pkg      | Arch pkg              | Fedora pkg               |
| ----------------- | -------- | ------------------------ | --------------------- | ------------------------ |
| cmake             | ✓        | `cmake`                  | `cmake`               | `cmake`                  |
| pkg-config        | ✓        | `pkg-config`             | `pkgconf`             | `pkgconfig`              |
| fftw3             | ✓        | `libfftw3-dev`           | `fftw`                | `fftw-devel`             |
| ncursesw          | ✓        | `libncursesw5-dev`       | `ncurses`             | `ncurses-devel`          |
| PulseAudio        | optional | `libpulse-dev`           | `libpulse`            | `pulseaudio-libs-devel`  |
| PipeWire          | optional | `libpipewire-0.3-dev`    | `pipewire`            | `pipewire-devel`         |

At least one audio backend (PulseAudio or PipeWire) is required to capture audio. `./install.sh` handles all of this automatically.

---

## Terminal Compatibility

| Terminal              | Truecolour themes | Notes                                    |
| --------------------- | :---------------: | ---------------------------------------- |
| Konsole               | ✓                 | Detected via `KONSOLE_VERSION`           |
| GNOME Terminal        | ✓                 | Detected via `VTE_VERSION`               |
| Xfce Terminal         | ✓                 | Detected via `VTE_VERSION`               |
| Terminator            | ✓                 | Detected via `VTE_VERSION`               |
| kitty                 | ✓                 | Detected via `KITTY_WINDOW_ID`           |
| Alacritty             | ✓                 | Sets `COLORTERM=truecolor`               |
| foot                  | ✓                 | Sets `COLORTERM=truecolor`               |
| iTerm2                | ✓                 | Detected via `ITERM_SESSION_ID`          |
| Windows Terminal (WSL)| ✓                 | Detected via `WT_SESSION`                |
| xterm                 | 256-colour        | No truecolour support                    |
| Linux console (TTY)   | 8-colour          | No 256-colour support                    |

> **Note for Konsole users:** cava-viz detects Konsole via the `KONSOLE_VERSION` environment variable and forces the full truecolour gradient path. The terminfo entry for `xterm-256color` (which Konsole uses) incorrectly advertises no colour-change capability — cava-viz bypasses this and calls `init_color()` directly, which Konsole handles correctly.

---

## Architecture

```
main.cpp               — event loop, input handling, inotify reload, watchdog
renderer.cpp/h         — ncurses drawing, RGB gradients, beat flash, HUD, modes
fft_processor.cpp/h    — FFT, CAVA smoothing, A-weighting, auto-sensitivity
config.cpp/h           — user config (load/save/clamp); state file (last_source)
audio_capture.h        — AudioCapture abstract interface
pulse_capture.cpp/h    — PulseAudio backend
pipewire_capture.cpp/h — PipeWire backend
```

### Algorithm highlights

- **Dual FFT** — bass uses a 2× larger buffer for sub-100 Hz bin resolution; treble uses a standard buffer
- **FPS-normalised smoothing** — gravity fall step = `0.028 × (66/fps)`, integral decay = `NOISE_REDUCTION^(fps/66)` — identical visual behaviour at any frame rate
- **O(n) monstercat** — two left→right and right→left rolling-max passes replace the original O(n²) nested loop with no output difference
- **Auto-sensitivity** — `sens_` initialised to `1/man_sens_` so `combined_sens = 1.0` from frame one; init ramp at 0.04×/frame settles without blasting
- **A-weighting** — IEC 61672 `Ra(f)` formula computed per-bar at `buildPlan()` time, stored in `aw_[]`, applied conditionally with zero plan-rebuild cost on toggle
- **Noise gate** — post-sensitivity threshold clamp eliminates idle jitter without affecting smoothing memory state
- **Stereo correlation** — EMA-smoothed `Σ(L×R) / sqrt(Σ(L²)×Σ(R²))` per frame; hysteresis between ON (0.97) and OFF (0.90) thresholds prevents rapid toggling

---
