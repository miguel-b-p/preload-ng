/* madvise_utils.h - Memory advice utilities for page management
 *
 * Copyright (C) 2024  Preload Contributors
 *
 * This file is part of preload.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef MADVISE_UTILS_H
#define MADVISE_UTILS_H

#include <sys/mman.h>
#include <stddef.h>

/*
 * Page evacuation strategies.
 * 
 * MADV_DONTNEED: Immediately discards pages. Suitable for anonymous memory.
 *                WARNING: On MAP_SHARED, this can cause DATA LOSS.
 *
 * MADV_FREE:     Lazy free - kernel reclaims when needed (Linux 4.5+).
 *                More efficient but requires newer kernel.
 *
 * For file-backed pages, consider posix_fadvise(POSIX_FADV_DONTNEED) instead.
 */

/* Check if MADV_FREE is available (Linux 4.5+) */
#ifndef MADV_FREE
#define MADV_FREE 8
#endif

/*
 * preload_evacuate_pages - Release unneeded pages from memory
 *
 * @addr:   Start address of region (page-aligned)
 * @length: Length of region in bytes
 * @lazy:   If true, use MADV_FREE (lazy). If false, use MADV_DONTNEED (immediate).
 *
 * WARNINGS:
 * - Never use on MAP_SHARED with unsaved data
 * - Pages with mlock() are immune and remain resident
 * - On anonymous memory, subsequent access causes zero-fill page fault
 *
 * Returns: 0 on success, -1 on error (check errno)
 */
int preload_evacuate_pages(void *addr, size_t length, int lazy);

/*
 * preload_evacuate_file_pages - Release file pages from page cache
 *
 * Uses posix_fadvise(POSIX_FADV_DONTNEED) which is safer for file-backed
 * pages than madvise variants.
 *
 * @fd:     File descriptor
 * @offset: Start offset in file
 * @length: Length of region (0 for entire file from offset)
 *
 * Returns: 0 on success, error code on failure
 */
int preload_evacuate_file_pages(int fd, off_t offset, off_t length);

/*
 * preload_check_madv_free_support - Check if MADV_FREE is supported
 *
 * MADV_FREE requires Linux 4.5+. This function tests availability.
 *
 * Returns: 1 if supported, 0 if not
 */
int preload_check_madv_free_support(void);

#endif /* MADVISE_UTILS_H */
