#!/bin/bash
# Installation script for preload-ng (precompiled binary)
# This script installs the precompiled binary from bin/

# Thanks to Aliel for testing the script and pointing out the issue with the service file!
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
INSTALL_BIN="/usr/local/sbin"
INSTALL_CONF="/usr/local/etc"
VAR_LIB="/usr/local/var/lib/preload"
SYSTEMD_DIR="/etc/systemd/system"
OPENRC_DIR="/etc/init.d"

# Find the bin directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BIN_DIR="$PROJECT_DIR/bin"

print_header() {
    echo -e "${BLUE}==========================================${NC}"
    echo -e "${BLUE}  Preload-NG - Binary Installation${NC}"
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

check_binary() {
    if [ ! -f "$BIN_DIR/preload" ]; then
        print_error "Precompiled binary not found at: $BIN_DIR/preload"
        echo ""
        echo "Please either:"
        echo "  1. Run 'bash build.sh' to compile from source"
        echo "  2. Download the precompiled binary from the releases page"
        exit 1
    fi

    if [ ! -f "$BIN_DIR/preload.conf" ]; then
        print_error "Configuration file not found at: $BIN_DIR/preload.conf"
        exit 1
    fi
}

detect_init_system() {
    if command -v systemctl &>/dev/null && systemctl --version &>/dev/null; then
        echo "systemd"
    elif command -v rc-service &>/dev/null; then
        echo "openrc"
    elif [ -f /etc/init.d/functions ]; then
        echo "sysvinit"
    else
        echo "unknown"
    fi
}

install_binary() {
    print_info "Installing binary to $INSTALL_BIN/preload..."
    mkdir -p "$INSTALL_BIN"
    cp "$BIN_DIR/preload" "$INSTALL_BIN/preload"
    chmod 755 "$INSTALL_BIN/preload"
    print_success "Binary installed"
}

install_config() {
    if [ -f "$INSTALL_CONF/preload.conf" ]; then
        print_warning "Configuration file already exists at $INSTALL_CONF/preload.conf"
        read -p "Overwrite? (y/N): " response
        case "$response" in
        [yY] | [yY][eE][sS])
            cp "$BIN_DIR/preload.conf" "$INSTALL_CONF/preload.conf"
            print_success "Configuration file overwritten"
            ;;
        *)
            print_info "Keeping existing configuration"
            ;;
        esac
    else
        print_info "Installing configuration to $INSTALL_CONF/preload.conf..."
        cp "$BIN_DIR/preload.conf" "$INSTALL_CONF/preload.conf"
        print_success "Configuration file installed"
    fi
    chmod 644 "$INSTALL_CONF/preload.conf"
}

create_directories() {
    print_info "Creating required directories..."
    mkdir -p "$VAR_LIB"
    chmod 755 "$VAR_LIB"

    # Create log directory if it doesn't exist
    if [ ! -d "/usr/local/var/log" ]; then
        mkdir -p "/usr/local/var/log"
        chmod 755 "/usr/local/var/log"
    fi

    print_success "Directories created"
}

install_systemd_service() {
    print_info "Installing systemd service..."

    cat >"$SYSTEMD_DIR/preload.service" <<'EOF'
[Unit]
Description=Preload Daemon - Adaptive Readahead
Documentation=man:preload(8)
After=local-fs.target

[Service]
Type=simple
ExecStart=/usr/local/sbin/preload --foreground --logfile ''
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5

# Security hardening
ProtectSystem=strict
ProtectHome=read-only
ReadWritePaths=/usr/local/var/lib/preload /var/log /usr/local/var/log
PrivateTmp=true
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

    chmod 644 "$SYSTEMD_DIR/preload.service"
    systemctl daemon-reload
    print_success "Systemd service installed"
}

install_openrc_service() {
    print_info "Installing OpenRC service..."

    cat >"$OPENRC_DIR/preload" <<'EOF'
#!/sbin/openrc-run

name="preload"
description="Preload Daemon - Adaptive Readahead"
command="/usr/local/sbin/preload"
command_args="--foreground"
command_background="yes"
pidfile="/run/${RC_SVCNAME}.pid"

depend() {
    need localmount
    after bootmisc
}
EOF

    chmod 755 "$OPENRC_DIR/preload"
    print_success "OpenRC service installed"
}

enable_service() {
    local init_system="$1"

    read -p "Enable and start preload service now? (Y/n): " response
    case "$response" in
    [nN] | [nN][oO])
        print_info "Service not enabled. You can enable it later with:"
        if [ "$init_system" = "systemd" ]; then
            echo "  sudo systemctl enable --now preload"
        elif [ "$init_system" = "openrc" ]; then
            echo "  sudo rc-update add preload default && sudo rc-service preload start"
        fi
        ;;
    *)
        if [ "$init_system" = "systemd" ]; then
            systemctl enable preload
            systemctl start preload
            print_success "Service enabled and started"
        elif [ "$init_system" = "openrc" ]; then
            rc-update add preload default
            rc-service preload start
            print_success "Service enabled and started"
        fi
        ;;
    esac
}

uninstall() {
    print_header
    echo "Uninstalling Preload-NG..."
    echo ""

    local init_system
    init_system=$(detect_init_system)

    # Stop and disable service
    if [ "$init_system" = "systemd" ]; then
        if systemctl is-active --quiet preload 2>/dev/null; then
            print_info "Stopping service..."
            systemctl stop preload
        fi
        if systemctl is-enabled --quiet preload 2>/dev/null; then
            print_info "Disabling service..."
            systemctl disable preload
        fi
        if [ -f "$SYSTEMD_DIR/preload.service" ]; then
            rm -f "$SYSTEMD_DIR/preload.service"
            systemctl daemon-reload
            print_success "Systemd service removed"
        fi
    elif [ "$init_system" = "openrc" ]; then
        if rc-service preload status &>/dev/null; then
            print_info "Stopping service..."
            rc-service preload stop
        fi
        rc-update del preload default 2>/dev/null || true
        if [ -f "$OPENRC_DIR/preload" ]; then
            rm -f "$OPENRC_DIR/preload"
            print_success "OpenRC service removed"
        fi
    fi

    # Remove binary
    if [ -f "$INSTALL_BIN/preload" ]; then
        rm -f "$INSTALL_BIN/preload"
        print_success "Binary removed"
    fi

    # Ask about config and data
    read -p "Remove configuration file ($INSTALL_CONF/preload.conf)? (y/N): " response
    case "$response" in
    [yY] | [yY][eE][sS])
        rm -f "$INSTALL_CONF/preload.conf"
        print_success "Configuration file removed"
        ;;
    *)
        print_info "Configuration file kept"
        ;;
    esac

    read -p "Remove state data ($VAR_LIB)? (y/N): " response
    case "$response" in
    [yY] | [yY][eE][sS])
        rm -rf "$VAR_LIB"
        print_success "State data removed"
        ;;
    *)
        print_info "State data kept"
        ;;
    esac

    echo ""
    print_success "Preload-NG uninstalled successfully!"
}

stop_preload() {
    # Stop preload service if running
    if [ "$init_system" = "systemd" ]; then
        if systemctl is-active --quiet preload 2>/dev/null; then
            print_info "Stopping running preload service..."
            systemctl stop preload
            print_success "Service stopped"
        fi
    elif [ "$init_system" = "openrc" ]; then
        if rc-service preload status &>/dev/null; then
            print_info "Stopping running preload service..."
            rc-service preload stop
            print_success "Service stopped"
        fi
    fi
}

install() {
    print_header
    check_root
    check_binary

    local init_system
    init_system=$(detect_init_system)

    echo "Detected init system: $init_system"
    echo ""

    if [ "$init_system" = "unknown" ]; then
        print_warning "Could not detect init system"
        print_info "Will install binary and config only (no service)"
        echo ""
    fi

    stop_preload

    # Install components
    install_binary
    install_config
    create_directories

    # Install service based on init system
    case "$init_system" in
    systemd)
        install_systemd_service
        echo ""
        enable_service "$init_system"
        ;;
    openrc)
        install_openrc_service
        echo ""
        enable_service "$init_system"
        ;;
    *)
        print_warning "No service installed. You'll need to start preload manually:"
        echo "  sudo /usr/local/sbin/preload --foreground"
        ;;
    esac

    echo ""
    echo -e "${GREEN}==========================================${NC}"
    echo -e "${GREEN}  Installation Complete!${NC}"
    echo -e "${GREEN}==========================================${NC}"
    echo ""
    echo "Binary location:  $INSTALL_BIN/preload"
    echo "Config location:  $INSTALL_CONF/preload.conf"
    echo "State directory:  $VAR_LIB"
    echo ""
    echo "To check status:  sudo systemctl status preload"
    echo "To view logs:     sudo journalctl -u preload -f"
    echo ""
}

show_help() {
    echo "Preload-NG Installation Script"
    echo ""
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  install     Install preload-ng (default)"
    echo "  uninstall   Remove preload-ng from the system"
    echo "  help        Show this help message"
    echo ""
}

# Main
case "${1:-install}" in
install)
    install
    ;;
uninstall)
    check_root
    uninstall
    ;;
help | --help | -h)
    show_help
    ;;
*)
    print_error "Unknown option: $1"
    show_help
    exit 1
    ;;
esac
