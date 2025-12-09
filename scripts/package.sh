#!/bin/bash
# Packaging script for preload-ng
# Creates a distributable tar.gz archive

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}==========================================${NC}"
    echo -e "${BLUE}  Preload-NG - Packaging Script${NC}"
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

find_project_dir() {
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    echo "$(dirname "$script_dir")"
}

validate_version() {
    local version="$1"

    if [[ -z "$version" ]]; then
        print_error "Version cannot be empty"
        return 1
    fi

    # Validate version format (allow numbers and dots)
    if [[ ! "$version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
        print_warning "Unusual version format: $version"
        read -p "Continue anyway? (y/N): " response
        case "$response" in
        [yY] | [yY][eE][sS])
            return 0
            ;;
        *)
            return 1
            ;;
        esac
    fi

    return 0
}

create_package() {
    local project_dir="$1"
    local version="$2"
    local project_name
    local file_name
    local parent_dir

    project_name=$(basename "$project_dir")
    file_name="preload-ng-${version}.tar.gz"
    parent_dir=$(dirname "$project_dir")

    print_info "Creating package: $file_name"
    echo ""

    # Go to parent directory
    cd "$parent_dir"

    # Create tar.gz file excluding unnecessary files
    # Create tar.gz file excluding unnecessary files
    tar --exclude='*.o' \
        --exclude='*.a' \
        --exclude='*.so' \
        --exclude='*.la' \
        --exclude='*.lo' \
        --exclude='.deps' \
        --exclude='.libs' \
        --exclude='*~' \
        --exclude='*.bak' \
        --exclude='*.swp' \
        --exclude='.git' \
        --exclude='.gitignore' \
        --exclude='*.tar.gz' \
        --exclude='result' \
        --exclude='preload.log' \
        --exclude='preload.state' \
        --exclude='core' \
        -czvf "$file_name" "$project_name"

    # Move to project directory
    mv "$file_name" "$project_dir/"

    print_success "Package created"
    echo ""
    echo "  File: $project_dir/$file_name"
    echo "  Size: $(du -h "$project_dir/$file_name" | cut -f1)"
}

show_help() {
    echo "Preload-NG Packaging Script"
    echo ""
    echo "Usage: $0 [OPTIONS] [VERSION]"
    echo ""
    echo "Options:"
    echo "  -v, --version VERSION  Specify version (e.g., 0.6.6)"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                     # Prompt for version"
    echo "  $0 -v 0.6.6            # Create preload-ng-0.6.6.tar.gz"
    echo "  $0 0.6.7               # Create preload-ng-0.6.7.tar.gz"
    echo ""
}

# Parse arguments
VERSION=""

while [[ $# -gt 0 ]]; do
    case $1 in
    -v | --version)
        VERSION="$2"
        shift 2
        ;;
    -h | --help)
        show_help
        exit 0
        ;;
    -*)
        print_error "Unknown option: $1"
        show_help
        exit 1
        ;;
    *)
        VERSION="$1"
        shift
        ;;
    esac
done

# Main
print_header

PROJECT_DIR=$(find_project_dir)
print_info "Project directory: $PROJECT_DIR"
echo ""

# Ask for version if not provided
if [ -z "$VERSION" ]; then
    read -p "Enter version (e.g., 0.6.6): " VERSION
fi

# Validate version
if ! validate_version "$VERSION"; then
    exit 1
fi

echo ""
create_package "$PROJECT_DIR" "$VERSION"

echo ""
echo -e "${GREEN}==========================================${NC}"
echo -e "${GREEN}  Packaging Complete!${NC}"
echo -e "${GREEN}==========================================${NC}"
echo ""
