# cava-viz

A terminal audio visualizer built on the [CAVA](https://github.com/karlstav/cava) algorithm — dual-FFT analysis, Monstercat smoothing, per-bar EQ, and autosensitivity, rendered in ncurses with truecolor gradients. Also runs headless as a bar-mode data source for Waybar, Polybar, eww, tmux, and any script.

```
 ▁         ▄                       ▂
 █  ▇  ▃   █  ▆  ▂              ▅  █  ▃
 █  █  █   █  █  █  ▄  ▂  ▁  ▂  █  █  █
─────────────────────────────────────────
 [pw] alsa_output.pci-0000_00_1f.3.monitor   Neon  60fps
```

---

## Features

- **CAVA-faithful algorithm** — dual-FFT, per-bar frequency EQ, Monstercat bar spreading, and autosensitivity
- **Truecolor gradients** — 12 built-in themes; graceful fallback to 256-color and 8-color terminals
- **User-defined themes** — write a `.theme` file with hex color stops; hot-reloaded while running
- **Stereo visualization** — side-by-side left/right channels with mono collapse
- **Live config reload** — edit the config or user themes while running; inotify reacts instantly
- **Headless bar mode** — output formatted bars to stdout, a FIFO, or a Unix socket for status bars
- **Low CPU footprint** — ncurses dirty-region rendering, `clock_nanosleep` frame timing, throttled color rebuilds
- **Dual audio backend** — PipeWire and PulseAudio; auto-selects, falls back gracefully, reconnects on device loss

---

## Requirements

| Dependency | Package (Arch) | Package (Debian/Ubuntu) |
|---|---|---|
| C++17 compiler | `gcc` / `clang` | `g++` / `clang++` |
| CMake ≥ 3.16 | `cmake` | `cmake` |
| Ninja *(optional, faster)* | `ninja` | `ninja-build` |
| FFTW3 | `fftw` | `libfftw3-dev` |
| ncursesw | `ncurses` | `libncursesw5-dev` |
| PipeWire *(optional)* | `pipewire` | `libpipewire-0.3-dev` |
| PulseAudio *(optional)* | `libpulse` | `libpulse-dev` |

At least one audio backend must be present.

---

## Installation

```bash
git clone https://github.com/venomseye/cava-viz.git
cd cava-viz
./install.sh
```

Detects your distro, installs missing dependencies, configures with CMake, builds with Ninja (Make fallback), and installs to `/usr/local/bin`. Also installs the man page and shell completions.

**Options:**

```bash
./install.sh                   # incremental build
./install.sh --clean           # wipe build dir first
./install.sh --test            # run unit tests before installing
./install.sh --skip-deps       # skip dependency check (fast rebuilds)
INSTALL_PREFIX=~/.local ./install.sh   # custom install prefix
```

**Uninstall:**

```bash
./uninstall.sh           # interactive — prompts before removing config
./uninstall.sh --yes     # non-interactive — removes everything
```

**Manual CMake build:**

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
| `-M` | Capture from microphone | off |
| `-r <Hz>` | Sample rate | `44100` |
| `-t <index>` | Starting theme (0–11 built-in, 12+ user) | `0` |
| `-f <n>` | Target FPS | `60` |
| `-w` | Auto bar width to fill terminal | off |
| `--list-sources` | Print available audio sources and exit | |
| `--check` | Validate config and audio setup, then exit | |
| `-V` | Print version and exit | |
| `-h` | Print help and exit | |

**Examples:**

```bash
viz                                      # auto-detect everything
viz -b pipewire -s alsa_output.pci.monitor   # explicit backend + source
viz -M -t 6 -f 30                        # mic input, Aurora theme, 30 fps
viz --list-sources                       # see what's available
viz --check                              # validate your setup
```

---

## Keybindings

| Key | Action |
|---|---|
| `q` | Quit |
| `t` | Cycle to next theme (built-in → user-defined → wrap) |
| `g` | Cycle gap width: 0 → 1 → 2 |
| `]` / `[` | Increase / decrease bar width |
| `↑` / `↓` | Adjust sensitivity manually |
| `a` | Toggle autosensitivity |
| `s` | Toggle stereo / mono |
| `h` | Toggle HUD pin (always visible vs auto-hide) |
| `c` | Toggle colour cycle (slow hue rotation) |
| `v` | Toggle per-bar colour (maps colour to bar position) |
| `w` | Toggle A-weighting (IEC 61672 perceptual curve) |
| `n` | Toggle auto-mono (collapses stereo when L ≈ R) |

All settings are persisted to the config file on every keypress.

---

## Live Config Reload

Edit the config while viz is running — changes apply in under a second:

```bash
$EDITOR ~/.config/cava-viz/config
```

Or trigger a reload from any terminal without editing a file:

```bash
kill -HUP $(pgrep viz)
```

`SIGHUP` reloads the config **and** all user themes, with the same behaviour as inotify (including a stereo/mono audio restart if the `stereo` key changed). Works over SSH and in any environment where inotify isn't available.

---

## Configuration

Created on first run at `${XDG_CONFIG_HOME:-~/.config}/cava-viz/config`.

```ini
# ── Visual ────────────────────────────────────────────────────────────────
theme          = 2          # 0=Fire 1=Plasma 2=Neon 3=Teal 4=Sunset 5=Candy
                            # 6=Aurora 7=Inferno 8=White 9=Rose 10=Mermaid 11=Vapor
                            # 12+ = user themes (alphabetical by filename)
bar_width      = 2          # 1–8
gap_width      = 1          # 0–2
hud_pinned     = 0          # 1 = always show HUD

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
noise_gate     = 0.020      # bars below this snap to zero (0.0–0.2)
auto_mono      = 0          # collapse stereo when channels are correlated

# ── Sensitivity ───────────────────────────────────────────────────────────
sensitivity    = 1.00
auto_sens      = 1

# ── Performance ───────────────────────────────────────────────────────────
fps            = 60
```

---

## Themes

### Built-in (index 0–11)

| # | Name | Gradient |
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

### User-defined (index 12+)

Place `.theme` files in `${XDG_CONFIG_HOME:-~/.config}/cava-viz/themes/`. They load alphabetically after the built-ins and cycle with `t`. Adding, editing, or removing a file reloads instantly — no restart needed.

```ini
# ~/.config/cava-viz/themes/ocean.theme
name   = Ocean

stop_0 = 0.00  #003366
stop_1 = 0.40  #0055aa
stop_2 = 0.75  #00aaee
stop_3 = 1.00  #00ffcc
```

**Rules:** 2–8 stops, pos 0.0–1.0, `#RRGGBB` hex, any order, `name` optional, `#` for comments.

**More examples:**

```ini
# synthwave.theme
name = Synthwave
stop_0 = 0.00  #1a0033
stop_1 = 0.35  #8800cc
stop_2 = 0.65  #ff00aa
stop_3 = 1.00  #ffffaa

# matrix.theme
name = Matrix
stop_0 = 0.00  #001100
stop_1 = 0.50  #00aa00
stop_2 = 1.00  #ccffcc

# dracula.theme
name = Dracula
stop_0 = 0.00  #282a36
stop_1 = 0.30  #6272a4
stop_2 = 0.65  #bd93f9
stop_3 = 1.00  #ff79c6
```

---

## Bar Mode (Status Bar Integration)

`--bar` replaces the ncurses display with a stream of formatted lines — no terminal is touched. Designed for Waybar, Polybar, eww, tmux, and any script.

```bash
viz --bar [--bar-format <fmt>] [--bar-count <n>] [--bar-color <#hex>]
          [--bar-fps <n>] [--bar-stereo <merge|split>]
          [--bar-sink <stdout|fifo|socket>] [--bar-out <path>]
```

| Flag | Description | Default |
|---|---|---|
| `--bar-format` | `plain` `waybar` `polybar` `eww` `raw` `dzen2` `i3bar` | `plain` |
| `--bar-count` | Bars per channel | `10` |
| `--bar-chars` | UTF-8 level chars | `▁▂▃▄▅▆▇█` |
| `--bar-color` | Accent color `#RRGGBB` | `#00ffcc` |
| `--bar-fps` | Output rate | `15` |
| `--bar-stereo` | `merge` or `split` | `merge` |
| `--bar-sep` | Separator for split stereo | ` \| ` |
| `--bar-sink` | `stdout` `fifo` `socket` | `stdout` |
| `--bar-out` | Path for fifo or socket sink | *(required if not stdout)* |

### Waybar

```jsonc
// ~/.config/waybar/config
"custom/viz": {
    "exec": "viz --bar --bar-format waybar --bar-count 12 --bar-color \"#cba6f7\"",
    "return-type": "json",
    "interval": "once",
    "restart-interval": 5,
    "tooltip": false
}
```

```css
/* style.css */
#custom-viz {
    font-family: "JetBrainsMono Nerd Font", monospace;
    font-size: 13px;
    padding: 0 10px;
}
```

### Polybar / lemonbar

```ini
[module/viz]
type = custom/script
exec = viz --bar --bar-format polybar --bar-count 10 --bar-color "#89b4fa"
tail = true
```

### eww

```lisp
(deflisten viz-bars `viz --bar --bar-format plain --bar-count 8`)
(defwidget viz [] (label :class "viz" :text viz-bars))
```

### tmux (via FIFO)

```bash
# Start in background
viz --bar --bar-sink fifo --bar-out /tmp/viz.pipe &

# In tmux.conf
set -g status-right "#(cat /tmp/viz.pipe)"
```

### dzen2 / i3bar

```bash
viz --bar --bar-format dzen2 | dzen2 -fn "JetBrainsMono:size=11"
viz --bar --bar-format i3bar  # for i3bar blocks
```

### Raw floats (custom scripts)

```bash
viz --bar --bar-format raw | python3 my_visualizer.py
```

---

## File Locations

Follows the [XDG Base Directory spec](https://specifications.freedesktop.org/basedir-spec/latest/):

| Path | Purpose |
|---|---|
| `${XDG_CONFIG_HOME:-~/.config}/cava-viz/config` | Main configuration |
| `${XDG_CONFIG_HOME:-~/.config}/cava-viz/themes/` | User `.theme` files |
| `${XDG_STATE_HOME:-~/.local/state}/cava-viz/state` | Last-used audio source |

Override `XDG_CONFIG_HOME` or `XDG_STATE_HOME` to relocate everything.

---

## Troubleshooting

**Bars are flat / no movement**

```bash
viz --list-sources          # see what's available
viz --check                 # validate the whole setup
viz -s "$(pactl get-default-sink).monitor"   # force the default monitor
```

Make sure PipeWire or PulseAudio is running: `systemctl --user status pipewire`

**All bars show `▁` (minimum) even with music playing**

The monitor source captured silence. Check your default sink:
```bash
pactl get-default-sink
pactl list short sources | grep monitor
```

**Terminal shows boxes instead of block characters**

Install a font with Unicode block element support:
```bash
sudo pacman -S ttf-jetbrains-mono-nerd   # Arch
sudo apt install fonts-jetbrains-mono    # Debian/Ubuntu
```

**Gradient looks flat / only a few colors**

Your terminal doesn't support truecolor. Check `$COLORTERM`:
```bash
echo $COLORTERM   # should say "truecolor" or "24bit"
```
Switch to a truecolor terminal: Kitty, WezTerm, Alacritty, or a modern Konsole/GNOME Terminal.

**Waybar module shows nothing**

Test the command directly:
```bash
viz --bar --bar-format waybar --bar-count 10 2>&1 | head -3
```
If it prints `{"text":"..."}` lines, the format is correct — check your Waybar config JSON and `"return-type": "json"`.

---

## Contributing

```bash
# Build with tests
./install.sh --test

# Run individual test suites
cd build && ctest --output-on-failure

# Check formatting (dry run)
find src tests -name "*.cpp" -o -name "*.h" | grep -v fft_processor \
  | xargs clang-format --style=file --dry-run -Werror

# Lint
clang-tidy -p build src/*.cpp   # excludes fft_processor.cpp via .clang-tidy
```

The CI pipeline (`.github/workflows/ci.yml`) runs format, lint, build (Release + Debug+ASan), and all three test suites on every push and pull request.

---

## License

[MIT](LICENSE)
