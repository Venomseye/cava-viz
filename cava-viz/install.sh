#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  viz — Terminal Audio Visualizer  |  install.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# ── Colors ────────────────────────────────────────────────────────────────────
C_RED='\033[0;31m'; C_GRN='\033[0;32m'; C_YLW='\033[1;33m'
C_CYN='\033[0;36m'; C_BLD='\033[1m'; C_RST='\033[0m'

info()  { echo -e "  ${C_CYN}→${C_RST} $*"; }
ok()    { echo -e "  ${C_GRN}✓${C_RST} $*"; }
warn()  { echo -e "  ${C_YLW}!${C_RST} $*"; }
die()   { echo -e "  ${C_RED}✗${C_RST} $*" >&2; exit 1; }
header(){ echo -e "\n${C_BLD}$*${C_RST}"; }

echo ""
echo -e "${C_BLD}${C_CYN}  ╔══════════════════════════════════════╗"
echo    "  ║   viz — Terminal Audio Visualizer   ║"
echo -e "  ╚══════════════════════════════════════╝${C_RST}"
echo ""

# ── Dependency tables per distro ──────────────────────────────────────────────
install_arch() {
    header "Installing dependencies (Arch Linux)..."
    sudo pacman -S --needed --noconfirm \
        cmake ninja pkg-config gcc \
        fftw ncurses \
        pipewire libpulse
}
install_debian() {
    header "Installing dependencies (Debian / Ubuntu)..."
    sudo apt-get update -qq
    sudo apt-get install -y \
        cmake ninja-build pkg-config build-essential \
        libfftw3-dev libncursesw5-dev \
        libpipewire-0.3-dev libspa-0.2-dev \
        libpulse-dev
}
install_fedora() {
    header "Installing dependencies (Fedora / RHEL)..."
    sudo dnf install -y \
        cmake ninja-build pkgconf gcc-c++ \
        fftw-devel ncurses-devel \
        pipewire-devel \
        pulseaudio-libs-devel
}

# ── Detect distro and install deps ───────────────────────────────────────────
header "Checking dependencies..."
MISSING_PKGS=0

need_install=0
for req in cmake pkg-config; do
    command -v "$req" &>/dev/null || { warn "Missing tool: $req"; need_install=1; }
done
pkg-config --exists fftw3     2>/dev/null || { warn "Missing library: fftw3";    need_install=1; }
pkg-config --exists ncursesw  2>/dev/null || { warn "Missing library: ncursesw"; need_install=1; }

if [ "$need_install" -eq 1 ]; then
    info "Installing missing dependencies..."
    if   command -v pacman  &>/dev/null; then install_arch
    elif command -v apt-get &>/dev/null; then install_debian
    elif command -v dnf     &>/dev/null; then install_fedora
    else
        warn "Unsupported package manager."
        warn "Please manually install: cmake fftw3 ncursesw pipewire/pulseaudio"
    fi
fi

# Final check
pkg-config --exists fftw3    || die "fftw3 not found — cannot build."
pkg-config --exists ncursesw || die "ncursesw not found — cannot build."
ok "All required dependencies found."

# ── Configure ─────────────────────────────────────────────────────────────────
header "Configuring..."
rm -rf "$BUILD_DIR"

CMAKE_GENERATOR="Unix Makefiles"
command -v ninja &>/dev/null && CMAKE_GENERATOR="Ninja"

cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
    -G "$CMAKE_GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -Wno-dev \
    2>&1 | grep -E "ENABLED|DISABLED|ERROR|error:" || true
ok "Configured (generator: $CMAKE_GENERATOR)"

# ── Build ─────────────────────────────────────────────────────────────────────
header "Building..."
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 4)"
ok "Build complete."

# ── Install ───────────────────────────────────────────────────────────────────
header "Installing to ${INSTALL_PREFIX}/bin/viz ..."
if [ -w "${INSTALL_PREFIX}/bin" ] 2>/dev/null || \
   ([ ! -d "${INSTALL_PREFIX}/bin" ] && [ -w "${INSTALL_PREFIX}" ] 2>/dev/null); then
    cmake --install "$BUILD_DIR"
else
    sudo cmake --install "$BUILD_DIR"
fi

INSTALLED="$(command -v viz 2>/dev/null || echo "${INSTALL_PREFIX}/bin/viz")"
ok "Installed: $INSTALLED"

# ── Done ─────────────────────────────────────────────────────────────────────
echo ""
echo -e "${C_BLD}  Done! Start the visualizer:${C_RST}"
echo ""
echo -e "    ${C_GRN}viz${C_RST}"
echo ""
echo "  Keybindings:"
echo "    t  = cycle theme       ] / [  = bar width"
echo "    g  = cycle gap         ↑ / ↓  = sensitivity"
echo "    a  = toggle auto-sens  q      = quit"
echo ""
