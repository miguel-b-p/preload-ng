#!/bin/bash
# Build script for preload-ng
# Compiles preload from source and optionally installs it

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}==========================================${NC}"
    echo -e "${BLUE}  Preload-NG - Build Script${NC}"
    echo -e "${BLUE}==========================================${NC}"
    echo ""
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}! $1${NC}"
}

print_info() {
    echo -e "${BLUE}→ $1${NC}"
}

check_command() {
    if ! command -v "$1" &>/dev/null; then
        print_error "'$1' not found. Please install it first."
        return 1
    fi
    return 0
}

check_dependencies() {
    print_info "Checking build dependencies..."
    echo ""

    local missing=()

    check_command automake || missing+=("automake")
    check_command autoconf || missing+=("autoconf")
    check_command libtool || missing+=("libtool")
    check_command make || missing+=("make")
    check_command gcc || missing+=("gcc")

    if [ ${#missing[@]} -ne 0 ]; then
        echo ""
        print_error "Missing dependencies: ${missing[*]}"
        echo ""
        echo "Please install them first:"
        echo "  Debian/Ubuntu: sudo apt install ${missing[*]} libglib2.0-dev"
        echo "  Fedora: sudo dnf install ${missing[*]} glib2-devel"
        echo "  Arch: sudo pacman -S ${missing[*]} glib2"
        exit 1
    fi

    print_success "All build dependencies found"
}

find_source_dir() {
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

    if [ -d "$script_dir/../preload-src" ]; then
        echo "$script_dir/../preload-src"
    elif [ -d "./preload-src" ]; then
        echo "./preload-src"
    elif [ -d "../preload-src" ]; then
        echo "../preload-src"
    else
        echo ""
    fi
}

clean_build() {
    if [ -f Makefile ]; then
        print_info "Cleaning previous build..."
        make distclean 2>/dev/null || make clean 2>/dev/null || true
        print_success "Previous build cleaned"
    fi
}

build() {
    print_info "[1/3] Configuring build system..."
    ./bootstrap
    ./configure
    print_success "Build system configured"

    echo ""
    print_info "[2/3] Compiling..."
    make -j"$(nproc)"
    print_success "Compilation complete"

    echo ""
    print_info "[3/3] Build finished!"
}

install_preload() {
    echo ""
    read -p "Do you want to install preload? (y/N): " response

    case "$response" in
    [yY] | [yY][eE][sS])
        echo ""
        print_info "Installing preload (requires root permissions)..."
        sudo make install
        print_success "Preload installed"

        echo ""
        read -p "Enable and start preload service? (Y/n): " enable_response
        case "$enable_response" in
        [nN] | [nN][oO])
            print_info "Service not enabled"
            ;;
        *)
            sudo systemctl enable --now preload
            print_success "Service enabled and started"
            ;;
        esac
        ;;
    *)
        echo ""
        print_info "Installation skipped"
        echo "To install manually, run: sudo make install"
        ;;
    esac
}

show_help() {
    echo "Preload-NG Build Script"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --clean       Clean build only (no compilation)"
    echo "  --no-install  Build without prompting to install"
    echo "  -h, --help    Show this help message"
    echo ""
}

# Parse arguments
CLEAN_ONLY=false
NO_INSTALL=false

while [[ $# -gt 0 ]]; do
    case $1 in
    --clean)
        CLEAN_ONLY=true
        shift
        ;;
    --no-install)
        NO_INSTALL=true
        shift
        ;;
    -h | --help)
        show_help
        exit 0
        ;;
    *)
        print_error "Unknown option: $1"
        show_help
        exit 1
        ;;
    esac
done

# Main
print_header
check_dependencies

# Find and enter source directory
SOURCE_DIR=$(find_source_dir)

if [ -z "$SOURCE_DIR" ]; then
    print_error "preload-src directory not found"
    exit 1
fi

print_info "Source directory: $SOURCE_DIR"
cd "$SOURCE_DIR"

if [ "$CLEAN_ONLY" = true ]; then
    clean_build
    echo ""
    print_success "Clean complete!"
    exit 0
fi

clean_build
echo ""
build

echo ""
echo -e "${GREEN}==========================================${NC}"
echo -e "${GREEN}  Build Completed Successfully!${NC}"
echo -e "${GREEN}==========================================${NC}"

if [ "$NO_INSTALL" = false ]; then
    install_preload
fi

echo ""
