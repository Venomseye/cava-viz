Here’s a **polished, GitHub-ready README.md** with badges, structure, and a modern look 👇

---

# 🎧 CLI Audio Visualizer

<p align="center">
  <b>Fast • Lightweight • Terminal-based Audio Visualizer</b><br>
  Built with C++17, PulseAudio & ncurses
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-linux-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/build-cmake-green?style=for-the-badge">
  <img src="https://img.shields.io/badge/license-MIT-orange?style=for-the-badge">
  <img src="https://img.shields.io/badge/status-stable-brightgreen?style=for-the-badge">
</p>

---

## ✨ Features

* 🎵 Real-time audio visualization (PulseAudio)
* 🎨 Multiple styles & themes
* ⚡ Adjustable FPS (10–240, default 60)
* 🔊 Stereo / Mono modes
* 💫 Blur & peak effects
* 📊 VU meter support
* 💾 Auto-save configuration
* 🖥️ Clean ncurses UI

---

## 🐧 Supported Platforms

| Platform      | Status      |
| ------------- | ----------- |
| Arch Linux    | ✅ Stable    |
| Debian/Ubuntu | ✅ Supported |

---

## 📦 Installation

### Arch Linux

```bash
chmod +x install_linux.sh
./install_linux.sh
```

### Debian / Ubuntu

```bash
chmod +x install_debian.sh
sudo ./install_debian.sh
```

---

## ⚙️ Controls

| Key     | Action                     |
| ------- | -------------------------- |
| `1-5`   | Change visualization style |
| `t / T` | Next / Previous theme      |
| `+ / -` | Adjust sensitivity         |
| `{ / }` | Decrease / Increase FPS    |
| `m`     | Toggle Mono / Stereo       |
| `b`     | Blur effect                |
| `v`     | VU meter                   |
| `s`     | Peak dots                  |
| `r`     | Reset settings             |
| `q`     | Quit                       |

---

## ⚡ FPS Control (New)

* Adjustable from **10 → 240 FPS**
* Default: **60 FPS**
* Step size: **10**
* Changes reflected in HUD
* Saved in config file

---

## 🧠 Configuration

Auto-saved at:

```bash
~/.config/visualizer.conf
```

Includes:

* FPS
* Sensitivity
* Theme
* Effects

---

## 🛠️ Build From Source

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
sudo cmake --install .
```

---

## 📚 Dependencies

* FFTW3
* PulseAudio
* ALSA *(optional)*
* ncurses
* CMake (3.15+)

---

## 🧪 Troubleshooting

### No Audio?

```bash
pactl info
```

### CMake Errors?

```bash
rm -rf build
cmake .. --debug-output
```

### Missing Dependencies?

```bash
# Debian/Ubuntu
sudo apt-get update

# Arch
sudo pacman -Syu
```

---

## 🚀 Roadmap

* [ ] Wayland native integration
* [ ] PipeWire support
* [ ] Config UI menu
* [ ] Plugin system
* [ ] More visual styles

---

## 🤝 Contributing

Pull requests are welcome!
Feel free to open issues for bugs or feature requests.

---

## ⭐ Support

If you like this project:

* ⭐ Star the repo
* 🐛 Report bugs
* 💡 Suggest features

---
