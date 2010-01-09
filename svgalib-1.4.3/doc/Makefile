include ../Makefile.cfg

srcdir    = ..
VPATH     = $(srcdir)/doc

SVGALIB1=man7/svgalib.7 man5/libvga.config.5 man7/svgalib.chips.7\
	man7/svgalib.et4000.7 man7/svgalib.mach32.7\
	man6/mach32info.6 man3/vga_* man3/keyboard_* man3/mouse_* \
	man1/convfont.1 man1/dumpreg.1 man1/fix132x43.1 man1/restorefont.1 \
	man1/restorepalette.1 man1/restoretextmode.1 man1/runx.1 man1/savetextmode.1 \
	man1/setmclk.1 man1/textmode.1\
	man1/svgakeymap.1 \
	man6/eventtest.6 man6/forktest.6 man6/fun.6 man6/keytest.6\
	man6/mousetest.6 man6/speedtest.6 man6/spin.6\
	man6/testaccel.6 man6/threed.6 man6/vgatest.6 man6/accel.6\
	man6/bg_test.6 man6/scrolltest.6 man6/testlinear.6\
	man8/mode3.8 man6/lineart.6

VGAGL1=man7/vgagl.7 $(shell ls man3/gl_* | grep -v tri ) man6/testgl.6

THREED1=man7/threedkit.7 man3/gl_*tri* man6/plane.6 man6/wrapdemo.6

SVGALIB=$(subst man,$(srcdir)/doc/man,$(SVGALIB1))
VGAGL=$(subst man,$(srcdir)/doc/man,$(VGAGL1))
THREED=$(subst man,$(srcdir)/doc/man,$(THREED1))
MANPAGES=$(shell cd $(srcdir)/doc; find man? -type f -print)
UNCOMANPAGES=$(subst .gz, , $(MANPAGES))

.PHONY: clean ascii dvi ps install gunzip gzip

gunzip:
	for dir in man?; do\
		echo gunzipping manpages in $$dir;\
		for file in $$dir/*; do \
			gunzip $$file; \
		done; \
	done

gzip:
	for dir in man?; do\
		echo gzipping manpages in $$dir;\
		for file in $$dir/*; do \
			gzip $$file; \
		done; \
	done

../0-README: man7/svgalib.7
	cat man7/svgalib.7 | nroff -man | col -b > ../0-README

ascii:
	cat $(SVGALIB) | grep -v '^\.so ' | nroff -man -T$@ \
		| col -b > svgalib.$@
	cat $(VGAGL) | grep -v '^\.so ' | nroff -man -T$@ \
		| col -b > vgagl.$@
	cat $(THREED) | grep -v '^\.so ' | nroff -man -T$@ \
		| col -b > 3d.$@

lpr:
	cat $(SVGALIB) | grep -v '^\.so ' | nroff -man -Tascii > svgalib.lpr
	cat $(VGAGL) | grep -v '^\.so ' | nroff -man -Tascii > vgagl.lpr
	cat $(THREED) | grep -v '^\.so ' | nroff -man -Tascii > 3d.lpr

ps dvi:
	cat $(SVGALIB) | grep -v '^\.so ' | troff -man -T$@ > svgalib.$@
	cat $(VGAGL) | grep -v '^\.so ' | troff -man -T$@ > vgagl.$@
	cat $(THREED) | grep -v '^\.so ' | troff -man -T$@ > 3d.$@

install: gzip
	mkdir -p $(mandir)/man1 $(mandir)/man3 $(mandir)/man5\
		 $(mandir)/man6 $(mandir)/man7 $(mandir)/man8
	$(INSTALL_DATA) $(srcdir)/doc/man1/* $(mandir)/man1
	$(INSTALL_DATA) $(srcdir)/doc/man3/* $(mandir)/man3
	$(INSTALL_DATA) $(srcdir)/doc/man5/* $(mandir)/man5
	$(INSTALL_DATA) $(srcdir)/doc/man6/* $(mandir)/man6
	$(INSTALL_DATA) $(srcdir)/doc/man7/* $(mandir)/man7
	$(INSTALL_DATA) $(srcdir)/doc/man8/* $(mandir)/man8
	for dir in man?; do\
		echo gunzipping manpages in $$dir;\
		for file in $$dir/*; do \
			gunzip $$file; \
		done; \
	done
ifdef MAKEWHATIS
	@echo "Rebuilding the whatis database (this is slow!)"
	@echo "You can comment this out in Makefile.cfg!"
	$(MAKEWHATIS)
endif

uninstall:
	@echo Removing all svgalib man pages.
	@for dir in $(mandir) /usr/man /usr/local/man; do \
		cd $$dir; \
		rm -f $(MANPAGES) $(UNCOMANPAGES); \
	done;\

clean:
#	-gzip -9 man?/* 2>/dev/null
	rm -f man?/*.bak
	rm -f svgalib.ascii vgagl.ascii 3d.ascii
	rm -f svgalib.dvi vgagl.dvi 3d.dvi
	rm -f svgalib.ps vgagl.ps 3d.ps
	rm -f svgalib.lpr vgagl.lpr 3d.lpr
