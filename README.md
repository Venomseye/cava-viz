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
  - Dual bass/mid FFT for full 50 Hz – 20 kHz range
  - Log-distributed frequency bands with per-band EQ
  - Monstercat smoothing (O(n) two-pass, configurable strength)
  - Auto-sensitivity with overshoot detection — no initial blast
  - Configurable gravity, rise smoothing, and high-frequency cutoff

- **Smooth rendering**
  - Unicode sub-cell bars (`▁▂▃▄▅▆▇█`)
  - Incremental redraw — only changed cells repainted each frame
  - Beat flash overlay — low-frequency energy pulse brightens bars on kick drums
  - No jitter, no flicker, no sticking artifacts

- **12 gradient themes**
  - Per-row RGB interpolation on truecolour terminals
  - 256-colour and 8-colour fallbacks
  - Clean transitions without banding

- **Stereo mirror layout**
  - Bass centred, treble expands outward
  - Hot stereo/mono toggle — no restart needed

- **HUD**
  - Shows backend · theme · bar count · source · width · sensitivity · FPS
  - Auto-hides after 3 s of inactivity; pin permanently with `h`
  - Brief feedback flash on every setting change

- **Live config reload (Linux)**
  - Edit `~/.config/cava-viz/config` while running — changes apply instantly via inotify
  - Internal state (last audio source) stored separately in `~/.local/state/cava-viz/state`

- **Robust audio**
  - PipeWire + PulseAudio with automatic backend selection
  - Auto-detect monitor source; watchdog reconnects on failure or source change
  - RMS silence detection — reconnects if source goes silent for 5 s

---

## Install (Recommended)

```bash
git clone https://github.com/venomseye/cava-viz
cd cava-viz
chmod +x install.sh uninstall.sh
./install.sh
```

The installer will:

* Detect your distro (**Arch / Debian / Fedora**)
* Install missing dependencies automatically
* Build with **Ninja** (if available, otherwise Make)
* Install the binary to `/usr/local/bin/viz`

---

### Custom install location

```bash
INSTALL_PREFIX=$HOME/.local ./install.sh
```

Then ensure:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

---

## Uninstall

```bash
./uninstall.sh
```

* Removes the installed binary (`viz`)
* Optionally removes config at `~/.config/cava-viz/`
* Optionally removes internal state at `~/.local/state/cava-viz/`

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

| Key       | Action                        |
| --------- | ----------------------------- |
| `q`       | Quit                          |
| `t`       | Next theme                    |
| `g`       | Cycle gap (0 → 1 → 2 → 0)    |
| `]` / `[` | Increase / decrease bar width |
| `↑` / `↓` | Adjust manual sensitivity     |
| `a`       | Toggle auto-sensitivity       |
| `s`       | Toggle stereo / mono (live)   |
| `h`       | Toggle HUD pin (always show)  |

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

---

## Config

Config file: `~/.config/cava-viz/config`

Created automatically on first run. Edit while running — inotify reloads changes instantly.

```ini
# ── Visual ────────────────────────────────────────────────────────
# 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy
# 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor
theme         = 0
bar_width     = 2
gap_width     = 1
hud_pinned    = 0

# ── Audio ─────────────────────────────────────────────────────────
stereo        = 1
high_cutoff   = 20000     # Hz, range 1000-24000

# ── FFT / Smoothing ───────────────────────────────────────────────
# gravity: fall speed (0.1=slow, 1.0=default, 5.0=instant)
gravity       = 1.00
# monstercat: bar spread (0=off, 1.5=default, 5.0=max)
monstercat    = 1.50
# rise_factor: attack smoothing (0.0=instant, 0.95=very slow)
rise_factor   = 0.30

# ── Sensitivity ───────────────────────────────────────────────────
sensitivity   = 1.50
auto_sens     = 1

# ── Performance ───────────────────────────────────────────────────
fps           = 60
```

> `last_source` is no longer in the user config. It is stored internally in
> `~/.local/state/cava-viz/state` so the config file stays clean and editable.

---

## Dependencies

| Package           | Required | Purpose              |
| ----------------- | -------- | -------------------- |
| `cmake`           | ✓        | Build system         |
| `pkg-config`      | ✓        | Dependency detection |
| `fftw3`           | ✓        | FFT computation      |
| `ncursesw`        | ✓        | Terminal rendering   |
| `libpipewire-0.3` | optional | PipeWire backend     |
| `libpulse`        | optional | PulseAudio backend   |

At least one audio backend is required. `./install.sh` handles everything automatically on Arch, Debian, and Fedora.

---

## Architecture

```
main.cpp               — event loop, input, inotify reload, watchdog
renderer.cpp/h         — ncurses drawing, gradients, beat flash, HUD
fft_processor.cpp/h    — FFT, CAVA smoothing, auto-sensitivity, monstercat
config.cpp/h           — user config load/save; state file (last_source)
audio_capture.h        — AudioCapture interface (backend-agnostic)
pulse_capture.cpp/h    — PulseAudio backend
pipewire_capture.cpp/h — PipeWire backend
```

### Algorithm highlights

- **Dual FFT** — bass range uses a 2× larger buffer for sub-100 Hz bin resolution
- **FPS-normalised smoothing** — gravity fall step and integral decay scale with frame rate so bars behave identically at 30, 60, or 120 fps
- **O(n) monstercat** — two-pass rolling-max replaces the original O(n²) nested loop with no change in output
- **Auto-sensitivity** — initialises at `1/manual_sensitivity` so the first displayed frame is never over-driven; init ramp tuned to settle without an initial blast

---
