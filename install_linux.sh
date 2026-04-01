#!/bin/bash
set -e

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║     Terminal Audio Visualizer - Complete Installation        ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# Install dependencies
echo "[1/4] Installing dependencies..."
sudo pacman -Syu --noconfirm
sudo pacman -S --needed --noconfirm \
    base-devel \
    cmake \
    fftw \
    libpulse \
    alsa-lib \
    ncurses

# Build
echo "[2/4] Cleaning old build..."
rm -rf build

echo "[3/4] Building..."
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Install
echo "[4/4] Installing..."
sudo cmake --install .

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                  Installation Complete!                      ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║                                                              ║"
echo "║  Start visualizer:                                           ║"
echo "║    viz                                                       ║"
echo "║                                                              ║"
echo "║  Configuration: ~/.config/visualizer.conf                    ║"
echo "║                                                              ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
