#!/usr/bin/env bash
# Installer shipped INSIDE the release tarball (as ./install.sh).
# Unlike the repo's top-level install.sh it does not build anything: it copies
# the prebuilt `viz` binary, man page, shell completions and example theme.
#
#   ./install.sh                         # -> /usr/local   (uses sudo if needed)
#   INSTALL_PREFIX=~/.local ./install.sh # -> ~/.local     (no sudo)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${INSTALL_PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

[ -f "$HERE/viz" ] || { echo "error: ./viz not found next to this script" >&2; exit 1; }

# Use sudo only when the target isn't writable and we aren't root.
SUDO=""
probe="$DESTDIR$PREFIX"
while [ ! -d "$probe" ] && [ "$probe" != "/" ]; do probe="$(dirname "$probe")"; done
if [ ! -w "$probe" ] && [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || { echo "error: $PREFIX is not writable and sudo is unavailable; set INSTALL_PREFIX" >&2; exit 1; }
    SUDO="sudo"
fi

put() { # put <mode> <src> <dest-relative-to-prefix>
    [ -f "$2" ] || return 0
    $SUDO install -Dm"$1" "$2" "$DESTDIR$PREFIX/$3"
    echo "  installed $PREFIX/$3"
}

put 755 "$HERE/viz"                      bin/viz
put 644 "$HERE/viz.1"                    share/man/man1/viz.1
put 644 "$HERE/completions/viz.bash"     share/bash-completion/completions/viz
put 644 "$HERE/completions/viz.zsh"      share/zsh/site-functions/_viz
put 644 "$HERE/completions/viz.fish"     share/fish/vendor_completions.d/viz.fish
put 644 "$HERE/examples/ocean.theme"     share/viz/themes/ocean.theme

echo ""
echo "viz installed to $PREFIX/bin/viz"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "note: $PREFIX/bin is not on your PATH" ;; esac
echo "Run: viz        (man viz for the manual)"
