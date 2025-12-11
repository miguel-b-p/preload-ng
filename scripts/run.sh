#!/bin/bash
# Script to run preload-ng from source
# Usage: sudo ./scripts/run.sh

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${BLUE}→ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

# Determine project root
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
# Based on the user's path structure: preload-ng/preload-src/preload
SRC_DIR="$PROJECT_ROOT/preload-src"

BINARY="$SRC_DIR/preload"
CONF_FILE="$SRC_DIR/preload.conf"
LOG_FILE="$SRC_DIR/preload.log"
STATE_FILE="$SRC_DIR/preload.state"

check_files() {
    if [ ! -f "$BINARY" ]; then
        print_error "Binary not found at $BINARY"
        print_info "Please build the project first."
        exit 1
    fi

    if [ ! -f "$CONF_FILE" ]; then
        echo -e "${YELLOW}! Config file not found at $CONF_FILE (preload might complain)${NC}"
    fi
}

run_preload() {
    print_info "Starting preload-ng..."
    print_info "Binary: $BINARY"
    print_info "Config: $CONF_FILE"
    print_info "Log:    $LOG_FILE"
    print_info "State:  $STATE_FILE"
    print_info "Mode:   Foreground"
    print_info "Command: $BINARY -f -c $CONF_FILE -l $LOG_FILE -s $STATE_FILE -d"
    echo ""

    # Executing binary
    # -f: run in foreground
    # -c: config file
    # -l: log file
    # -s: state file
    # -d: debug mode
    "$BINARY" -f \
        -c "$CONF_FILE" \
        -l "$LOG_FILE" \
        -s "$STATE_FILE" \
        -d

}

# Main
check_root
check_files
run_preload
