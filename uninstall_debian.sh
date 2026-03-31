#!/bin/bash
set -e

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  Terminal Audio Visualizer - Uninstall (Debian/Ubuntu)     ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "[ERROR] This script must be run as root (use sudo)"
   exit 1
fi

# Remove binary
if [ -f /usr/local/bin/viz ]; then
    echo "Removing /usr/local/bin/viz..."
    rm /usr/local/bin/viz
    echo "✓ Binary removed"
else
    echo "✗ Binary not found (may have been removed already)"
fi

echo ""

# Ask about config
read -p "Delete saved settings (~/.config/visualizer.conf)? [y/N] " -r
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -f "$HOME/.config/visualizer.conf"
    echo "✓ Config removed"
fi

echo ""

# Ask about build folder
read -p "Delete the build/ folder in this directory? [y/N] " -r
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -rf build/
    echo "✓ Build folder removed"
fi

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║  Uninstall Complete                                        ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
