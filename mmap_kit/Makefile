#
#  Example Library Makefile
#

#
#  RTEMS_MAKEFILE_PATH is typically set in an environment variable
#

LIB=${ARCH}/libmmap_kit.a

# C source names
CSRCS = madvise.c mlockall.c mlock.c mmap.c mprotect.c msync.c \
  munlock.c munmap.c posix_madvise.c
CSRCS += devmem.c
COBJS = $(CSRCS:%.c=${ARCH}/%.o)
DEFINES += -I.

SYS_H_FILES=sys/mman.h
RTEMS_H_FILES=rtems/devmem.h

include $(RTEMS_MAKEFILE_PATH)/Makefile.inc
include $(RTEMS_CUSTOM)
include $(PROJECT_ROOT)/make/leaf.cfg

CLEAN_ADDITIONS += $(LIB)

OBJS= $(COBJS)

all:    ${ARCH} $(LIB)

$(LIB): $(OBJS)
	$(AR) crv $(LIB) $(OBJS)

INCLUDE_DIR=${PROJECT_RELEASE}/lib/include
install: all
	$(INSTALL_VARIANT) -m 555 ${LIB} ${PROJECT_RELEASE}/lib
	$(INSTALL_VARIANT) -m 555 ${SYS_H_FILES} ${INCLUDE_DIR}/sys
	$(INSTALL_VARIANT) -m 555 ${RTEMS_H_FILES} ${INCLUDE_DIR}/rtems
