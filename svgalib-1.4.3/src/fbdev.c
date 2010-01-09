#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "vga.h"
#include "libvga.h"
#include "driver.h"
#include "timing.h"
#include "interface.h"

extern unsigned char * B8000_MEM_POINTER;

static int fbdev_fd;
static size_t fbdev_memory;
static size_t fbdev_startaddressrange;
static CardSpecs *cardspecs;
static struct console_font_op fbdev_font;
static struct fb_var_screeninfo fbdev_textmode;

/* Card Specs */

static int fbdev_match_programmable_clock(int clock)
{
	return clock;
}

static int fbdev_map_clock(int bpp, int clock)
{
	return clock;
}

static int fbdev_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
	return htiming;
}

/* Driver Specs */

static int fbdev_saveregs(unsigned char *regs)
{
	ioctl(fbdev_fd, FBIOGET_VSCREENINFO, regs+EXT);
	return sizeof(struct fb_var_screeninfo);
}

static void fbdev_setregs(const unsigned char *regs, int mode)
{
	ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, regs+EXT);
}

static void fbdev_unlock(void)
{
}

static void fbdev_lock(void)
{
}

static int fbdev_init(int force, int par1, int par2)
{
	struct fb_fix_screeninfo info;
	int fd;

	if ((fd = open("/dev/fb0", O_RDWR)) < 0)
	{
		return -1;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &info))
	{
		close(fd);
		return -1;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &fbdev_textmode))
	{
		close(fd);
		return -1;
	}

	fbdev_memory = info.smem_len;
	fbdev_fd = fd;

	fbdev_startaddressrange = 65536;
	while (fbdev_startaddressrange < fbdev_memory)
	{
		fbdev_startaddressrange <<= 1;
	}
	fbdev_startaddressrange -= 1;

	cardspecs = malloc(sizeof(CardSpecs));
	cardspecs->videoMemory = fbdev_memory;

/* FIXME: autodetect these */
	cardspecs->maxPixelClock4bpp = 200000;
	cardspecs->maxPixelClock8bpp = 200000;
	cardspecs->maxPixelClock16bpp = 200000;
	cardspecs->maxPixelClock24bpp = 200000;
	cardspecs->maxPixelClock32bpp = 200000;
	cardspecs->maxHorizontalCrtc = 8192;

	cardspecs->flags = CLOCK_PROGRAMMABLE;

	cardspecs->nClocks = 0;
	cardspecs->clocks = 0;

	cardspecs->mapClock = fbdev_map_clock;
	cardspecs->matchProgrammableClock = fbdev_match_programmable_clock;
	cardspecs->mapHorizontalCrtc = fbdev_map_horizontal_crtc;

	__svgalib_driverspecs = &__svgalib_fbdev_driverspecs;
	__svgalib_banked_mem_base = info.smem_start;
	__svgalib_banked_mem_size = 0x10000;
	__svgalib_linear_mem_base = info.smem_start;
	__svgalib_linear_mem_size = fbdev_memory;

	__svgalib_novga = 1;
	B8000_MEM_POINTER = (void *)0xB8000;
	BANKED_MEM_POINTER = mmap(0, __svgalib_banked_mem_size,
				  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	LINEAR_MEM_POINTER = mmap(0, __svgalib_linear_mem_size,
				  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	return 0;
}

static int fbdev_test(void)
{
	return !fbdev_init(0, 0, 0);
}

static void fbdev_setpage(int page)
{
	static int oldpage = 0;

	if (page != oldpage)
	{
//		munmap(BANKED_MEM_POINTER, __svgalib_banked_mem_size);
		mmap(BANKED_MEM_POINTER, __svgalib_banked_mem_size,
		     PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
		     fbdev_fd, page * __svgalib_banked_mem_size);
		oldpage = page;
	}
}

static int fbdev_screeninfo(struct fb_var_screeninfo *info, int mode)
{
	ModeTiming modetiming;
	ModeInfo *modeinfo;

	/* The VGA modes don't work */

	if ((mode < G640x480x256 ) || mode == G720x348x2)
		return 1;

	if (ioctl(fbdev_fd, FBIOGET_VSCREENINFO, info))
		return 1;

	modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

	if (__svgalib_getmodetiming(&modetiming, modeinfo, cardspecs))
	{
		free(modeinfo);
		return 1;
	}

	info->xres = modeinfo->width;
	info->yres = modeinfo->height;
	info->xres_virtual = modeinfo->width;
	info->yres_virtual = modeinfo->height;
	info->xoffset = 0;
	info->yoffset = 0;
	info->bits_per_pixel = modeinfo->bitsPerPixel;
	info->grayscale = 0;
	info->red.offset = modeinfo->redOffset;
	info->red.length = modeinfo->redWeight;
	info->red.msb_right = 0;
	info->green.offset = modeinfo->greenOffset;
	info->green.length = modeinfo->greenWeight;
	info->green.msb_right = 0;
	info->blue.offset = modeinfo->blueOffset;
	info->blue.length = modeinfo->blueWeight;
	info->blue.msb_right = 0;
	info->nonstd = 0;

	free(modeinfo);

	info->vmode &= FB_VMODE_MASK;

	if (modetiming.flags & INTERLACED)
		info->vmode |= FB_VMODE_INTERLACED;
	if (modetiming.flags & DOUBLESCAN)
	{
		info->vmode |= FB_VMODE_DOUBLE;
		modetiming.VDisplay >>= 1;
		modetiming.VSyncStart >>= 1;
		modetiming.VSyncEnd >>= 1;
		modetiming.VTotal >>= 1;
	}

	info->pixclock = 1000000000 / modetiming.pixelClock;
	info->left_margin = modetiming.HTotal - modetiming.HSyncEnd;
	info->right_margin = modetiming.HSyncStart - modetiming.HDisplay;
	info->upper_margin = modetiming.VTotal - modetiming.VSyncEnd;
	info->lower_margin = modetiming.VSyncStart - modetiming.VDisplay;
	info->hsync_len = modetiming.HSyncEnd - modetiming.HSyncStart;
	info->vsync_len = modetiming.VSyncEnd - modetiming.VSyncStart;

	info->sync &= ~(FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT);

	if (modetiming.flags & PHSYNC)
		info->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (modetiming.flags & PVSYNC)
		info->sync |= FB_SYNC_VERT_HIGH_ACT;

	return 0;
}

static void fbdev_set_virtual_height(struct fb_var_screeninfo *info)
{
	int maxpixels = fbdev_memory;
	int bytesperpixel = info->bits_per_pixel / 8;
 
	if (bytesperpixel)
		maxpixels /= bytesperpixel;

	info->yres_virtual = maxpixels / info->xres_virtual;
}

static int fbdev_setmode(int mode, int prv_mode)
{
	struct fb_var_screeninfo info;

	if (mode == TEXT)
	{
		if (ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, &fbdev_textmode))
			return 1;

		memset(LINEAR_MEM_POINTER, 0,
		       fbdev_textmode.bits_per_pixel ?
		       fbdev_textmode.xres_virtual *
		       fbdev_textmode.yres_virtual *
		       fbdev_textmode.bits_per_pixel / 8 : 65536);
	}
	else
	{
		if (fbdev_screeninfo(&info, mode))
			return 1;

		fbdev_set_virtual_height(&info);

		if (ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, &info))
			return 1;
	}

	return 0;
}

static int fbdev_modeavailable(int mode)
{
	struct fb_var_screeninfo info;
	unsigned g;

	if (fbdev_screeninfo(&info, mode))
		return 0;

	g = info.green.length;

	info.activate = FB_ACTIVATE_TEST;
	if (ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, &info))
		return 0;

	if (info.bits_per_pixel == 16 &&
	    info.green.length != g)
		return 0;

	/* We may need to add more checks here. */

	return SVGADRV;
}

static void fbdev_setdisplaystart(int address)
{
	struct fb_var_screeninfo info;

	if (ioctl(fbdev_fd, FBIOGET_VSCREENINFO, &info))
		return;

	info.xoffset = address % info.xres_virtual;
	info.yoffset = address / info.xres_virtual;

	ioctl(fbdev_fd, FBIOPAN_DISPLAY, &info);
}

static void fbdev_setlogicalwidth(int width)
{
	struct fb_var_screeninfo info;

	if (ioctl(fbdev_fd, FBIOGET_VSCREENINFO, &info))
		return;

	info.xres_virtual = width;
	fbdev_set_virtual_height(&info);

	ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, &info);
}

static void fbdev_getmodeinfo(int mode, vga_modeinfo * modeinfo)
{
	struct fb_var_screeninfo info;
	int maxpixels = fbdev_memory;

	if (modeinfo->bytesperpixel)
		maxpixels /= modeinfo->bytesperpixel;

	modeinfo->maxlogicalwidth = maxpixels / modeinfo->height;
	modeinfo->startaddressrange = fbdev_startaddressrange;
	modeinfo->maxpixels = maxpixels;
	modeinfo->haveblit = 0;
	modeinfo->flags |= (__svgalib_modeinfo_linearset | CAPABLE_LINEAR); 

	if (fbdev_screeninfo(&info, mode))
		return;

	info.activate = FB_ACTIVATE_TEST;
	if (ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, &info))
		return;

	modeinfo->linewidth = info.xres_virtual * info.bits_per_pixel / 8;
}

static int fbdev_linear(int op, int param)
{
	switch(op)
	{
	case LINEAR_QUERY_BASE:
		return __svgalib_linear_mem_base;
	case LINEAR_ENABLE:
	case LINEAR_DISABLE:
		return 0;
	}
	return -1;
}

/* Emulation */

static void fbdev_savepalette(unsigned char *red,
			unsigned char *green,
			unsigned char *blue)
{
	__u16 r[256], g[256], b[256], t[256];
	struct fb_cmap cmap;
	unsigned i;

	cmap.start = 0;
	cmap.len = 256;
	cmap.red = r;
	cmap.green = g;
	cmap.blue = b;
	cmap.transp = t;

	if (ioctl(fbdev_fd, FBIOGETCMAP, &cmap))
		return;

	for (i = 0; i < 256; i++)
	{
		red[i] = r[i] >> 10;
		green[i] = g[i] >> 10;
		blue[i] = b[i] >> 10;
	}
}

static void fbdev_restorepalette(const unsigned char *red,
			   const unsigned char *green,
			   const unsigned char *blue)
{
	__u16 r[256], g[256], b[256], t[256];
	struct fb_cmap cmap;
	unsigned i;

	for (i = 0; i < 256; i++)
	{
		r[i] = (red[i] << 10) | (red[i] << 4) | (red[i] >> 2);
		g[i] = (green[i] << 10) | (green[i] << 4) | (green[i] >> 2);
		b[i] = (blue[i] << 10) | (blue[i] << 4) | (blue[i] >> 2);
		t[i] = 0;
	}

	cmap.start = 0;
	cmap.len = 256;
	cmap.red = r;
	cmap.green = g;
	cmap.blue = b;
	cmap.transp = t;

	ioctl(fbdev_fd, FBIOPUTCMAP, &cmap);
}

static int fbdev_setpalette(int index, int red, int green, int blue)
{
	__u16 r, g, b, t;
	struct fb_cmap cmap;

	r = (red << 10) | (red << 4) | (red >> 2);
	g = (green << 10) | (green << 4) | (green >> 2);
	b = (blue << 10) | (blue << 4) | (blue >> 2);
	t = 0;

	cmap.start = index;
	cmap.len = 1;
	cmap.red = &r;
	cmap.green = &g;
	cmap.blue = &b;
	cmap.transp = &t;
	ioctl(fbdev_fd, FBIOPUTCMAP, &cmap);

	return 0;
}

static void fbdev_getpalette(int index, int *red, int *green, int *blue)
{
	__u16 r, g, b, t;
	struct fb_cmap cmap;

	cmap.start = 0;
	cmap.len = 1;
	cmap.red = &r;
	cmap.green = &g;
	cmap.blue = &b;
	cmap.transp = &t;

	if (ioctl(fbdev_fd, FBIOGETCMAP, &cmap))
		return;

	*red = r >> 10;
	*green = g >> 10;
	*blue = b >> 10;
}

static void fbdev_savefont(void)
{
	fbdev_font.op = KD_FONT_OP_GET;
	fbdev_font.flags = 0;
	fbdev_font.width = 32;
	fbdev_font.height = 32;
	fbdev_font.charcount = 512;
	fbdev_font.data = malloc(65536);

	ioctl(fbdev_fd, KDFONTOP, &fbdev_font);
}

static void fbdev_restorefont(void)
{
	struct fb_var_screeninfo info;

	fbdev_font.op = KD_FONT_OP_SET;
	ioctl(fbdev_fd, KDFONTOP, &fbdev_font);
}

static void fbdev_waitretrace(void)
{
	/* This is from SDL */
#ifdef FBIOWAITRETRACE
	ioctl(console_fd, FBIOWAITRETRACE, 0);
#endif
}

/* Function tables */

static Emulation fbdev_vgaemul = 
{
	fbdev_savepalette,
	fbdev_restorepalette,
	fbdev_setpalette,
	fbdev_getpalette,
	fbdev_savefont,
	fbdev_restorefont,
	0,				/* screenoff */
	0,				/* screenon */
	fbdev_waitretrace
};

DriverSpecs __svgalib_fbdev_driverspecs =
{
	fbdev_saveregs,
	fbdev_setregs,
	fbdev_unlock,
	fbdev_lock,
	fbdev_test,
	fbdev_init,
	fbdev_setpage,
	0,				/* setrdpage */
	0,				/* setwrpage */
	fbdev_setmode,
	fbdev_modeavailable,
	fbdev_setdisplaystart,
	fbdev_setlogicalwidth,
	fbdev_getmodeinfo,
	0,				/* bitblt */
	0,				/* imageblt */
	0,				/* fillblt */
	0,				/* hlinelistblt */
	0,				/* bltwait */
	0,				/* extset */
	0,				/* accel */
	fbdev_linear,
	0,				/* Accelspecs */
	&fbdev_vgaemul
};
