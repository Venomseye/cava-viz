#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  viz — Terminal Audio Visualizer  |  uninstall.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
BIN="${INSTALL_PREFIX}/bin/viz"

C_RED='\033[0;31m'; C_GRN='\033[0;32m'; C_YLW='\033[1;33m'
C_CYN='\033[0;36m'; C_BLD='\033[1m';    C_RST='\033[0m'

info() { echo -e "  ${C_CYN}→${C_RST} $*"; }
ok()   { echo -e "  ${C_GRN}✓${C_RST} $*"; }
warn() { echo -e "  ${C_YLW}!${C_RST} $*"; }
die()  { echo -e "  ${C_RED}✗${C_RST} $*" >&2; exit 1; }

# ── Flags ─────────────────────────────────────────────────────────────────────
YES=0
for arg in "$@"; do
    case "$arg" in
        --yes|-y) YES=1 ;;
    esac
done

echo ""
echo -e "${C_BLD}  viz — Uninstall${C_RST}"
echo ""

# Prompt helper — auto-confirms when --yes is passed
confirm() {
    local prompt="$1"
    if [ "$YES" -eq 1 ]; then
        echo -e "  ${C_YLW}?${C_RST} $prompt [auto-yes]"
        return 0
    fi
    read -r -p "  $prompt [y/N] " ans
    [[ "${ans,,}" == "y" ]]
}

# Remove helper — uses sudo only when needed
remove() {
    local path="$1"
    if [ -w "$(dirname "$path")" ]; then
        rm -rf "$path"
    else
        sudo rm -rf "$path"
    fi
}

# ── Binary ────────────────────────────────────────────────────────────────────
if [ -f "$BIN" ]; then
    info "Removing binary: $BIN"
    remove "$BIN"
    ok "Removed binary."
else
    warn "Binary not found at $BIN"
    warn "Already uninstalled, or try: INSTALL_PREFIX=/usr ./uninstall.sh"
fi

# ── Man page ──────────────────────────────────────────────────────────────────
MAN_FILE="${INSTALL_PREFIX}/share/man/man1/cava-viz.1"
if [ -f "$MAN_FILE" ]; then
    info "Removing man page: $MAN_FILE"
    remove "$MAN_FILE"
    ok "Removed man page."
fi

# ── Shell completions ─────────────────────────────────────────────────────────
BASH_C="${INSTALL_PREFIX}/share/bash-completion/completions/viz"
ZSH_C="${INSTALL_PREFIX}/share/zsh/site-functions/_viz"
FISH_C="${INSTALL_PREFIX}/share/fish/vendor_completions.d/viz.fish"

removed_completions=0
for f in "$BASH_C" "$ZSH_C" "$FISH_C"; do
    if [ -f "$f" ]; then
        info "Removing completion: $f"
        remove "$f"
        removed_completions=1
    fi
done
[ "$removed_completions" -eq 1 ] && ok "Removed shell completions."

# ── Shared data (example themes) ──────────────────────────────────────────────
DATA_DIR="${INSTALL_PREFIX}/share/cava-viz"
if [ -d "$DATA_DIR" ]; then
    info "Removing shared data: $DATA_DIR"
    remove "$DATA_DIR"
    ok "Removed shared data."
fi

# ── User config ───────────────────────────────────────────────────────────────
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/cava-viz"
if [ -d "$CONFIG_DIR" ]; then
    echo ""
    if confirm "Remove saved config at $CONFIG_DIR?"; then
        rm -rf "$CONFIG_DIR"
        ok "Removed config (themes, settings)."
    else
        info "Config kept at $CONFIG_DIR"
    fi
fi

# ── User state ────────────────────────────────────────────────────────────────
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/cava-viz"
if [ -d "$STATE_DIR" ]; then
    echo ""
    if confirm "Remove saved state at $STATE_DIR?"; then
        rm -rf "$STATE_DIR"
        ok "Removed state (last audio source)."
    else
        info "State kept at $STATE_DIR"
    fi
fi

echo ""
echo -e "  ${C_BLD}viz has been uninstalled.${C_RST}"
echo ""
