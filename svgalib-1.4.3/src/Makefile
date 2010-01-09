#----------------------------------------------------------------------
# Makefile for the main svgalib source.
#----------------------------------------------------------------------

# *** NO SERVICIBLE PARTS HERE!
#     All options are in Makefile.cfg.

include ../Makefile.cfg

srcdir    = ..
VPATH     = $(srcdir)/src

#----------------------------------------------------------------------
# Compiler Section (overrides Makefile.cfg)
#----------------------------------------------------------------------

INCLUDES += -I$(srcdir)/src

#----------------------------------------------------------------------
# Rules Section
#----------------------------------------------------------------------

DRIVERS = vgadrv.o regextr.o modetab.o
MODULES = vga.o \
          vgadraw.o vgaclear.o vgaline.o vgapix.o \
          vgapal.o vgacol.o vgamodesel.o vgamisc.o \
	  vgadump.o vgaconvplanar.o vgaaccel.o \
	  timing.o vgaregs.o interface.o accel.o \
	  vgabgpage.o vgabgvt.o vgapci.o
MOUSE   = mouse/mouse.o
KEYBOARD = keyboard/keyboard.o
JOYSTICK = joystick/joystick.o
RAMDAC	= ramdac/ramdac.o ramdac/normal.o ramdac/attdacs.o ramdac/sierra.o \
          ramdac/icw.o ramdac/s3dacs.o ramdac/IBMRGB52x.o
CLOCKCHIP = clockchip/icd2061a.o

# defines for all files.
ifeq (y, $(NO_ASM))
  DEFINES += -DNO_ASSEMBLY
endif
ifdef SVGALIB_CONFIG_FILE
  DEFINES += -DSVGALIB_CONFIG_FILE=\"$(SVGALIB_CONFIG_FILE)\"
endif
ifdef ALLOW_MOUSE_OVERRIDE
  DEFINES += -DALLOW_MOUSE_OVERRIDE
endif
ifdef DEBUG_ACCEL
  DEFINES += -DDEBUG_ACCEL
endif
ifdef DEBUG_KEYBOARD
  DEFINES += -DDEBUG_KEYBOARD
endif
ifdef BACKGROUND
  DEFINES += -DBACKGROUND=1
endif
ifeq (a.out, $(TARGET_FORMAT))
  DEFINES += -DSVGA_AOUT
endif

# defines for vga.c only.
ifdef ROOT_VC_SHORTCUT
  VGA_DEFINES += -DROOT_VC_SHORTCUT
endif

ifdef DEBUG_CONF
  DEFINES += -DDEBUG_CONF
endif

ifdef INCLUDE_ET4000_DRIVER
  VGA_DEFINES += -DINCLUDE_ET4000_DRIVER
  DRIVERS += et4000.o vgahico.o 
  ifdef INCLUDE_ET4000_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_ET4000_DRIVER_TEST
  endif
  ifdef ET4000_REGS
    DEFINES += -DET4000_REGS=\"$(ET4000_REGS)\"
  endif
  ifdef USE_CLOCKS
    DEFINES += -DUSE_CLOCKS
  endif
endif
ifdef INCLUDE_CIRRUS_DRIVER
  VGA_DEFINES += -DINCLUDE_CIRRUS_DRIVER
  DRIVERS += cirrus.o
  ifdef INCLUDE_CIRRUS_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_CIRRUS_DRIVER_TEST
  endif
endif
ifdef INCLUDE_TVGA_DRIVER
  VGA_DEFINES += -DINCLUDE_TVGA_DRIVER
  DRIVERS += tvga8900.o
  ifdef INCLUDE_TVGA_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_TVGA_DRIVER_TEST
  endif
endif
ifdef INCLUDE_OAK_DRIVER
  VGA_DEFINES += -DINCLUDE_OAK_DRIVER
  DRIVERS += oak.o
  ifdef INCLUDE_OAK_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_OAK_DRIVER_TEST
  endif
endif
ifdef INCLUDE_EGA_DRIVER
  VGA_DEFINES += -DINCLUDE_EGA_DRIVER
  DRIVERS += egadrv.o
  ifdef INCLUDE_EGA_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_EGA_DRIVER_TEST
  endif
  ifdef EGA_REGS
    DEFINES += -DEGA_REGS=\"$(EGA_REGS)\"
  endif
endif
ifdef INCLUDE_MACH32_DRIVER
  VGA_DEFINES += -DINCLUDE_MACH32_DRIVER
  DRIVERS += mach32.o
  ifdef INCLUDE_MACH32_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_MACH32_DRIVER_TEST
  endif
endif
ifdef INCLUDE_S3_DRIVER
  VGA_DEFINES += -DINCLUDE_S3_DRIVER
  DRIVERS += s3.o
  ifdef INCLUDE_S3_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_S3_DRIVER_TEST
  endif
endif
ifdef INCLUDE_ET3000_DRIVER
  VGA_DEFINES += -DINCLUDE_ET3000_DRIVER
  DRIVERS += et3000.o
  ifdef INCLUDE_ET3000_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_ET3000_DRIVER_TEST
  endif
endif
ifdef INCLUDE_GVGA6400_DRIVER
  VGA_DEFINES += -DINCLUDE_GVGA6400_DRIVER
  DRIVERS += gvga6400.o
  ifdef INCLUDE_GVGA6400_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_GVGA6400_DRIVER_TEST
  endif
endif
ifdef INCLUDE_ARK_DRIVER
  VGA_DEFINES += -DINCLUDE_ARK_DRIVER
  DRIVERS += ark.o
  ifdef INCLUDE_ARK_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_ARK_DRIVER_TEST
  endif
endif
ifdef INCLUDE_ATI_DRIVER
  VGA_DEFINES += -DINCLUDE_ATI_DRIVER
  DRIVERS += ati.o
  ifdef INCLUDE_ATI_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_ATI_DRIVER_TEST
  endif
endif
ifdef INCLUDE_ALI_DRIVER
  VGA_DEFINES += -DINCLUDE_ALI_DRIVER
  DRIVERS += ali.o
  ifdef INCLUDE_ALI_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_ALI_DRIVER_TEST
  endif
endif
ifdef INCLUDE_MACH64_DRIVER
  VGA_DEFINES +=-DINCLUDE_MACH64_DRIVER
  DRIVERS += mach64.o
  ifdef INCLUDE_MACH64_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_MACH64_DRIVER_TEST
  endif
endif
ifdef INCLUDE_CHIPS_DRIVER
  VGA_DEFINES +=-DINCLUDE_CHIPS_DRIVER
  DRIVERS += chips.o
  ifdef INCLUDE_CHIPS_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_CHIPS_DRIVER_TEST
  endif
endif
ifdef INCLUDE_APM_DRIVER
  VGA_DEFINES +=-DINCLUDE_APM_DRIVER
  DRIVERS += apm.o
  ifdef INCLUDE_APM_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_APM_DRIVER_TEST
  endif
endif
ifdef INCLUDE_VESA_DRIVER
  VGA_DEFINES +=-DINCLUDE_VESA_DRIVER
  DRIVERS += vesa.o lrmi.o
  ifdef INCLUDE_VESA_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_VESA_DRIVER_TEST
  endif
endif
ifdef INCLUDE_NV3_DRIVER
  VGA_DEFINES +=-DINCLUDE_NV3_DRIVER
  DRIVERS += nv3.o
  ifdef INCLUDE_NV3_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_NV3_DRIVER_TEST
  endif
endif
ifdef INCLUDE_G400_DRIVER
  VGA_DEFINES +=-DINCLUDE_G400_DRIVER
  DRIVERS += g400.o
  ifdef INCLUDE_G400_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_G400_DRIVER_TEST
  endif
endif
ifdef INCLUDE_R128_DRIVER
  VGA_DEFINES +=-DINCLUDE_R128_DRIVER
  DRIVERS += r128.o
  ifdef INCLUDE_R128_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_R128_DRIVER_TEST
  endif
endif
ifdef INCLUDE_MX_DRIVER
  VGA_DEFINES +=-DINCLUDE_MX_DRIVER
  DRIVERS += mx.o
  ifdef INCLUDE_MX_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_MX_DRIVER_TEST
  endif
endif
ifdef INCLUDE_SAVAGE_DRIVER
  VGA_DEFINES +=-DINCLUDE_SAVAGE_DRIVER
  DRIVERS += savage.o
  ifdef INCLUDE_SAVAGE_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_SAVAGE_DRIVER_TEST
  endif
endif
ifdef INCLUDE_PARADISE_DRIVER
  VGA_DEFINES +=-DINCLUDE_PARADISE_DRIVER
  DRIVERS += paradise.o
  ifdef INCLUDE_PARADISE_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_PARADISE_DRIVER_TEST
  endif
endif
ifdef INCLUDE_RAGE_DRIVER
  VGA_DEFINES +=-DINCLUDE_RAGE_DRIVER
  DRIVERS += rage.o
  ifdef INCLUDE_RAGE_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_RAGE_DRIVER_TEST
  endif
endif
ifdef INCLUDE_BANSHEE_DRIVER
  VGA_DEFINES +=-DINCLUDE_BANSHEE_DRIVER
  DRIVERS += banshee.o
  ifdef INCLUDE_BANSHEE_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_BANSHEE_DRIVER_TEST
  endif
endif
ifdef INCLUDE_SIS_DRIVER
  VGA_DEFINES +=-DINCLUDE_SIS_DRIVER
  DRIVERS += sis.o
  ifdef INCLUDE_SIS_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_SIS_DRIVER_TEST
  endif
endif
ifdef INCLUDE_I740_DRIVER
  VGA_DEFINES +=-DINCLUDE_I740_DRIVER
  DRIVERS += i740.o
  ifdef INCLUDE_I740_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_I740_DRIVER_TEST
  endif
endif
ifdef INCLUDE_LAGUNA_DRIVER
  VGA_DEFINES +=-DINCLUDE_LAGUNA_DRIVER
  DRIVERS += laguna.o
  ifdef INCLUDE_LAGUNA_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_LAGUNA_DRIVER_TEST
  endif
endif
ifdef INCLUDE_ET6000_DRIVER
  VGA_DEFINES += -DINCLUDE_ET6000_DRIVER
  DRIVERS += et6000.o
  ifdef INCLUDE_ET6000_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_ET6000_DRIVER_TEST
  endif
endif
ifdef INCLUDE_NEO_DRIVER
  VGA_DEFINES += -DINCLUDE_NEO_DRIVER
  DRIVERS += neo.o
  ifdef INCLUDE_NEO_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_NEO_DRIVER_TEST
  endif
endif
ifdef INCLUDE_FBDEV_DRIVER
  VGA_DEFINES += -DINCLUDE_FBDEV_DRIVER
  DRIVERS += fbdev.o
  ifdef INCLUDE_FBDEV_DRIVER_TEST
    VGA_DEFINES += -DINCLUDE_FBDEV_DRIVER_TEST
  endif
endif

# DYNAMIC is used by both ET drivers.
ifdef DYNAMIC
  ifdef INCLUDE_ET6000_DRIVER
    DEFINES += -DDYNAMIC
  else
    ifdef INCLUDE_ET4000_DRIVER
      DEFINES += -DDYNAMIC
    endif
  endif
endif

# defines for ramdac.c, ramdac.h (and files including it) only.
ifdef INCLUDE_NORMAL_DAC
  RAMDAC_DEFINES += -DINCLUDE_NORMAL_DAC
  ifdef INCLUDE_NORMAL_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_NORMAL_DAC_TEST
  endif
endif
ifdef INCLUDE_S3_SDAC_DAC
  RAMDAC_DEFINES += -DINCLUDE_S3_SDAC_DAC
  ifdef INCLUDE_S3_SDAC_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_S3_SDAC_DAC_TEST
  endif
endif
ifdef INCLUDE_S3_GENDAC_DAC
  RAMDAC_DEFINES += -DINCLUDE_S3_GENDAC_DAC
  ifdef INCLUDE_S3_GENDAC_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_S3_GENDAC_DAC_TEST
  endif
endif
ifdef INCLUDE_S3_TRIO64_DAC
  RAMDAC_DEFINES += -DINCLUDE_S3_TRIO64_DAC
  ifdef INCLUDE_S3_TRIO64_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_S3_TRIO64_DAC_TEST
  endif
endif
ifdef INCLUDE_SIERRA_DAC
  RAMDAC_DEFINES += -DINCLUDE_SIERRA_DAC
  ifdef INCLUDE_SIERRA_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_SIERRA_DAC_TEST
  endif
endif
ifdef INCLUDE_SC15025_DAC
  RAMDAC_DEFINES += -DINCLUDE_SC15025_DAC
  ifdef INCLUDE_SC15025_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_SC15025_DAC_TEST
  endif
endif
ifdef INCLUDE_ATT20C490_DAC
  RAMDAC_DEFINES += -DINCLUDE_ATT20C490_DAC
  ifdef INCLUDE_ATT20C490_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_ATT20C490_DAC_TEST
  endif
endif
ifdef INCLUDE_ATT20C498_DAC
  RAMDAC_DEFINES += -DINCLUDE_ATT20C498_DAC
  ifdef INCLUDE_ATT20C498_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_ATT20C498_DAC_TEST
  endif
endif
ifdef INCLUDE_ICW_DAC
  RAMDAC_DEFINES += -DINCLUDE_ICW_DAC
  ifdef INCLUDE_ICW_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_ICW_DAC_TEST
  endif
endif
ifdef INCLUDE_IBMRGB52x_DAC
  RAMDAC_DEFINES += -DINCLUDE_IBMRGB52x_DAC
  ifdef INCLUDE_IBMRGB52x_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_IBMRGB52x_DAC_TEST
  endif
endif
ifdef INCLUDE_SC1148X_DAC
  RAMDAC_DEFINES += -DINCLUDE_SC1148X_DAC
  ifdef INCLUDE_SC1148X_DAC_TEST
    RAMDAC_DEFINES += -DINCLUDE_SC1148X_DAC_TEST
  endif
endif

ALLOBJS = $(MODULES) $(MOUSE) $(JOYSTICK) $(KEYBOARD) $(RAMDAC) $(CLOCKCHIP) $(DRIVERS)

.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<

.PHONY:	all dep depadd clean install FORCE



ifeq (a.out, $(TARGET_FORMAT))

all:	libvga.a

else                                                               

all:	libvga.so.$(VERSION)

# These rules are for ELF only.
libvga.so.$(VERSION): $(ALLOBJS)
	$(CC) -s -shared -Wl,-soname,libvga.so.$(MAJOR_VER) \
	  -o libvga.so.$(VERSION) $(ALLOBJS) -lm

$(sharedlibdir)/libvga.so.$(VERSION): libvga.so.$(VERSION)
	$(INSTALL_PROGRAM) $< $(sharedlibdir)/$<
	ldconfig

install: $(sharedlibdir)/libvga.so.$(VERSION)

endif	  

libvga.a: $(ALLOBJS)
	rm -f libvga.a
	$(AR) rcs libvga.a $(ALLOBJS)

libvgagl.a: FORCE
	make -f $(srcdir)/gl/Makefile $@ srcdir="$(srcdir)" DLLFLAGS="$(DLLFLAGS)"

vga.o: vga.c
	$(CC) $(CFLAGS) $(VGA_DEFINES) -c -o $@ $<

#lrmi must NOT be compiled as -fPIC
lrmi.o: lrmi.c
	$(CC) $(CFLAGS:-fPIC=) -c -o $@ $<

$(RAMDAC): %.o: %.c
	$(CC) $(CFLAGS) $(RAMDAC_DEFINES) -c -o $@ $<

s3.o ark.o: %.o: %.c
	$(CC) $(CFLAGS) $(RAMDAC_DEFINES) -c -o $@ $<

nucleus.o: %.o: %.c 
	$(CC) $(CFLAGS) $(RAMDAC_DEFINES) -c -o $@ $< scitech.o

$(MODULES): .depend.src

dep:
	rm -f .depend.src
	make .depend.src

.depend.src:
	echo '# Module dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(MODULES)) >>.depend.src
	echo '# Mouse dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(MOUSE)) >.depend.src.mouse
	sed 's#^\(.*\.o\):#mouse/\1:#g' <.depend.src.mouse >>.depend.src
	rm -f .depend.src.mouse
	echo '# Joystick dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(JOYSTICK)) >.depend.src.joystick
	sed 's#^\(.*\.o\):#joystick/\1:#g' <.depend.src.joystick >>.depend.src
	rm -f .depend.src.joystick
	echo '# Keyboard dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(KEYBOARD)) >.depend.src.keyboard
	sed 's#^\(.*\.o\):#keyboard/\1:#g' <.depend.src.keyboard >>.depend.src
	rm -f .depend.src.keyboard
	echo '# Driver dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(DRIVERS)) >>.depend.src
	echo '# Ramdac dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(RAMDAC)) >.depend.src.ramdac
	sed 's#^\(.*\.o\):#ramdac/\1:#g' <.depend.src.ramdac >>.depend.src
	rm -f .depend.src.ramdac
	echo '# Clockchip dependencies' >>.depend.src
	$(CC) $(INCLUDES) -MM $(patsubst %.o,$(srcdir)/src/%.c,$(CLOCKCHIP)) >.depend.src.clockchip
	sed 's#^\(.*\.o\):#clockchip/\1:#g' <.depend.src.clockchip >>.depend.src
	rm -f .depend.src.clockchip

clean:
	rm -f .depend.src *~ *.o mouse/*.o keyboard/*.o mouse/*~ keyboard/*~ \
		ramdac/*.o clockchip/*.o ramdac/*~ clockchip/*~ \
		libvga.a libvga.so.$(VERSION) *.bak */*.bak

FORCE: # dummy target.

#
# include a dependency file if one exists
#
ifeq (.depend.src,$(wildcard .depend.src))
include .depend.src
endif
