# Maintainer: Joshua <your@email.com>
pkgname=viz
pkgver=1.2.0
pkgrel=1
pkgdesc="Terminal audio visualizer using the CAVA algorithm with ncurses truecolor gradients"
arch=('x86_64' 'aarch64')
url="https://github.com/Venomseye/viz"
license=('MIT')

# Both backends are compiled in (ENABLE_PIPEWIRE/ENABLE_PULSEAUDIO=ON below),
# so the binary links libpulse*.so and libpipewire-0.3.so and will not start
# without them: they are hard runtime dependencies, not optdepends.
depends=(
    'fftw'
    'ncurses'
    'libpulse'
    'libpipewire'
)
makedepends=(
    'cmake'
    'ninja'
    'pkg-config'
    'pipewire'      # headers for the PipeWire backend
    'libpulse'
)
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')  # replace with actual sha256 after tagging

build() {
    cd "$pkgname-$pkgver"
    cmake -B build -S . \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DNATIVE_ARCH=OFF \
        -DBUILD_TESTS=ON \
        -DENABLE_PIPEWIRE=ON \
        -DENABLE_PULSEAUDIO=ON \
        -Wno-dev
    cmake --build build --parallel
}

check() {
    cd "$pkgname-$pkgver"
    # every suite registered in CMake (config, user_theme, fft_processor,
    # audio_utils, text_utils)
    ctest --test-dir build --output-on-failure
}

package() {
    cd "$pkgname-$pkgver"
    DESTDIR="$pkgdir" cmake --install build

    # Man page, completions and example themes are installed by CMake.

    # License
    install -Dm644 LICENSE \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
