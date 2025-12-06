#!/bin/bash
# Benchmark script for preload-ng
# Compares application startup time with and without preload

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVICE_NAME="preload-ng"
DEFAULT_APP="gimp"
WARMUP_RUNS=1
BENCHMARK_RUNS=10
PRELOAD_WAIT=40

print_header() {
    echo -e "${BLUE}==========================================${NC}"
    echo -e "${BLUE}  Preload-NG - Benchmark Script${NC}"
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

check_root() {
    if [ "$EUID" -ne 0 ]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

check_dependencies() {
    print_info "Checking dependencies..."

    local missing=()

    if ! command -v hyperfine &>/dev/null; then
        missing+=("hyperfine")
    fi

    if ! command -v xdotool &>/dev/null; then
        missing+=("xdotool")
    fi

    if [ ${#missing[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing[*]}"
        echo ""
        echo "Please install them first:"
        echo "  Debian/Ubuntu: sudo apt install ${missing[*]}"
        echo "  Fedora: sudo dnf install ${missing[*]}"
        echo "  Arch: sudo pacman -S ${missing[*]}"
        exit 1
    fi

    print_success "All dependencies found"
}

clear_cache() {
    print_info "Clearing system caches..."

    # Sync all pending buffers to disk
    sync

    # Completely clear page cache, dentries and inodes
    # 1 = pagecache, 2 = dentries/inodes, 3 = both
    echo 3 | tee /proc/sys/vm/drop_caches >/dev/null

    # Release swap if available (forces pages in swap to return and be discarded)
    swapoff -a 2>/dev/null && swapon -a 2>/dev/null || true

    print_success "Cache cleared"
}

run_benchmark() {
    local app="$1"
    local window_class="$2"

    echo ""
    print_info "Running benchmark for: $app"
    echo ""

    hyperfine --warmup "$WARMUP_RUNS" --runs "$BENCHMARK_RUNS" \
        --prepare 'sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null; sleep 2' \
        "$app & xdotool search --sync --onlyvisible --class \"$window_class\" windowkill"
}

benchmark_without_preload() {
    echo ""
    echo -e "${YELLOW}==========================================${NC}"
    echo -e "${YELLOW}  BENCHMARK WITHOUT PRELOAD${NC}"
    echo -e "${YELLOW}==========================================${NC}"
    echo ""

    print_info "Stopping preload service..."
    systemctl stop "$SERVICE_NAME" 2>/dev/null || print_warning "Service not running"

    clear_cache

    print_info "Waiting for system to stabilize (5s)..."
    sleep 5

    run_benchmark "$APP" "$APP_CLASS"
}

benchmark_with_preload() {
    echo ""
    echo -e "${GREEN}==========================================${NC}"
    echo -e "${GREEN}  BENCHMARK WITH PRELOAD${NC}"
    echo -e "${GREEN}==========================================${NC}"
    echo ""

    print_info "Starting preload service..."
    systemctl start "$SERVICE_NAME"
    print_success "Preload service started"

    echo ""
    print_info "Waiting for preload to learn patterns (${PRELOAD_WAIT}s = 2 cycles)..."
    sleep "$PRELOAD_WAIT"

    run_benchmark "$APP" "$APP_CLASS"
}

show_help() {
    echo "Preload-NG Benchmark Script"
    echo ""
    echo "Usage: sudo $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -a, --app APP       Application to benchmark (default: $DEFAULT_APP)"
    echo "  -c, --class CLASS   Window class for xdotool (default: same as app)"
    echo "  -r, --runs N        Number of benchmark runs (default: $BENCHMARK_RUNS)"
    echo "  -w, --wait N        Seconds to wait for preload (default: $PRELOAD_WAIT)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  sudo $0                           # Benchmark GIMP"
    echo "  sudo $0 -a firefox -c Navigator   # Benchmark Firefox"
    echo "  sudo $0 -a libreoffice --calc     # Benchmark LibreOffice Calc"
    echo ""
}

# Parse arguments
APP="$DEFAULT_APP"
APP_CLASS=""

while [[ $# -gt 0 ]]; do
    case $1 in
    -a | --app)
        APP="$2"
        shift 2
        ;;
    -c | --class)
        APP_CLASS="$2"
        shift 2
        ;;
    -r | --runs)
        BENCHMARK_RUNS="$2"
        shift 2
        ;;
    -w | --wait)
        PRELOAD_WAIT="$2"
        shift 2
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

# Default class to app name if not specified
if [ -z "$APP_CLASS" ]; then
    APP_CLASS="$APP"
fi

# Main
print_header
check_root
check_dependencies

echo "Configuration:"
echo "  Application:     $APP"
echo "  Window class:    $APP_CLASS"
echo "  Benchmark runs:  $BENCHMARK_RUNS"
echo "  Preload wait:    ${PRELOAD_WAIT}s"
echo ""

read -p "Start benchmark? (Y/n): " response
case "$response" in
[nN] | [nN][oO])
    echo "Benchmark cancelled."
    exit 0
    ;;
esac

benchmark_without_preload
benchmark_with_preload

echo ""
echo -e "${GREEN}==========================================${NC}"
echo -e "${GREEN}  Benchmark Complete!${NC}"
echo -e "${GREEN}==========================================${NC}"
echo ""
print_info "Compare the 'Mean' times to see the improvement."
echo ""
