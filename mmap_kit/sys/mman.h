/*
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */

#ifndef __SYS_MMAN_h
#define __SYS_MMAN_h

#include <sys/types.h>

/* XXX 
 * Add constants and missing routines from:
 *
 * http://www.opengroup.org/onlinepubs/000095399/basedefs/sys/mman.h.html
 */

/** This is the return value of mmap on error */
#define MAP_FAILED ((void *) -1)

/**
 *  XXX Doxygen comments for all XXX
 */
int madvise(
  void    *addr,
  size_t   len,
  int      advice
);

int posix_madvise(
  void    *addr,
  size_t   len,
  int      advice
);

int mlockall(
  int      flags
);

int mlock(
  const void *addr,
  int         flags,
  size_t      length
);

int munlock(
  const void *addr,
  int         flags,
  size_t      length
);

void *mmap(
  void   *addr,
  size_t  length,
  int     prot,
  int     flags,
  int     fd,
  off_t   offset
);

int munmap(
  void   *addr,
  size_t  length
);

int mprotect(
  const void *addr,
  size_t      len,
  int         prot
);

int msync(
  void    *addr,
  size_t   length,
  int      flags
);

#endif
