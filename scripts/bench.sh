#!/bin/bash
# benchmark_preload.sh

echo "=== BENCHMARK WITHOUT PRELOAD ==="
# Stop preload and remove all traces
sudo systemctl stop preload

# Sync all pending buffers to disk
sync

# Completely clear page cache, dentries and inodes
# 1 = pagecache, 2 = dentries/inodes, 3 = both
echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null

# Release swap if available (forces pages in swap to return and be discarded)
sudo swapoff -a 2>/dev/null && sudo swapon -a 2>/dev/null

# Wait for system to stabilize
sleep 5

echo "Cache cleared. Starting baseline benchmark..."
hyperfine --warmup 1 --runs 10 \
    --prepare 'sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null; sleep 2' \
    'libreoffice --calc & xdotool search --sync --onlyvisible --class "libreoffice-calc" windowkill'

echo ""
echo "=== BENCHMARK WITH PRELOAD ==="
sudo systemctl start preload

# Wait for preload to start and make first predictions
echo "Waiting for preload to learn patterns (40s = 2 cycles)..."
sleep 40

hyperfine --warmup 1 --runs 10 \
    --prepare 'sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null; sleep 2' \
    'libreoffice --calc & xdotool search --sync --onlyvisible --class "libreoffice-calc" windowkill'
