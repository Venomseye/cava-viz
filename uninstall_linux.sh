#!/bin/bash
set -e

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  Terminal Audio Visualizer - Uninstall (Linux)             ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Remove binary
if [ -f /usr/local/bin/viz ]; then
    echo "Removing /usr/local/bin/viz..."
    sudo rm /usr/local/bin/viz
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
echo "╔═══════��════════════════════════════════════════════════════╗"
echo "║  Uninstall Complete                                        ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""