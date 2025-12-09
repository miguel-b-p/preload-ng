/* time_utils.h - Time tracking utilities with hibernate/suspend awareness
 *
 * Copyright (C) 2024  Preload Contributors
 *
 * This file is part of preload.
 */

#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>
#include <stdint.h>

/*
 * preload_get_boottime - Get time since boot including suspend/hibernate
 *
 * Uses CLOCK_BOOTTIME when available (Linux 2.6.39+), which continues to
 * advance during system suspend/hibernate. Falls back to CLOCK_MONOTONIC
 * on older kernels.
 *
 * This is essential for accurate time tracking across suspend cycles.
 *
 * Edge cases:
 * - VM migration may cause time jumps
 * - Clock drift after long hibernation
 *
 * Returns: Seconds since boot (including suspend time), or -1 on error
 */
int64_t preload_get_boottime(void);

/*
 * preload_get_boottime_ms - Same as above but in milliseconds
 *
 * Returns: Milliseconds since boot, or -1 on error
 */
int64_t preload_get_boottime_ms(void);

/*
 * preload_check_boottime_support - Check if CLOCK_BOOTTIME is available
 *
 * Returns: 1 if CLOCK_BOOTTIME supported, 0 if falling back to MONOTONIC
 */
int preload_check_boottime_support(void);

#endif /* TIME_UTILS_H */
