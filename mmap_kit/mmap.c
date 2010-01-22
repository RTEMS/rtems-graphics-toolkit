/*
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/mman.h>

void *mmap(
  void   *addr,
  size_t  length,
  int     prot,
  int     flags,
  int     fd,
  off_t   offset
)
{
  return NULL;
}
