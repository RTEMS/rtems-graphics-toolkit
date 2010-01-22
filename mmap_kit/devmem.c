/*  /dev/mem
 *
 *  COPYRIGHT (c) 1989-2010.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems.h>
#include <rtems/devmem.h>
#include <rtems/libio.h>

uint32_t   devmem_major;
static char initialized;

/*  devmem_initialize
 *
 *  This routine is the /dev/mem device driver init routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargp - pointer to parameter block
 *
 *  Output parameters:
 *    rval       - RTEMS_SUCCESSFUL
 */
rtems_device_driver devmem_initialize(
  rtems_device_major_number major,
  rtems_device_minor_number minor __attribute__((unused)),
  void *pargp __attribute__((unused))
)
{
  rtems_device_driver status;

  if ( !initialized ) {
    initialized = 1;

    status = rtems_io_register_name(
      "/dev/devmem",
      major,
      (rtems_device_minor_number) 0
    );

    if (status != RTEMS_SUCCESSFUL)
      rtems_fatal_error_occurred(status);

    devmem_major = major;
  }

  return RTEMS_SUCCESSFUL;
}

/*  devmem_open
 *
 *  This routine is the /dev/mem device driver open routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargb - pointer to open parameter block
 *
 *  Output parameters:
 *    rval       - RTEMS_SUCCESSFUL
 */
rtems_device_driver devmem_open(
  rtems_device_major_number major __attribute__((unused)),
  rtems_device_minor_number minor __attribute__((unused)),
  void *pargp __attribute__((unused))
)
{
  return RTEMS_SUCCESSFUL;
}

/*  devmem_close
 *
 *  This routine is the /dev/mem device driver close routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargb - pointer to close parameter block
 *
 *  Output parameters:
 *    rval       - RTEMS_SUCCESSFUL
 */
rtems_device_driver devmem_close(
  rtems_device_major_number major __attribute__((unused)),
  rtems_device_minor_number minor __attribute__((unused)),
  void *pargp __attribute__((unused))
)
{
  return RTEMS_SUCCESSFUL;
}

/*  devmem_read
 *
 *  This routine is the /dev/mem device driver read routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargp - pointer to read parameter block
 *
 *  Output parameters:
 *    rval       - RTEMS_SUCCESSFUL
 */
rtems_device_driver devmem_read(
  rtems_device_major_number major __attribute__((unused)),
  rtems_device_minor_number minor __attribute__((unused)),
  void *pargp __attribute__((unused))
)
{
  return RTEMS_SUCCESSFUL;
}

/*  devmem_write
 *
 *  This routine is the /dev/mem device driver write routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargp - pointer to write parameter block
 *
 *  Output parameters:
 *    rval       - RTEMS_SUCCESSFUL
 */
rtems_device_driver devmem_write(
  rtems_device_major_number major __attribute__((unused)),
  rtems_device_minor_number minor __attribute__((unused)),
  void *pargp
)
{
  rtems_libio_rw_args_t *rw_args = (rtems_libio_rw_args_t *) pargp;

  if ( rw_args )
    rw_args->bytes_moved = rw_args->count;

  return RTEMS_SUCCESSFUL;
}

/*  devmem_control
 *
 *  This routine is the /dev/mem device driver control routine.
 *
 *  Input parameters:
 *    major - device major number
 *    minor - device minor number
 *    pargp - pointer to cntrl parameter block
 *
 *  Output parameters:
 *    rval       - RTEMS_SUCCESSFUL
 */

rtems_device_driver devmem_control(
  rtems_device_major_number major __attribute__((unused)),
  rtems_device_minor_number minor __attribute__((unused)),
  void *pargp __attribute__((unused))
)
{
  return RTEMS_SUCCESSFUL;
}
