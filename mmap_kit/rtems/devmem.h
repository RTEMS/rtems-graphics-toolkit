/*  devmem.h
 *
 *  /dev/mem device driver
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

#ifndef __DEVMEM_DRIVER_h
#define __DEVMEM_DRIVER_h

#ifdef __cplusplus
extern "C" {
#endif

#define DEVMEM_DRIVER_TABLE_ENTRY \
  { devmem_initialize, devmem_open, devmem_close, \
    devmem_read, devmem_write, devmem_control }

rtems_device_driver devmem_initialize(
  rtems_device_major_number,
  rtems_device_minor_number,
  void *
);

rtems_device_driver devmem_open(
  rtems_device_major_number,
  rtems_device_minor_number,
  void *
);

rtems_device_driver devmem_close(
  rtems_device_major_number,
  rtems_device_minor_number,
  void *
);

rtems_device_driver devmem_read(
  rtems_device_major_number,
  rtems_device_minor_number,
  void *
);

rtems_device_driver devmem_write(
  rtems_device_major_number,
  rtems_device_minor_number,
  void *
);

rtems_device_driver devmem_control(
  rtems_device_major_number,
  rtems_device_minor_number,
  void *
);

#ifdef __cplusplus
}
#endif

#endif
/* end of include file */
