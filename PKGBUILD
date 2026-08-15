# Maintainer: Joshua <your@email.com>
pkgname=cava-viz
pkgver=1.2.0
pkgrel=1
pkgdesc="Terminal audio visualizer using the CAVA algorithm with ncurses truecolor gradients"
arch=('x86_64' 'aarch64')
url="https://github.com/youruser/cava-viz"
license=('MIT')

# PipeWire and PulseAudio are optional — at least one must be present at runtime.
# fftw and ncurses are always required.
depends=(
    'fftw'
    'ncurses'
)
optdepends=(
    'pipewire: PipeWire audio backend'
    'libpulse: PulseAudio audio backend'
)
makedepends=(
    'cmake'
    'ninja'
    'pkg-config'
    'pipewire'      # build with both backends enabled by default
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
        -DENABLE_PIPEWIRE=ON \
        -DENABLE_PULSEAUDIO=ON \
        -Wno-dev
    cmake --build build --parallel
}

check() {
    cd "$pkgname-$pkgver"

    # config round-trip tests
    g++ -std=c++17 -Isrc \
        -o test_config \
        tests/test_config.cpp src/config.cpp
    ./test_config

    # user theme parser tests
    g++ -std=c++17 -Isrc \
        -o test_user_theme \
        tests/test_user_theme.cpp src/user_theme.cpp src/config.cpp
    ./test_user_theme
}

package() {
    cd "$pkgname-$pkgver"
    DESTDIR="$pkgdir" cmake --install build

    # Man page
    install -Dm644 man/cava-viz.1 \
        "$pkgdir/usr/share/man/man1/cava-viz.1"

    # Shell completions
    install -Dm644 completions/cava-viz.bash \
        "$pkgdir/usr/share/bash-completion/completions/viz"
    install -Dm644 completions/cava-viz.zsh \
        "$pkgdir/usr/share/zsh/site-functions/_viz"
    install -Dm644 completions/cava-viz.fish \
        "$pkgdir/usr/share/fish/vendor_completions.d/viz.fish"

    # Default example theme
    install -Dm644 examples/ocean.theme \
        "$pkgdir/usr/share/cava-viz/themes/ocean.theme"

    # License
    install -Dm644 LICENSE \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
