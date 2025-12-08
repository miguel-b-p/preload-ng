# Preload-NG Documentation

This directory contains documentation for the Preload-NG project.

---

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [How It Works](#how-it-works)
4. [Configuration](#configuration)
5. [Troubleshooting](#troubleshooting)
6. [Technical Details](#technical-details)

---

## Overview

Preload is an adaptive readahead daemon that monitors application usage and prefetches files from disk into memory to reduce startup times.

### Key Features

- **Intelligent Prediction**: Uses Markov chains to predict which applications will be launched
- **Low Overhead**: Runs quietly in the background with minimal resource usage
- **Adaptive Learning**: Continuously learns from your usage patterns
- **Desktop Agnostic**: Works with any desktop environment or window manager

---

## Installation

### Using Precompiled Binary

```bash
git clone https://github.com/miguel-b-p/preload-ng.git
cd preload-ng/scripts
sudo bash install.sh
```

### Building from Source

```bash
git clone https://github.com/miguel-b-p/preload-ng.git
cd preload-ng/scripts
bash build.sh
```

### Using Nix Flakes

```bash
# Build the package
nix build github:miguel-b-p/preload-ng

# Run directly
nix run github:miguel-b-p/preload-ng

# Enter development shell
nix develop github:miguel-b-p/preload-ng
```

#### NixOS Configuration

Add to your `flake.nix`:

```nix
{
  inputs.preload-ng.url = "github:miguel-b-p/preload-ng";

  outputs = { self, nixpkgs, preload-ng, ... }: {
    nixosConfigurations.your-hostname = nixpkgs.lib.nixosSystem {
      modules = [
        preload-ng.nixosModules.default
        {
          services.preload-ng.enable = true;
        }
      ];
    };
  };
}
```

##### NixOS Declarative Configuration

All settings from `preload.conf` are available as NixOS options:

```nix
{
  services.preload-ng = {
    enable = true;
    debug = false;             # Enable verbose debug output (default: false)
    usePrecompiled = true;     # Use precompiled binary (true) or compile from source (false)
    settings = {
      # Model settings
      cycle = 20;              # Time quantum in seconds
      useCorrelation = true;   # Use correlation in predictions
      minSize = 2000000;       # Minimum map size to track (bytes)

      # Memory thresholds (percentages, -100 to 100)
      memTotal = -10;
      memFree = 50;
      memCached = 0;
      memBuffers = 50;

      # System settings
      doScan = true;           # Monitor running processes
      doPredict = true;        # Enable prefetching
      autoSave = 3600;         # Auto-save period (seconds)

      # File filtering
      mapPrefix = "/usr/;/lib;/var/cache/;!/";
      exePrefix = "!/usr/sbin/;!/usr/local/sbin/;/usr/;!/";

      # I/O settings
      processes = 30;          # Parallel readahead processes
      sortStrategy = 3;        # 0=none, 1=path, 2=inode, 3=block
    };
  };
}
```

See [Configuration](#configuration) below for detailed explanations of each option.

---

## How It Works

### Data Collection

Preload periodically scans `/proc` to gather information about:

- Currently running processes
- Shared libraries (`.so` files) mapped by each process
- Memory access patterns

### Prediction Model

The daemon builds a **Markov chain model** that tracks:

- Which applications are commonly run together
- Temporal correlations between application launches
- Probability of each application being launched next

### Prefetching

Based on predictions, preload uses `readahead(2)` system call to:

- Load binary executables into page cache
- Preload shared libraries
- Prepare files that applications typically access on startup

---

## Configuration

The main configuration file is `/etc/preload.conf`.

### Complete Configuration Reference

```ini
###############################################################################
#                              [model] SECTION
#         Controls prediction algorithm and memory usage behavior
###############################################################################

[model]

# cycle (seconds)
# The quantum of time for preload. Data gathering and predictions happen
# every cycle. Use an even number.
# WARNING: Setting too low may reduce system performance and stability.
# Default: 20
cycle = 20

# usecorrelation (true/false)
# Whether to use correlation coefficient in the prediction algorithm.
# Using it typically results in more accurate predictions.
# Default: true
usecorrelation = true

# minsize (bytes)
# Minimum sum of mapped memory for preload to track an application.
# Too high = less effective predictions
# Too low = uses quadratically more resources tracking small processes
# Default: 2000000 (2MB)
minsize = 2000000

###############################################################################
#                         MEMORY THRESHOLDS
#
# Controls how much memory preload can use for prefetching.
# All values are percentages (-100 to 100).
#
# Formula: max(0, TOTAL * memtotal + FREE * memfree) + CACHED * memcached
#
# Where TOTAL, FREE, CACHED are read from /proc/meminfo at runtime.
###############################################################################

# memtotal (percentage of total RAM)
# Negative values effectively subtract from the budget.
# Example: -10 means "subtract 10% of total RAM from budget"
# Default: -10
memtotal = -10

# memfree (percentage of free RAM)
# How much of currently free memory preload can use.
# Default: 50
memfree = 50

# memcached (percentage of cached RAM)
# How much of cached memory to consider available.
# Cached memory contains file data that may still be in use.
# Default: 0 (conservative - don't touch cached)
memcached = 0

# membuffers (percentage of buffer RAM)
# Buffer memory contains filesystem metadata (inodes, directories).
# Unlike cached, buffers are typically clean and safely reclaimable.
# Default: 50
membuffers = 50

###############################################################################
#                             [system] SECTION
#               Controls daemon behavior and I/O operations
###############################################################################

[system]

# doscan (true/false)
# Whether to monitor running processes and update the model.
# Turn off temporarily for testing predictions without learning.
# Default: true
doscan = true

# dopredict (true/false)
# Whether to make predictions and prefetch from disk.
# Turn off to only train the model without prefetching.
# Can be toggled on-the-fly by modifying config and sending SIGHUP.
# Default: true
dopredict = true

# autosave (seconds)
# How often to automatically save state to disk.
# State save also performs cleanup of deleted files from the model.
# WARNING: Don't disable completely - cleanup only happens on save.
# Default: 3600 (1 hour)
autosave = 3600

###############################################################################
#                           FILE FILTERING
#
# Prefix matching rules for which files to consider.
# Items separated by semicolons (;)
# Matching stops at first match.
# Prefix with ! to reject instead of accept.
# If no match occurs, file is accepted.
#
# Example: !/lib/modules;/
#   = reject /lib/modules/*, accept everything else
#
# Note: /lib matches /lib, /lib64, /libexec
#       Use /lib/ for exact match
###############################################################################

# mapprefix (semicolon-separated paths)
# Which shared libraries/mapped files to consider.
# Recommended to exclude /dev (preload doesn't handle device files).
# Default: /usr/;/lib;/var/cache/;!/
mapprefix = /usr/;/lib;/var/cache/;!/

# exeprefix (semicolon-separated paths)
# Which executables to consider.
# Default: !/usr/sbin/;!/usr/local/sbin/;/usr/;!/
exeprefix = !/usr/sbin/;!/usr/local/sbin/;/usr/;!/

###############################################################################
#                           I/O OPTIMIZATION
###############################################################################

# processes (integer)
# Maximum parallel readahead processes.
# 0 = no parallelism (all readahead in main process)
# Parallel readahead allows kernel to batch nearby I/O requests.
# Default: 30
processes = 30

# sortstrategy (0-3)
# How to sort I/O requests for optimal disk access:
#
#   0 = SORT_NONE   - No sorting. Best for Flash/SSD storage.
#   1 = SORT_PATH   - Sort by file path. Best for network filesystems.
#   2 = SORT_INODE  - Sort by inode number. Less housekeeping I/O.
#   3 = SORT_BLOCK  - Sort by disk block. Most sophisticated.
#                     Best for traditional HDDs and most Linux filesystems.
#
# Default: 3 (SORT_BLOCK)
sortstrategy = 3
```

---

## Troubleshooting

### Checking Status

```bash
# View daemon status
systemctl status preload

# Check log file
tail -f /var/log/preload

# View state file
cat /var/lib/preload/preload.state
```

### Running Manually

```bash
# Run with verbose output
preload -v 5

# Run in foreground (no fork)
preload -f

# Specify alternate config file
preload -c /path/to/config
```

### Common Issues

| Issue                           | Solution                                  |
| ------------------------------- | ----------------------------------------- |
| High disk activity after boot   | Normal for first few boots while learning |
| No improvement in startup times | Ensure apps have been run at least twice  |
| Excessive memory usage          | Reduce `memfree` in config file           |

---

## Technical Details

### State File Format

The state file (`/var/lib/preload/preload.state`) contains:

- List of executables with usage statistics
- Shared library mappings
- Markov chain transition probabilities
- Timestamps for each entry

### Memory Management

Preload respects system memory limits:

1. Checks available memory before prefetching
2. Uses `ionice` for low I/O priority
3. Can be configured to use only idle I/O scheduler

### Signals

| Signal    | Action                         |
| --------- | ------------------------------ |
| `SIGHUP`  | Reload configuration           |
| `SIGUSR1` | Dump debug info to log         |
| `SIGUSR2` | Save state immediately         |
| `SIGTERM` | Clean shutdown with state save |

---

## Files Reference

| Path                             | Description         |
| -------------------------------- | ------------------- |
| `/usr/sbin/preload`              | Daemon executable   |
| `/etc/preload.conf`              | Configuration file  |
| `/var/lib/preload/preload.state` | Learning database   |
| `/var/log/preload`               | Log file            |
| `/etc/logrotate.d/preload`       | Log rotation config |

---

## See Also

- [README.md](../README.md) — Project overview and credits
- [CHANGELOG](../changelogs/0.6.6.md) — Changelog for this fork
- [proposal.txt](../preload-src/doc/proposal.txt) — Original GSoC proposal
- `man preload` — Manual page
