#!/bin/bash
# Sofi installation script for Arch-based systems

set -e

echo "Installing Sofi launcher..."

# Install dependencies
echo "Installing dependencies..."
sudo pacman -S --needed meson wayland wayland-protocols cairo pango libxkbcommon freetype2 harfbuzz glib2 scdoc

# Build
echo "Building Sofi..."
meson setup build --prefix=/usr/local
cd build
ninja

# Install
echo "Installing to system..."
sudo ninja install

# Create config directory
mkdir -p ~/.config/sofi

# Copy example config if no config exists
if [ ! -f ~/.config/sofi/config ]; then
    cp ../examples/config ~/.config/sofi/config
    echo "Example config copied to ~/.config/sofi/config"
fi

echo ""
echo "✅ Sofi installed successfully!"
echo ""
echo "Usage:"
echo "  sofi         - Default launcher"
echo "  sofi-run     - Launch from PATH"
echo "  sofi-drun    - Launch desktop apps"
echo "  sofi-files   - Unified app and file search"
echo ""
echo "Configure by editing: ~/.config/sofi/config"