#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  viz — Terminal Audio Visualizer  |  uninstall.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
BIN="${INSTALL_PREFIX}/bin/viz"

C_RED='\033[0;31m'; C_GRN='\033[0;32m'; C_YLW='\033[1;33m'
C_CYN='\033[0;36m'; C_BLD='\033[1m'; C_RST='\033[0m'

info() { echo -e "  ${C_CYN}→${C_RST} $*"; }
ok()   { echo -e "  ${C_GRN}✓${C_RST} $*"; }
warn() { echo -e "  ${C_YLW}!${C_RST} $*"; }

echo ""
echo -e "${C_BLD}  viz — Uninstall${C_RST}"
echo ""

# ── Remove binary ─────────────────────────────────────────────────────────────
if [ -f "$BIN" ]; then
    info "Removing $BIN ..."
    if [ -w "$(dirname "$BIN")" ]; then
        rm -f "$BIN"
    else
        sudo rm -f "$BIN"
    fi
    ok "Removed $BIN"
else
    warn "Binary not found at $BIN"
    warn "Already uninstalled, or try: INSTALL_PREFIX=/usr ./uninstall.sh"
fi

# ── Optionally remove config ──────────────────────────────────────────────────
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/cava-viz"
if [ -d "$CONFIG_DIR" ]; then
    echo ""
    read -r -p "  Remove saved config at $CONFIG_DIR? [y/N] " ans
    if [[ "${ans,,}" == "y" ]]; then
        rm -rf "$CONFIG_DIR"
        ok "Removed config"
    else
        info "Config kept at $CONFIG_DIR"
    fi
fi

echo ""
echo -e "  ${C_BLD}viz has been uninstalled.${C_RST}"
echo ""
