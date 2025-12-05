#!/bin/bash
# Installation script for preload-ng
# This script compiles and optionally installs preload

set -e

echo "=========================================="
echo "  Preload-NG - Installation Script"
echo "=========================================="
echo ""

# Check dependencies
echo "[1/4] Checking dependencies..."

check_command() {
    if ! command -v "$1" &>/dev/null; then
        echo "Error: '$1' not found. Please install it first."
        exit 1
    fi
}

check_command autoreconf
check_command make
check_command gcc

echo "      ✓ All dependencies found"
echo ""

# Run autoreconf
echo "[2/4] Running autoreconf..."
autoreconf -fi
echo "      ✓ autoreconf completed"
echo ""

# Run configure
echo "[3/4] Running configure..."
./configure
echo "      ✓ configure completed"
echo ""

# Compile
echo "[4/4] Compiling..."
make
echo "      ✓ Compilation completed"
echo ""

echo "=========================================="
echo "  Build completed successfully!"
echo "=========================================="
echo ""

# Ask if user wants to install
read -p "Do you want to install preload? (y/N): " response

case "$response" in
[yY] | [yY][eE][sS])
    echo ""
    echo "Installing preload (requires root permissions)..."
    sudo make install
    echo ""
    echo "✓ Preload installed successfully!"
    echo ""
    echo "To enable the service, run:"
    echo "  sudo systemctl enable preload"
    echo "  sudo systemctl start preload"
    ;;
*)
    echo ""
    echo "Installation cancelled."
    echo "To install manually, run: sudo make install"
    ;;
esac
