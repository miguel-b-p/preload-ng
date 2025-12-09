/* time_utils.c - Time tracking utilities with hibernate/suspend awareness
 *
 * Copyright (C) 2024  Preload Contributors
 *
 * This file is part of preload.
 */

#include "common.h"
#include "time_utils.h"
#include "log.h"

/* Check CLOCK_BOOTTIME availability - Linux 2.6.39+ */
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif

/* Static flag for CLOCK_BOOTTIME support (cached after first check) */
static int boottime_checked = 0;
static int boottime_supported = 0;

int
preload_check_boottime_support(void)
{
  struct timespec ts;

  if (boottime_checked) {
    return boottime_supported;
  }

  boottime_checked = 1;

  /* Try CLOCK_BOOTTIME - returns EINVAL on older kernels */
  if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) {
    boottime_supported = 1;
    g_debug("CLOCK_BOOTTIME is supported - time tracking includes suspend/hibernate");
  } else {
    boottime_supported = 0;
    g_debug("CLOCK_BOOTTIME not supported - falling back to CLOCK_MONOTONIC");
  }

  return boottime_supported;
}

int64_t
preload_get_boottime(void)
{
  struct timespec ts;
  clockid_t clock_id;

  clock_id = preload_check_boottime_support() ? CLOCK_BOOTTIME : CLOCK_MONOTONIC;

  if (clock_gettime(clock_id, &ts) != 0) {
    g_warning("clock_gettime failed: %s", strerror(errno));
    return -1;
  }

  return (int64_t)ts.tv_sec;
}

int64_t
preload_get_boottime_ms(void)
{
  struct timespec ts;
  clockid_t clock_id;

  clock_id = preload_check_boottime_support() ? CLOCK_BOOTTIME : CLOCK_MONOTONIC;

  if (clock_gettime(clock_id, &ts) != 0) {
    g_warning("clock_gettime failed: %s", strerror(errno));
    return -1;
  }

  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
