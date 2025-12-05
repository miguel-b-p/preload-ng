#!/bin/bash
# Script to package preload-ng as tar.gz
# Asks for version and creates file with appropriate name

set -e

echo "=========================================="
echo "  Preload-NG - Packaging Script"
echo "=========================================="
echo ""

# Ask for version
read -p "Enter version (example: 0.6.6): " version

# Validate input
if [[ -z "$version" ]]; then
    echo "Error: Version cannot be empty."
    exit 1
fi

# Validate version format (allow numbers and dots)
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    echo "Warning: Unusual version format. Continuing..."
fi

# Define file name
file_name="preload-ng-${version}.tar.gz"
current_directory=$(basename "$(pwd)")
parent_directory=$(dirname "$(pwd)")

echo ""
echo "Creating file: $file_name"
echo ""

# Go to parent directory and create tar.gz
cd "$parent_directory"

# Create tar.gz file excluding unnecessary files
tar --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.so' \
    --exclude='*.la' \
    --exclude='*.lo' \
    --exclude='.deps' \
    --exclude='.libs' \
    --exclude='autom4te.cache' \
    --exclude='config.h' \
    --exclude='config.log' \
    --exclude='config.status' \
    --exclude='stamp-h1' \
    --exclude='libtool' \
    --exclude='*~' \
    --exclude='*.bak' \
    --exclude='*.swp' \
    --exclude='.git' \
    --exclude='.gitignore' \
    -czvf "$file_name" "$current_directory"

# Move to original directory
mv "$file_name" "$current_directory/"

cd "$current_directory"

echo ""
echo "=========================================="
echo "  Packaging completed!"
echo "=========================================="
echo ""
echo "File created: $(pwd)/$file_name"
echo "Size: $(du -h "$file_name" | cut -f1)"
echo ""
