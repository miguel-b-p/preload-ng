/* readahead.c - read in advance a list of files, adding them to the page cache
 *
 * Copyright (C) 2005,2008  Behdad Esfahbod
 *
 * This file is part of preload.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#include "common.h"
#include "readahead.h"
#include "log.h"
#include "conf.h"

#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/fs.h>

static void
set_block(preload_map_t *file, gboolean use_inode)
{
  int fd = -1;
  int block = 0;
  struct stat buf;

  /* in case we can get block, set to 0 to not retry */
  file->block = 0;

  fd = open(file->path, O_RDONLY);
  if (fd < 0)
    return;
  
  if (0 > fstat (fd, &buf)) {
    close(fd);
    return;
  }

#ifdef FIBMAP
  if (!use_inode)
    {
      block = file->offset / buf.st_blksize;
      if (0 > ioctl (fd, FIBMAP, &block))
	block = 0;
    }
#endif

  /* fall back to inode number */
  block = buf.st_ino;

  file->block = block;

  close (fd);
}


/* Compare files by path */
static int
map_path_compare (const preload_map_t **pa, const preload_map_t **pb)
{
  const preload_map_t *a = *pa, *b = *pb;
  int i;
  
  i = strcmp(a->path, b->path);
  if (!i) /* same file - use safe comparison to avoid overflow */
    i = (a->offset > b->offset) - (a->offset < b->offset);
  if (!i) /* same offset?! */
    i = (b->length > a->length) - (b->length < a->length);

  return i;
}

/* Compare files by block */
static int
map_block_compare (const preload_map_t **pa, const preload_map_t **pb)
{
  const preload_map_t *a = *pa, *b = *pb;
  int i;
  
  /* Use safe comparison to avoid integer overflow */
  i = (a->block > b->block) - (a->block < b->block);
  if (!i) /* no block? */
    i = strcmp(a->path, b->path);
  if (!i) /* same file */
    i = (a->offset > b->offset) - (a->offset < b->offset);
  if (!i) /* same offset?! */
    i = (b->length > a->length) - (b->length < a->length);

  return i;
}

static int procs = 0;

static void
wait_for_children (void)
{
  /* wait for child processes to terminate */
  while (procs > 0)
    {
      int status;
      if (wait (&status) > 0)
	procs--;
    }
}

/*
 * try_readahead_with_fallback - Attempt to prefetch file data into page cache
 *
 * This function first tries the readahead(2) syscall. If that fails with
 * EINVAL or ENOSYS (unsupported filesystem or older kernel), it falls back
 * to mmap()+madvise(MADV_WILLNEED)+munmap().
 *
 * IMPORTANT: readahead(2) is ADVISORY - the kernel may ignore the request
 * under memory pressure. This is by design per the Linux kernel documentation.
 *
 * Edge cases handled:
 * - Files in /proc/ and /sys/ cannot use madvise fallback (mmap fails)
 * - NFS has limited readahead support, fallback helps
 * - Page alignment is required for madvise
 *
 * Returns: 0 on success, -1 on failure
 */
static int
try_readahead_with_fallback(int fd, off_t offset, size_t length)
{
  int ret;
  void *addr;
  size_t page_size;
  off_t aligned_offset;
  size_t aligned_length;

  /* Try readahead first - it's faster when supported */
  ret = readahead(fd, offset, length);
  if (ret == 0) {
    return 0; /* Success */
  }
  
  /* readahead failed - check if we should try fallback */
  if (errno != EINVAL && errno != ENOSYS && errno != EOPNOTSUPP) {
    /* Actual error (e.g., I/O error, bad fd), don't fallback */
    return -1;
  }
  
  /* Fallback to mmap + madvise(MADV_WILLNEED) */
  page_size = getpagesize();
  
  /* Align offset to page boundary (required for mmap) */
  aligned_offset = offset & ~(off_t)(page_size - 1);
  aligned_length = length + (offset - aligned_offset);
  
  /* Round up length to page boundary */
  aligned_length = (aligned_length + page_size - 1) & ~(page_size - 1);
  
  addr = mmap(NULL, aligned_length, PROT_READ, MAP_PRIVATE, fd, aligned_offset);
  if (addr == MAP_FAILED) {
    /* mmap failed - this is expected for pseudo-filesystems like /proc, /sys */
    return -1;
  }
  
  /*
   * MADV_WILLNEED: Tell kernel we expect to access this region soon.
   * Note: Unlike readahead, this may block on some implementations.
   */
  ret = madvise(addr, aligned_length, MADV_WILLNEED);
  
  /* Always unmap regardless of madvise result */
  munmap(addr, aligned_length);
  
  return ret;
}

static void
process_file(const char *path, size_t offset, size_t length)
{
  int fd = -1;
  int maxprocs = conf->system.maxprocs;

  if (procs >= maxprocs)
    wait_for_children ();

  if (maxprocs > 0)
    {
      /* parallel reading */

      int status = fork();

      if (status == -1)
        {
	  /* ignore error, return */
	  return;
	}

      /* return immediately in the parent */
      if (status > 0)
        {
	  procs++;
	  return;
	}
    }

  fd = open(path,
	      O_RDONLY
	    | O_NOCTTY
#ifdef O_NOATIME
	    | O_NOATIME
#endif
	   );
  if (fd >= 0)
    {
      /* Use readahead with madvise fallback */
      try_readahead_with_fallback(fd, offset, length);

      close (fd);
    }

  if (maxprocs > 0)
    {
      /* we're in a child process, exit */
      exit (0);
    }
}

static void
sort_by_block_or_inode (preload_map_t **files, int file_count)
{
  int i;
  gboolean need_block = FALSE;

  /* first see if any file doesn't have block/inode info */
  for (i=0; i<file_count; i++)
    if (files[i]->block == -1)
      {
	need_block = TRUE;
	break;
      }

  if (need_block)
    {
      /* Sorting by path, to make stat fast. */
      qsort(files, file_count, sizeof(*files), (GCompareFunc)map_path_compare);

      for (i=0; i<file_count; i++)
	if (files[i]->block == -1)
	  set_block (files[i], conf->system.sortstrategy == SORT_INODE);
    }

  /* Sorting by block. */
  qsort(files, file_count, sizeof(*files), (GCompareFunc)map_block_compare);
}

static void
sort_files (preload_map_t **files, int file_count)
{
  switch (conf->system.sortstrategy) {
    case SORT_NONE:
      break;

    case SORT_PATH:
      qsort(files, file_count, sizeof(*files), (GCompareFunc)map_path_compare);
      break;

    case SORT_INODE:
    case SORT_BLOCK:
      sort_by_block_or_inode (files, file_count);
      break;

    default:
      g_warning ("Invalid value for config key system.sortstrategy: %d",
		 conf->system.sortstrategy);
      /* avoid warning every time */
      conf->system.sortstrategy = SORT_BLOCK;
      break;
  }
}

int
preload_readahead (preload_map_t **files, int file_count)
{
  int i;
  const char *path = NULL;
  size_t offset = 0, length = 0;
  int processed = 0;

  sort_files (files, file_count);
  for (i=0; i<file_count; i++)
    {
      if (path &&
	  offset <= files[i]->offset &&
	  offset + length >= files[i]->offset &&
	  0 == strcmp (path, files[i]->path))
        {
	  /* merge requests */
	  length = files[i]->offset + files[i]->length - offset;
	  continue;
	}

      if (path)
        {
	  process_file(path, offset, length);
	  processed++;
	  path = NULL;
	}

      path   = files[i]->path;
      offset = files[i]->offset;
      length = files[i]->length;
    }

  if (path)
    {
      process_file(path, offset, length);
      processed++;
      path = NULL;
    }

  wait_for_children ();

  return processed;
}
