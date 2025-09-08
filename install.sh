#!/bin/bash
# Sorce installation script for Arch-based systems

set -e

echo "Installing Sorce launcher..."

# Install dependencies
echo "Installing dependencies..."
sudo pacman -S --needed meson wayland wayland-protocols cairo pango libxkbcommon freetype2 harfbuzz glib2 scdoc

# Build
echo "Building Sorce..."
meson setup build --prefix=/usr/local
cd build
ninja

# Install
echo "Installing to system..."
sudo ninja install

# Create config directory
mkdir -p ~/.config/sorce

# Copy example config if no config exists
if [ ! -f ~/.config/sorce/config ]; then
    cp ../examples/config ~/.config/sorce/config
    echo "Example config copied to ~/.config/sorce/config"
fi

echo ""
echo "✅ Sorce installed successfully!"
echo ""
echo "Usage:"
echo "  sorce         - Default launcher"
echo "  sorce-run     - Launch from PATH"
echo "  sorce-drun    - Launch desktop apps"
echo "  sorce-files   - Unified app and file search"
echo ""
echo "Configure by editing: ~/.config/sorce/config"