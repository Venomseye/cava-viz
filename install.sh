#!/bin/bash
set -e

echo "Installing dependencies..."
sudo pacman -S --needed ncurses fftw libpulse

echo "Building..."
g++ visualizer.cpp -o visualizer \
    -lncursesw -lpulse-simple -lpulse -lfftw3 -lm -O2 -std=c++17

echo "Installing to /usr/local/bin..."
sudo install -m755 visualizer /usr/local/bin/viz

echo "Done! Run with: visualizer"
