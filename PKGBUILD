# Maintainer: Your Name <you@example.com>
pkgname=cava-viz
pkgver=1.1.0
pkgrel=1
pkgdesc="Cava-inspired terminal audio visualizer with PipeWire and PulseAudio support"
arch=('x86_64' 'aarch64')
url="https://github.com/yourusername/cava-viz"
license=('MIT')

# Runtime dependencies
depends=(
    'fftw'
    'ncurses'
    'gcc-libs'
    'glibc'
)

# Optional runtime backends
optdepends=(
    'libpulse: PulseAudio audio backend'
    'pipewire: PipeWire audio backend (recommended)'
    'pipewire-pulse: PulseAudio compatibility via PipeWire'
)

# Build tools
makedepends=(
    'cmake'
    'pkg-config'
    'ninja'
    # At least one of:
    'libpulse'
    'pipewire'
    'spa-headers'   # libspa-0.2
)

# ── Source ─────────────────────────────────────────────────────────────────────
# For a local build from the project directory:
source=("${pkgname}-${pkgver}.tar.gz")
sha256sums=('SKIP')

# Alternatively, from git:
# source=("git+https://github.com/yourusername/cava-viz.git#tag=v${pkgver}")
# sha256sums=('SKIP')

# ── Build ──────────────────────────────────────────────────────────────────────
build() {
    cmake \
        -B "${srcdir}/build" \
        -S "${srcdir}/${pkgname}-${pkgver}" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DENABLE_PULSEAUDIO=ON \
        -DENABLE_PIPEWIRE=ON \
        -Wno-dev

    cmake --build "${srcdir}/build" --parallel
}

# ── Check (optional unit tests) ───────────────────────────────────────────────
# check() {
#     cmake --build "${srcdir}/build" --target test
# }

# ── Package ────────────────────────────────────────────────────────────────────
package() {
    DESTDIR="${pkgdir}" cmake --install "${srcdir}/build"

    # License
    install -Dm644 \
        "${srcdir}/${pkgname}-${pkgver}/LICENSE" \
        "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"

    # Optional: desktop/man files if added later
    # install -Dm644 "${srcdir}/${pkgname}-${pkgver}/doc/${pkgname}.1" \
    #     "${pkgdir}/usr/share/man/man1/${pkgname}.1"
}

# ── Local build helper (not standard PKGBUILD, for convenience) ───────────────
# To build locally without making a tarball, run from the project root:
#
#   mkdir -p pkg-build && cd pkg-build
#   cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build . --parallel
#   sudo cmake --install .
#
# Or via makepkg after creating the tarball:
#   tar czf cava-viz-1.1.0.tar.gz --transform 's,^,cava-viz-1.1.0/,' \
#       CMakeLists.txt src/ LICENSE README.md
#   makepkg -si
