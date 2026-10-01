#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  viz — Terminal Audio Visualizer  |  install.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# ── Flags ─────────────────────────────────────────────────────────────────────
CLEAN=0
RUN_TESTS=0
SKIP_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --clean|-c)     CLEAN=1     ;;
        --test|-T)      RUN_TESTS=1 ;;
        --skip-deps|-S) SKIP_DEPS=1 ;;
    esac
done

# ── Colors ────────────────────────────────────────────────────────────────────
C_RED='\033[0;31m'; C_GRN='\033[0;32m'; C_YLW='\033[1;33m'
C_CYN='\033[0;36m'; C_BLD='\033[1m';    C_RST='\033[0m'

info()   { echo -e "  ${C_CYN}→${C_RST} $*"; }
ok()     { echo -e "  ${C_GRN}✓${C_RST} $*"; }
warn()   { echo -e "  ${C_YLW}!${C_RST} $*"; }
die()    { echo -e "  ${C_RED}✗${C_RST} $*" >&2; exit 1; }
header() { echo -e "\n${C_BLD}$*${C_RST}"; }

echo ""
echo -e "${C_BLD}${C_CYN}  ╔══════════════════════════════════════╗"
echo    "  ║   viz — Terminal Audio Visualizer   ║"
echo -e "  ╚══════════════════════════════════════╝${C_RST}"
echo ""

if [ "$CLEAN" -eq 1 ] || [ "$RUN_TESTS" -eq 1 ] || [ "$SKIP_DEPS" -eq 1 ]; then
    echo "  Options:"
    [ "$CLEAN"     -eq 1 ] && echo "    --clean      wipe build dir before configuring"
    [ "$RUN_TESTS" -eq 1 ] && echo "    --test        run unit tests before installing"
    [ "$SKIP_DEPS" -eq 1 ] && echo "    --skip-deps   skip dependency check/install"
    echo ""
fi

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
        libpulse-dev
    sudo apt-get install -y libpipewire-0.3-dev 2>/dev/null || \
        warn "libpipewire-0.3-dev not available — PipeWire backend will be disabled"
}
install_fedora() {
    header "Installing dependencies (Fedora / RHEL)..."
    sudo dnf install -y \
        cmake ninja-build pkgconf gcc-c++ \
        fftw-devel ncurses-devel \
        pipewire-devel \
        pulseaudio-libs-devel
}

# ── Detect distro and install deps ────────────────────────────────────────────
if [ "$SKIP_DEPS" -eq 0 ]; then
    header "Checking dependencies..."

    need_install=0
    for req in cmake pkg-config; do
        command -v "$req" &>/dev/null || { warn "Missing tool: $req"; need_install=1; }
    done
    pkg-config --exists fftw3    2>/dev/null || { warn "Missing library: fftw3";    need_install=1; }
    pkg-config --exists ncursesw 2>/dev/null || { warn "Missing library: ncursesw"; need_install=1; }

    audio_ok=0
    pkg-config --exists libpulse        2>/dev/null && audio_ok=1
    pkg-config --exists libpipewire-0.3 2>/dev/null && audio_ok=1
    if [ "$audio_ok" -eq 0 ]; then
        warn "No audio backend found (libpulse / libpipewire-0.3)"
        need_install=1
    fi

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

    pkg-config --exists fftw3    || die "fftw3 not found — cannot build."
    pkg-config --exists ncursesw || die "ncursesw not found — cannot build."

    audio_ok=0
    pkg-config --exists libpulse        2>/dev/null && audio_ok=1
    pkg-config --exists libpipewire-0.3 2>/dev/null && audio_ok=1
    if [ "$audio_ok" -eq 0 ]; then
        warn "No audio backend available — viz will build but cannot capture audio."
        warn "On Debian/Ubuntu: sudo apt-get install libpulse-dev"
    else
        ok "Audio backend found."
    fi
    ok "All required dependencies found."
else
    info "Skipping dependency check (--skip-deps)"
fi

# ── Configure ─────────────────────────────────────────────────────────────────
header "Configuring..."

if [ "$CLEAN" -eq 1 ]; then
    info "Cleaning build directory (--clean)..."
    rm -rf "$BUILD_DIR"
fi

CMAKE_GENERATOR="Unix Makefiles"
command -v ninja &>/dev/null && CMAKE_GENERATOR="Ninja"

CMAKE_LOG="$BUILD_DIR/cmake_configure.log"
mkdir -p "$BUILD_DIR"

TEST_FLAG="OFF"
[ "$RUN_TESTS" -eq 1 ] && TEST_FLAG="ON"

if ! cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
        -G "$CMAKE_GENERATOR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DBUILD_TESTS="$TEST_FLAG" \
        -Wno-dev \
        2>&1 | tee "$CMAKE_LOG" | grep -E "ENABLED|DISABLED|Tests|Warning|warning"; then
    echo ""
    warn "CMake configure failed. Full log:"
    cat "$CMAKE_LOG"
    die "Configure step failed — see log above."
fi
ok "Configured (generator: $CMAKE_GENERATOR)"

# ── Build ─────────────────────────────────────────────────────────────────────
header "Building..."
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 4)"
ok "Build complete."

# ── Tests ─────────────────────────────────────────────────────────────────────
if [ "$RUN_TESTS" -eq 1 ]; then
    header "Running tests..."
    failed=0
    for t in test_config test_user_theme; do
        BIN="$BUILD_DIR/$t"
        if [ -x "$BIN" ]; then
            info "Running $t ..."
            "$BIN" || { warn "$t FAILED"; failed=1; }
        else
            warn "$t binary not found — skipping"
        fi
    done
    [ "$failed" -eq 1 ] && die "One or more test suites failed — aborting install."
    ok "All tests passed."
fi

# ── Install ───────────────────────────────────────────────────────────────────
header "Installing to ${INSTALL_PREFIX} ..."
if [ -w "${INSTALL_PREFIX}/bin" ] 2>/dev/null || \
   ([ ! -d "${INSTALL_PREFIX}/bin" ] && [ -w "${INSTALL_PREFIX}" ] 2>/dev/null); then
    cmake --install "$BUILD_DIR"
else
    sudo cmake --install "$BUILD_DIR"
fi

INSTALLED="$(command -v viz 2>/dev/null || echo "${INSTALL_PREFIX}/bin/viz")"
ok "Binary:          $INSTALLED"

MAN_PATH="${INSTALL_PREFIX}/share/man/man1/viz.1"
BASH_PATH="${INSTALL_PREFIX}/share/bash-completion/completions/viz"
ZSH_PATH="${INSTALL_PREFIX}/share/zsh/site-functions/_viz"
FISH_PATH="${INSTALL_PREFIX}/share/fish/vendor_completions.d/viz.fish"

[ -f "$MAN_PATH"  ] && ok "Man page:        $MAN_PATH"
[ -f "$BASH_PATH" ] && ok "Bash completion: $BASH_PATH"
[ -f "$ZSH_PATH"  ] && ok "Zsh completion:  $ZSH_PATH"
[ -f "$FISH_PATH" ] && ok "Fish completion: $FISH_PATH"

# ── Post-install guidance ─────────────────────────────────────────────────────
echo ""
echo -e "${C_BLD}  Done! Quick start:${C_RST}"
echo ""
echo -e "    ${C_GRN}viz${C_RST}                           # run the visualizer"
echo -e "    ${C_GRN}viz --list-sources${C_RST}            # see available audio sources"
echo -e "    ${C_GRN}viz --check${C_RST}                   # validate config and setup"

echo ""
echo "  Keybindings:"
echo "    t      = cycle theme        ] / [  = bar width"
echo "    g      = cycle gap          ↑ / ↓  = sensitivity"
echo "    a      = toggle auto-sens   s      = toggle stereo/mono"
echo "    h      = toggle HUD pin     q      = quit"
echo "    c      = colour cycle       v      = per-bar colour"
echo "    w      = A-weighting        n      = auto-mono"
echo ""
echo "  Live reload:  pkill -USR1 -x viz"
echo "  Man page:     man viz"
echo ""
