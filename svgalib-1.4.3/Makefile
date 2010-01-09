#---------------------------------------------------------------------OU
# Makefile for SVGAlib.
#
# It's pretty unreadable, but just doing make install should be
# enough. This will install the headerfiles and shared library first
# (which is enough to compile things), after which the static version is
# optionally compiled and installed (if it fails, the shared libraries
# should still work fine).
#
#----------------------------------------------------------------------

# *** NO SERVICIBLE PARTS HERE!
#     All options are in Makefile.cfg.

include Makefile.cfg

#----------------------------------------------------------------------
# Rules Section
#----------------------------------------------------------------------

JUMP_DIR=$(shell sh -c pwd)/jump/
JUMP_LIB=libvga

export JUMP_DIR
export JUMP_LIB

# Search the old directories for mkimage and mkstubs.
# Make sure mkimage get the right "as" assembler.
ifeq (a.out, $(TARGET_FORMAT))
  PATH := /usr/i486-linuxaout/bin:$(PATH):/usr/dll/bin
  export PATH
endif	

# In case someone goes for the demos w/o installing svgalib make
# the static libs first.
ifdef INSTALLSHAREDLIB
  PREDEMO =
else
  PREDEMO = static
endif

# A flag if this is a distribution:
DISTRIBUTION = $(shell sh -c "echo sharedlib/DIST*")
INSTALLAOUTDIR =	

ifeq (elf, $(TARGET_FORMAT))
ifdef INSTALLAOUTLIB
ifeq ($(DISTRIBUTION), sharedlib/DISTRIBUTION)
	OBSOLETEAOUTDIRS = $(shell sed 's?\#.*$$??' /etc/ld.so.conf 2>/dev/null | \
			     sed 's?\([^/]\)[ 	]*$$?\1/ ?' | grep aout/ )
	INSTALLAOUTDIR = $(word 1, $(OBSOLETEAOUTDIRS))
endif
endif
endif

UTILS	= restorefont runx restorepalette restoretextmode textmode \
	  savetextmode dumpreg fix132x43

OBSOLETEHDIRS = /usr/include/ /usr/include/vga/ /usr/local/include/ /usr/local/include/vga/
OBSOLETEBDIRS = /usr/bin/ /usr/local/bin/

OBSOLETEHEADERS = /inc/vga.h /inc/vgakeyboard.h /inc/vgamouse.h /inc/vgagl.h /inc/vgajoystick.h

OBSOLETELIBLINKS = /lib/libvga.so /lib/libvga.so.1 /lib/libvgagl.so /lib/libvgagl.so.1

OBSOLETESHAREDIMAGES = /lib/libvgagl.so.* /lib/libvga.so.*

ifeq (a.out, $(TARGET_FORMAT))
	OBSOLETELDIRS = /lib/ /usr/lib/ /usr/local/lib/ /usr/share/lib/ \
			$(shell sed 's?\#.*$$??' /etc/ld.so.conf 2>/dev/null | \
			  sed 's?\([^/]\)[ 	]*$$?\1/ ?' )
	SHAREDLIBS = sharedlib/libvga.so.$(VERSION)
	SVGALIBSHAREDSTUBS = sharedlib/libvga.sa sharedlib/libvgagl.sa
	JUMP = jump
else	
	OBSOLETELDIRS = /lib/ /usr/lib/ /usr/local/lib/ /usr/share/lib/ \
			$(shell sed 's?\#.*$$??' /etc/ld.so.conf 2>/dev/null | \
			  sed 's?\([^/]\)[ 	]*$$?\1/ ?' | grep -v aout/ )

	SHAREDAOUTLIBS = sharedlib/libvga.so.$(VERSION)
	SVGALIBSHAREDAOUTSTUBS = sharedlib/libvga.sa sharedlib/libvgagl.sa
	SHAREDLIBS = sharedlib/libvga.so.$(VERSION) sharedlib/libvgagl.so.$(VERSION)
	SVGALIBSHAREDSTUBS =
	JUMP =
endif	

BACKUP   = ../svgalib-$(VERSION).tar.gz

default:
	@echo "To install SVGAlib, do one of the following:"
	@echo ""
	@echo "	make clean      - clean every thing. Do this after every change"
	@echo "	                  of Makefile.cfg! Esp. when changing from a.out"
	@echo "	                  to ELF and vice versa"
	@echo "	make install	- compile & install components specified in Makefile.cfg"
	@echo "	make demoprogs	- make demo programs in demo/ and threeDKit/"
	@echo ""
	@echo "	make uninstall	- remove an existing installation from various"
	@echo "	                  common places. (old traces often confuse the"
	@echo "	                  compiler even when svgalib is not installed anew)"
	@echo "	                  (make install includes an uninstall first)"
	@echo ""
	@echo "	Be sure to read the file 0-INSTALL!"
	@echo ""

.PHONY: default all install installheaders installconfig
.PHONY: clean distclean indent uninstall
.PHONY: force_remake remake_shared shared static
.PHONY: indent-gnu configaout

installheaders:
	@echo Installing header files in $(includedir).
	@if [ -f /usr/include/vga.h ]; then \
		rm -f /usr/include/vga.h /usr/include/vgagl.h; \
		echo Old header files in /usr/include removed.; \
	fi
	mkdir -p $(includedir)
	@cp $(srcdir)/src/vga.h $(includedir)/vga.h
	@chmod a+r $(includedir)/vga.h
	@cp $(srcdir)/gl/vgagl.h $(includedir)/vgagl.h
	@chmod a+r $(includedir)/vgagl.h
	@cp $(srcdir)/src/mouse/vgamouse.h $(includedir)/vgamouse.h
	@chmod a+r $(includedir)/vgamouse.h
	@cp $(srcdir)/src/joystick/vgajoystick.h $(includedir)/vgajoystick.h
	@chmod a+r $(includedir)/vgajoystick.h
	@cp $(srcdir)/src/keyboard/vgakeyboard.h $(includedir)/vgakeyboard.h
	@chmod a+r $(includedir)/vgakeyboard.h

installsharedlib: $(SHAREDLIBS) $(SVGALIBSHAREDSTUBS)
ifeq (a.out, $(TARGET_FORMAT))
	@echo Installing shared library stubs in $(libdir).
	-@for foo in $(notdir $(SVGALIBSHAREDSTUBS)); do \
		$(INSTALL_DATA) sharedlib/$$foo $(libdir)/$$foo; \
		chmod a+r $(libdir)/$$foo; \
	done
endif
	@if [ -f /usr/lib/libvga.sa ]; then \
		rm -f /usr/lib/libvga.sa /usr/lib/libvgagl.sa; \
		echo Old stubs in /usr/lib removed.; \
	fi
	@rm -f $(OBSOLETESHAREDIMAGES)
ifndef KEEPSHAREDLIBS
	@echo "Removing shared library images (old & current)..."
	@for i in $(OBSOLETELDIRS); do \
	    rm -f `echo $(OBSOLETESHAREDIMAGES) | sed s?/lib/?$$i?g`; \
         done
endif
	mkdir -p $(sharedlibdir)
	@echo Installing shared library image as \
		$(addprefix $(sharedlibdir)/,$(notdir $(SHAREDLIBS))).
	@for foo in $(notdir $(SHAREDLIBS)); do \
		$(INSTALL_SHLIB) sharedlib/$$foo $(sharedlibdir)/$$foo; \
		(cd $(sharedlibdir); \
		 ln -sf $$foo `echo $$foo | sed 's/\.so\..*/.so/'` ); \
	done
	@ldconfig

ifdef INSTALLAOUTDIR

installaoutcompat:
	@echo Installing shared a.out compatibility library in $(TOPDIR)/$(INSTALLAOUTDIR).
	@for foo in $(notdir $(SVGALIBSHAREDAOUTSTUBS)); do \
		$(INSTALL_DATA) sharedlib/$$foo $(TOPDIR)/$(INSTALLAOUTDIR)$$foo; \
	done
	@echo Installing shared a.out library compat image as \
		$(addprefix $(INSTALLAOUTDIR),$(notdir $(SHAREDLIBS))).
	@for foo in $(notdir $(SHAREDAOUTLIBS)); do \
		$(INSTALL_SHLIB) sharedlib/$$foo $(TOPDIR)/$(INSTALLAOUTDIR)$$foo; \
		(cd $(TOPDIR)/$(INSTALLAOUTDIR); \
		 ln -sf $$foo `echo $$foo | sed 's/\.so\..*/.so/'` ); \
	done
	@ldconfig

else

installaoutcompat:
	@true

endif

installstaticlib: static
	@echo Installing static libraries in $(libdir).
	@rm -f /usr/lib/libvga.a
	@$(INSTALL_DATA) staticlib/libvga.a $(libdir)/libvga.a
	@chmod a+r $(libdir)/libvga.a
	@rm -f /usr/lib/libvgagl.a
	@$(INSTALL_DATA) staticlib/libvgagl.a $(libdir)/libvgagl.a
	@chmod a+r $(libdir)/libvgagl.a

installutils: textutils lrmi
	@if [ ! -d $(bindir) ]; then \
		echo No $(bindir) directory, creating it.; \
		mkdir $(bindir); \
	fi
	@if [ -f /usr/bin/restorefont ]; then \
		echo Removing old utilities in /usr/bin.; \
		for x in $(UTILS); do rm -f /usr/bin/$$x; done; \
	fi
	@if [ -f /usr/bin/convfont ]; then \
		echo Removing inappropriate utilities in /usr/bin.; \
		rm -f /usr/bin/convfont /usr/bin/setmclk; \
	fi
	@echo Installing textmode utilities in $(bindir):
	@echo "restorefont:      Save/restore textmode font."
	@cp utils/restorefont $(bindir)
	@echo "restorepalette:   Set standard VGA palette."
	@cp utils/restorepalette $(bindir)
	@echo "dumpreg:          Write ASCII dump of SVGA registers."
	@cp utils/dumpreg $(bindir)
	@echo "restoretextmode:  Save/restore textmode registers."
	@cp utils/restoretextmode $(bindir)
	@echo "textmode:         Script that tries to restore textmode."
	@cp utils/textmode $(bindir)
	@echo "savetextmode:     Script that saves textmode information used by 'textmode'."
	@cp utils/savetextmode $(bindir)
	@echo "mode3:       Restore textmode by setting VESA mode 3."
	@cp lrmi-0.6m/mode3 $(bindir)
	@echo "Installing keymap utilities in $(bindir):"
	@echo "svgakeymap:       Perl script that generates scancode conversion maps."
	@cp utils/svgakeymap $(bindir)

installconfig:
	@if [ ! -d $(datadir) ]; then \
		echo Creating configuration directory $(datadir).; \
		mkdir $(datadir); \
		chmod go-w $(datadir); \
	fi
	@if [ \( -f /usr/local/lib/libvga.config -a ! -f $(datadir)/libvga.config \) ]; then \
		echo "Moving old config file /usr/local/lib/libvga.config to $(datadir)." ; \
		mv -f /usr/local/lib/libvga.config $(datadir)/libvga.config; \
	fi
	@if [ \( -f /usr/local/lib/libvga.et4000 -a ! -f $(datadir)/libvga.et4000 \) ]; then \
		echo "Moving old config file /usr/local/lib/libvga.et4000 to $(datadir)." ; \
		mv -f /usr/local/lib/libvga.et4000 $(datadir)/libvga.et4000; \
	fi
	@if [ \( -f /usr/local/lib/libvga.ega -a ! -f $(datadir)/libvga.ega \) ]; then \
		echo "Moving old config file /usr/local/lib/libvga.ega to $(datadir)." ; \
		mv -f /usr/local/lib/libvga.ega $(datadir)/libvga.ega; \
	fi
	@if [ \( -f /etc/mach32.eeprom -a ! -f $(datadir)/mach32.eeprom \) ]; then \
		echo Consider moving your /etc/mach32.eeprom file to $(datadir) ; \
		echo and changing $(datadir)/libvga.config appropriately. ; \
	fi
	@if [ ! -f $(datadir)/libvga.config ]; then \
		echo Installing default configuration file in $(datadir).; \
		cp $(confdir)/libvga.config $(datadir)/libvga.config; \
	fi
	@if [ ! -f $(datadir)/libvga.et4000 ]; then \
		echo Installing dynamically loaded ET4000 registers in $(datadir).; \
		cp $(confdir)/et4000.regs $(datadir)/libvga.et4000; \
	fi
	@if [ ! -f $(datadir)/default.keymap ]; then \
		echo Installing default keymap file in $(datadir).; \
		cp $(confdir)/default.keymap $(datadir)/null.keymap; \
	fi
	@if [ ! -f $(datadir)/dvorak-us.keymap ]; then \
		echo Installing Dvorak keymap file in $(datadir).; \
		cp $(confdir)/dvorak-us.keymap $(datadir)/dvorak-us.keymap; \
	fi

installman:
	(cd doc; $(MAKE) -f $(srcdir)/doc/Makefile srcdir="$(srcdir)" install )
	 
install: uninstall $(INSTALLAOUTLIB) installheaders $(INSTALLSHAREDLIB) installconfig \
	$(INSTALLSTATICLIB) $(INSTALLUTILS) $(INSTALLMAN)
	@echo
	@echo
	@echo Now run "'make demoprogs'" to make the test and demo programs in
	@echo demos/ and threedkit/.

uninstall:
	@echo "Removing textmode utilities..."
	@for i in $(OBSOLETEBDIRS); do \
          for prog in $(UTILS); do \
            rm -f $$i$$prog ; \
          done ; \
         done
	@echo "Removing shared library stubs (old & current)..."
	@for i in $(OBSOLETELDIRS); do \
	    rm -f `echo /lib/libvga.so.$(VERSION) /lib/libvgagl.so.$(VERSION) \
			$(OBSOLETELIBLINKS) /lib/libvga.sa /lib/libvgagl.sa \
		     | sed s?/lib/?$$i?g`; \
         done
ifndef KEEPSHAREDLIBS
	@echo "Removing shared library images (old & current)..."
	@for i in $(OBSOLETELDIRS); do \
	    rm -f `echo $(OBSOLETESHAREDIMAGES) | sed s?/lib/?$$i?g`; \
         done
endif
ifneq ($(INSTALLAOUTDIR), )
	@echo "Removing shared a.out library stubs (old & current)..."
	@for i in $(OBSOLETEAOUTDIRS); do \
	    rm -f `echo /lib/libvga.so.$(VERSION) /lib/libvgagl.so.$(VERSION) \
			$(OBSOLETELIBLINKS) /lib/libvga.sa /lib/libvgagl.sa \
		     | sed s?/lib/?$$i?g`; \
         done
ifndef KEEPSHAREDLIBS
	@echo "Removing shared a.out library images (old & current)..."
	@for i in $(OBSOLETEAOUTDIRS); do \
	    rm -f `echo $(OBSOLETESHAREDIMAGES) | sed s?/lib/?$$i?g`; \
         done
endif
endif
	@echo "Removing static libraries..."
	@for i in $(OBSOLETELDIRS); do \
	    rm -f `echo /lib/libvga.a /lib/libvgagl.a | sed s?/lib/?$$i?g`; \
	done
	@echo "Removing header files..."
	@for i in $(OBSOLETEHDIRS); do \
	    rm -f `echo $(OBSOLETEHEADERS) | sed s?/inc/?$$i?g`; \
	done
	(cd doc; $(MAKE) -f $(srcdir)/doc/Makefile srcdir="$(srcdir)" uninstall)
	 

SHAREDDIRS0 = sharedlib/mouse sharedlib/keyboard sharedlib/ramdac \
		sharedlib/clockchip sharedlib/joystick
SHAREDDIRS = $(SHAREDDIRS0) $(JUMP)
STATICDIRS = staticlib/mouse staticlib/keyboard staticlib/ramdac \
		staticlib/clockchip staticlib/joystick
UTILDIRS = utils
DEMODIRS = demos threeDKit

$(SHAREDDIRS0) $(STATICDIRS) $(DEMODIRS):
	mkdir -p $@

jump:
	mkdir -p jump
	if [ ! -f jump/jump.funcs ]; then \
		cp $(srcdir)/jump/jump.funcs $(srcdir)/jump/jump.ignore \
		   $(srcdir)/jump/jump.vars jump; \
	fi

utils:
	mkdir -p utils
	if [ ! -f utils/runx ]; then \
		cp $(srcdir)/utils/runx $(srcdir)/utils/savetextmode \
		   $(srcdir)/utils/textmode utils; \
	fi

static: staticlib/libvga.a staticlib/libvgagl.a

.PHONY: staticlib/libvgagl.a staticlib/libvga.a

staticlib/libvgagl.a staticlib/libvga.a: $(STATICDIRS)
	(cd $(dir $@); \
	 $(MAKE) -f $(srcdir)/src/Makefile $(notdir $@) \
	 	srcdir="$(srcdir)" DLLFLAGS="" \
	)

ifeq (a.out, $(TARGET_FORMAT))

shared: force_remake $(SHAREDLIBS) $(SVGALIBSHAREDSTUBS)

force_remake:
	@if [ -f sharedlib/DISTRIBUTION ] ; then \
		rm -f sharedlib/DISTRIBUTION ; \
		$(MAKE) shared; \
	fi

sharedlib/libsources:
	@mkdir -p sharedlib
	@echo sharedlib/libvga.a sharedlib/libvgagl.a >sharedlib/libsources

ifeq ($(DISTRIBUTION), sharedlib/DISTRIBUTION)

sharedlib/libvga.so.$(VERSION):
	@echo "Ooops, this shouldn\'t happen.. I\'m going to fix it..."
	@rm -f sharedlib/DISTRIBUTION
	@$(MAKE) sharedlib/libvga.so.$(VERSION)

else

sharedlib/libvga.so.$(VERSION): sharedlib/libvga.a sharedlib/libvgagl.a
	(cd sharedlib; \
	 mkstubs -a 0x63b00000 -l libvga -v $(VERSION) -j 0x00001000 -g 4096 \
		-- libvga; \
	 mkstubs -a 0x63b00000 -l libvga -v $(VERSION) -j 0x00001000 -g 4096 \
		-- libvgagl; \
	 mkimage -a 0x63b00000 -l libvga -v $(VERSION) -j 0x00001000 -g 4096 \
		-- $(MKIMAGEFLAGS) libvga.a libvgagl.a \
		`$(CC) --print-libgcc-file-name` -lc -lm; \
	 strip libvga.so.$(VERSION) \
	)

endif	# DISTRIBUTION

sharedlib/libvga.a: $(SHAREDDIRS)
	(cd $(dir $@); \
	 $(MAKE) -f $(srcdir)/src/Makefile $(notdir $@) \
	 	srcdir="$(srcdir)" DLLFLAGS="$(DLLFLAGS)" \
	)

else

# ELF

.PHONY: sharedlib/libvga.so.$(VERSION) sharedlib/libvgagl.so.$(VERSION)

shared: $(SHAREDLIBS) $(SVGALIBSHAREDSTUBS)

sharedlib/libvga.so.$(VERSION): $(SHAREDDIRS)
	@rm -f sharedlib/DISTRIBUTION
	(cd $(dir $@); \
	 $(MAKE) -f $(srcdir)/src/Makefile $(notdir $@) \
	 	srcdir="$(srcdir)" DLLFLAGS="$(DLLFLAGS)" \
	)

endif	# a.out

sharedlib/libvgagl.a sharedlib/libvgagl.so.$(VERSION): $(SHAREDDIRS)
	(cd $(dir $@); \
	 $(MAKE) -f $(srcdir)/gl/Makefile $(notdir $@) \
	 	srcdir="$(srcdir)" DLLFLAGS="$(DLLFLAGS)" \
	)

demoprogs: $(PREDEMO) $(DEMODIRS)
	@for dir in $(DEMODIRS); do \
		if [ -d $(srcdir)/$$dir ]; then \
			(cd $$dir; \
			$(MAKE) -f $(srcdir)/$$dir/Makefile srcdir="$(srcdir)"); \
		fi; \
	done

textutils: $(UTILDIRS)
	(cd utils; \
	$(MAKE) -f $(srcdir)/utils/Makefile srcdir="$(srcdir)")

lrmi:
	(cd lrmi-0.6m;\
	$(MAKE))

backup: $(BACKUP)

configaout:
	@if grep '^TARGET_FORMAT = elf$$' $(srcdir)/Makefile.cfg >/dev/null; then \
		$(MAKE) clean; \
		sed 's/^TARGET_FORMAT = elf$$/# TARGET_FORMAT = elf/' \
			$(srcdir)/Makefile.cfg | \
		sed 's/^#[ 	]*TARGET_FORMAT = a.out$$/TARGET_FORMAT = a.out/' \
			> mkcfg ; \
		mv mkcfg $(srcdir)/Makefile.cfg; \
		echo "Was configured for elf, rerunning make"; \
		$(MAKE) clean backup; \
		echo "Ignore this error message:"; \
		exit 1; \
	fi

$(BACKUP): configaout
# I tried using a dependency, but make reordered them
# thus I have to do it this way:
	$(MAKE) shared
	$(MAKE) distclean

	sed 's/^TARGET_FORMAT = a.out$$/# TARGET_FORMAT = a.out/' \
		$(srcdir)/Makefile.cfg | \
	sed 's/^#[ 	]*TARGET_FORMAT = elf$$/TARGET_FORMAT = elf/' \
		> mkcfg ; \
	mv mkcfg $(srcdir)/Makefile.cfg; \
	(cd ..; \
	find svgalib-$(VERSION) ! -type d -print | sort | tar -cvf- -T- ) \
		| gzip -9 >$(BACKUP)

distclean:
	(cd $(srcdir)/doc; $(MAKE) clean)
	(cd $(srcdir)/doc; $(MAKE) ../0-README)
	(cd $(srcdir)/src; $(MAKE) clean)
	(cd $(srcdir)/gl; $(MAKE) clean)
	(cd $(srcdir)/utils; $(MAKE) clean)
	(cd $(srcdir)/demos; $(MAKE) clean)
	(cd $(srcdir)/mach; $(MAKE) clean)
	(cd $(srcdir)/threeDKit; $(MAKE) clean)
	(cd $(srcdir)/lrmi-0.6m; $(MAKE) clean)
	(cd $(srcdir)/jump; rm -f *.s jump.log jump.params jump.undefs)
	(cd jump; rm -f *.s jump.log jump.params jump.undefs)
	find . \( -name '.depend*' -o -name '*~*' \) -exec rm {} \;
	rm -rf sharedlib/[!l]* sharedlib/l[!i]* sharedlib/li[!b]* staticlib
	rm -rf sharedlib/*.a
	mkdir -p sharedlib
	touch sharedlib/DISTRIBUTION

clean: distclean
	rm -rf sharedlib

indent:
	find demos gl mach src support -name '*.[ch]' -exec indent -kr {} \;
	indent -kr src/*.regs

indent-gnu:
	find demos gl mach src support -name '*.[ch]' -exec indent -gnu {} \;
	indent -gnu src/*.regs

FORCE:
