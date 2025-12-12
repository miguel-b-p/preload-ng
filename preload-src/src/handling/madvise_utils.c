/* madvise_utils.c - Memory advice utilities for page management
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

#include "common.h"
#include "madvise_utils.h"
#include "log.h"

#include <fcntl.h>

/* Static flag for MADV_FREE support (cached after first check) */
static int madv_free_checked = 0;
static int madv_free_supported = 0;

int
preload_check_madv_free_support(void)
{
  void *test_addr;
  size_t page_size;
  
  if (madv_free_checked) {
    return madv_free_supported;
  }
  
  madv_free_checked = 1;
  page_size = getpagesize();
  
  /* Allocate anonymous page for testing */
  test_addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (test_addr == MAP_FAILED) {
    madv_free_supported = 0;
    return 0;
  }
  
  /* Touch the page to make it resident */
  *(volatile char *)test_addr = 0;
  
  /* Try MADV_FREE - returns EINVAL on kernels < 4.5 */
  if (madvise(test_addr, page_size, MADV_FREE) == 0) {
    madv_free_supported = 1;
    g_debug("MADV_FREE is supported on this kernel");
  } else {
    madv_free_supported = 0;
    g_debug("MADV_FREE not supported (kernel < 4.5), using MADV_DONTNEED");
  }
  
  munmap(test_addr, page_size);
  return madv_free_supported;
}

int
preload_evacuate_pages(void *addr, size_t length, int lazy)
{
  int advice;
  int ret;
  
  /*
   * Choose evacuation strategy:
   * - MADV_FREE (lazy=1): Pages marked as lazy-free, reclaimed when needed
   * - MADV_DONTNEED (lazy=0): Pages immediately discarded
   *
   * WARNING: Both destroy contents of anonymous memory!
   * For MAP_SHARED, use only if data is already synced to disk.
   */
  
  if (lazy && preload_check_madv_free_support()) {
    advice = MADV_FREE;
  } else {
    advice = MADV_DONTNEED;
  }
  
  ret = madvise(addr, length, advice);
  if (ret < 0) {
    g_debug("madvise(%s) failed: %s",
            advice == MADV_FREE ? "MADV_FREE" : "MADV_DONTNEED",
            strerror(errno));
  }
  
  return ret;
}

int
preload_evacuate_file_pages(int fd, off_t offset, off_t length)
{
  int ret;
  
  /*
   * posix_fadvise(POSIX_FADV_DONTNEED):
   * - Tells kernel the file data is no longer needed
   * - Kernel may drop pages from page cache (not guaranteed)
   * - Safe for file-backed mappings (no data loss)
   * - Does NOT affect dirty pages (they'll be written first)
   *
   * This is the preferred method for evicting file pages.
   */
  
#ifdef POSIX_FADV_DONTNEED
  ret = posix_fadvise(fd, offset, length, POSIX_FADV_DONTNEED);
  if (ret != 0) {
    g_debug("posix_fadvise(POSIX_FADV_DONTNEED) failed: %s", strerror(ret));
  }
  return ret;
#else
  /* Fallback: posix_fadvise not available */
  (void)fd;
  (void)offset;
  (void)length;
  return ENOSYS;
#endif
}
