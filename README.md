# cli_visualizer
A high-performance terminal-based audio visualizer built in C++ using ncurses, FFTW, and PulseAudio. The tool captures live system audio and renders a dynamic, real-time frequency spectrum directly in the terminal.


# Audio Visualizer

Terminal-based audio visualizer using:
- C++
- ncurses
- PulseAudio
- FFTW

## Build

g++ cli.cpp -o visualizer -lncurses -lpulse-simple -lpulse -lfftw3 -lm -O2 -std=c++17

## Run

./visualizer

## Controls

- m → toggle mono/stereo
- q → quit
- 1 - 4 → bar styles
- t/T → next/prev theme
- +/- → sensitivity
- ] [ → peak fall speed
- s → toggle peak on/off
- b → blur/trail on/off
- r → reset
- q → quit

HOW TO INSTALL:
git clone https://github.com/venomseye/visualizer
cd visualizer
chmod +x install.sh
./install.sh
