#!/bin/bash
set -e

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Terminal Audio Visualizer - Debian/Ubuntu Installation      ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "[ERROR] This script must be run as root (use sudo)"
   exit 1
fi

# Update package lists
echo "[1/4] Updating package lists..."
apt-get update

echo "[2/4] Installing dependencies..."
apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libfftw3-dev \
    libpulse-dev \
    libasound2-dev \
    libncurses-dev \
    pkg-config \
    git

# Build
echo "[3/4] Cleaning old build..."
rm -rf build

echo "      Building..."
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Install
echo "[4/4] Installing..."
cmake --install .

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
echo "║  Uninstall: sudo ./uninstall_debian.sh                       ║"
echo "║                                                              ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
