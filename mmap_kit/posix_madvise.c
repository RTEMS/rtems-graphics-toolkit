/*
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */

#include <sys/mman.h>

int posix_madvise(
  void    *addr,
  size_t   len,
  int      advice
)
{
  return 0;
}
