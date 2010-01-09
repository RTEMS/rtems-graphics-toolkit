/* VGAlib version 1.2 - (c) 1993 Tommy Frandsen                    */
/*                                                                 */
/* This library is free software; you can redistribute it and/or   */
/* modify it without any restrictions. This library is distributed */
/* in the hope that it will be useful, but without any warranty.   */

/* Multi-chipset support Copyright (C) 1993 Harm Hanemaayer */
/* partially copyrighted (C) 1993 by Hartmut Schirmer */
/* Changes by Michael Weller. */
/* Modified by Don Secrest to include Tseng ET6000 handling */
/* Changes around the config things by 101 (Attila Lendvai) */

/* The code is a bit of a mess; also note that the drawing functions */
/* are not speed optimized (the gl functions are much faster). */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/kd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/vt.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"
#include "vgapci.h"
#include "mouse/vgamouse.h"
#include "keyboard/vgakeyboard.h"
#include "vgaregs.h"
#include "vgaversion.h"

#ifdef BACKGROUND
#include "vgabg.h"

/* ugly check to prevent usage on kernel versions < 2.0.36 and < 2.1.123
 * should this also be runtime checked?
 *
 * Why not earlier kernels?
 * The following sequence of mmap() calls is deadly in these versions
 * due to a kernel bug:
 * #define MAP_ADDR 0xfd000000  // high mem area, eg. framebuf
 * #define MAP_SIZE 0x200000
 * map_a=mmap(0,MAP_SIZE,PROT_READ|PROT_WRITE,
 *            MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 * map_b=mmap(0,MAP_SIZE,PROT_READ | PROT_WRITE,
 *            MAP_SHARED,dev_mem_fd,MAP_ADDR);
 * map_c=mmap(map_a,MAP_SIZE,PROT_READ | PROT_WRITE,
 *           MAP_SHARED | MAP_FIXED,proc_self_mem_fd,
 *           map_b);
 * The map_c mmapping will destroy random kernel data. A similar
 * mmap sequence is done when calling __svgalib_map_virtual_screen()
 * to map the linear framebuffer virtual address to the final
 * virtual address (which then can mapped to backing storage on VC switch,
 * transparently for the application).
 */
#include <linux/version.h>
#define LNX_MAJ_VER ((LINUX_VERSION_CODE >> 16) & 255)
#define LNX_MED_VER ((LINUX_VERSION_CODE >> 8) & 255)
#define LNX_MIN_VER (LINUX_VERSION_CODE & 255)
#if (LNX_MAJ_VER < 2) || \
    ( (LNX_MAJ_VER == 2) && (LNX_MED_VER == 0) && (LNX_MIN_VER < 36) ) || \
    ( (LNX_MAJ_VER == 2) && (LNX_MED_VER == 1) && (LNX_MIN_VER < 123) ) || \
    ( (LNX_MAJ_VER == 2) && (LNX_MED_VER == 3) && (LNX_MIN_VER > 26) )
#warning BACKGROUND *not* supported on kernel versions < 2.0.36 or < 2.1.123,
#warning or version>2.3.26. Compile without BACKGROUND.
#endif
#endif /* #ifdef BACKGROUND */

/* Delay in microseconds after a mode is set (screen is blanked during this */
/* time), allows video signals to stabilize */
#define MODESWITCHDELAY 150000

/* Define this to disable video output during mode switches, in addition to */
/* 'turning off the screen', which is always done. */
/* Doesn't look very nice on my Cirrus. */
/* #define DISABLE_VIDEO_OUTPUT */

/* #define DONT_WAIT_VC_ACTIVE */

/* Use /dev/tty instead of /dev/tty0 (the previous behaviour may have been
 * silly). */
#define USE_DEVTTY

#define SETSIG(sa, sig, fun) {\
	sa.sa_handler = fun; \
	sa.sa_flags = SA_RESTART; \
	zero_sa_mask(&(sa.sa_mask)); \
	sigaction(sig, &sa, NULL); \
}

#ifdef INCLUDE_VESA_DRIVER
extern int __svgalib_lrmi_cpu_type;
#endif

/* variables used to shift between monchrome and color emulation */

int __svgalib_CRT_I;			/* current CRT index register address */
int __svgalib_CRT_D;			/* current CRT data register address */
int __svgalib_IS1_R;			/* current input status register address */
static int color_text;		/* true if color text emulation */

unsigned char * BANKED_MEM_POINTER=NULL, * LINEAR_MEM_POINTER, *MMIO_POINTER;
unsigned char * B8000_MEM_POINTER=NULL;
unsigned long int __svgalib_banked_mem_base, __svgalib_banked_mem_size;
unsigned long int __svgalib_mmio_base, __svgalib_mmio_size=0;
unsigned long int __svgalib_linear_mem_base=0, __svgalib_linear_mem_size=0;

/* If == 0 then nothing is defined by the user... */
int __svgalib_default_mode = 0;

struct info infotable[] =
{
    {80, 25, 16, 160, 0},	/* VGAlib VGA modes */
    {320, 200, 16, 40, 0},
    {640, 200, 16, 80, 0},
    {640, 350, 16, 80, 0},
    {640, 480, 16, 80, 0},
    {320, 200, 256, 320, 1},
    {320, 240, 256, 80, 0},
    {320, 400, 256, 80, 0},
    {360, 480, 256, 90, 0},
    {640, 480, 2, 80, 0},

    {640, 480, 256, 640, 1},	/* VGAlib SVGA modes */
    {800, 600, 256, 800, 1},
    {1024, 768, 256, 1024, 1},
    {1280, 1024, 256, 1280, 1},

    {320, 200, 1 << 15, 640, 2},	/* Hicolor/truecolor modes */
    {320, 200, 1 << 16, 640, 2},
    {320, 200, 1 << 24, 320 * 3, 3},
    {640, 480, 1 << 15, 640 * 2, 2},
    {640, 480, 1 << 16, 640 * 2, 2},
    {640, 480, 1 << 24, 640 * 3, 3},
    {800, 600, 1 << 15, 800 * 2, 2},
    {800, 600, 1 << 16, 800 * 2, 2},
    {800, 600, 1 << 24, 800 * 3, 3},
    {1024, 768, 1 << 15, 1024 * 2, 2},
    {1024, 768, 1 << 16, 1024 * 2, 2},
    {1024, 768, 1 << 24, 1024 * 3, 3},
    {1280, 1024, 1 << 15, 1280 * 2, 2},
    {1280, 1024, 1 << 16, 1280 * 2, 2},
    {1280, 1024, 1 << 24, 1280 * 3, 3},

    {800, 600, 16, 100, 0},	/* SVGA 16-color modes */
    {1024, 768, 16, 128, 0},
    {1280, 1024, 16, 160, 0},

    {720, 348, 2, 90, 0},	/* Hercules emulation mode */

    {320, 200, 1 << 24, 320 * 4, 4},
    {640, 480, 1 << 24, 640 * 4, 4},
    {800, 600, 1 << 24, 800 * 4, 4},
    {1024, 768, 1 << 24, 1024 * 4, 4},
    {1280, 1024, 1 << 24, 1280 * 4, 4},

    {1152, 864, 16, 144, 0},
    {1152, 864, 256, 1152, 1},
    {1152, 864, 1 << 15, 1152 * 2, 2},
    {1152, 864, 1 << 16, 1152 * 2, 2},
    {1152, 864, 1 << 24, 1152 * 3, 3},
    {1152, 864, 1 << 24, 1152 * 4, 4},

    {1600, 1200, 16, 200, 0},
    {1600, 1200, 256, 1600, 1},
    {1600, 1200, 1 << 15, 1600 * 2, 2},
    {1600, 1200, 1 << 16, 1600 * 2, 2},
    {1600, 1200, 1 << 24, 1600 * 3, 3},
    {1600, 1200, 1 << 24, 1600 * 4, 4},

    {320, 240, 256, 320, 1},	
    {320, 240, 1<<15, 320*2, 2},
    {320, 240, 1<<16, 320*2, 2},
    {320, 240, 1<<24, 320*3, 3},
    {320, 240, 1<<24, 320*4, 4},
     
    {400, 300, 256, 400, 1},
    {400, 300, 1<<15, 400*2, 2},
    {400, 300, 1<<16, 400*2, 2},
    {400, 300, 1<<24, 400*3, 3},
    {400, 300, 1<<24, 400*4, 4},
     
    {512, 384, 256, 512, 1},		
    {512, 384, 1<<15, 512*2, 2},
    {512, 384, 1<<16, 512*2, 2},
    {512, 384, 1<<24, 512*3, 3},
    {512, 384, 1<<24, 512*4, 4},

    {960, 720, 256, 960, 1},		
    {960, 720, 1<<15, 960*2, 2},
    {960, 720, 1<<16, 960*2, 2},
    {960, 720, 1<<24, 960*3, 3},
    {960, 720, 1<<24, 960*4, 4},

    {1920, 1440, 256, 1920, 1},		
    {1920, 1440, 1<<15, 1920*2, 2},
    {1920, 1440, 1<<16, 1920*2, 2},
    {1920, 1440, 1<<24, 1920*3, 3},
    {1920, 1440, 1<<24, 1920*4, 4},

    {320, 400, 1<<8,  320,   1},
    {320, 400, 1<<15, 320*2, 2},
    {320, 400, 1<<16, 320*2, 2},
    {320, 400, 1<<24, 320*3, 3},
    {320, 400, 1<<24, 320*4, 4},

    {640, 400, 256, 640, 1},
    {640, 400, 1<<15, 640*2, 2},
    {640, 400, 1<<16, 640*2, 2},
    {640, 400, 1<<24, 640*3, 3},
    {640, 400, 1<<24, 640*4, 4},

    {320, 480, 256, 320, 1},
    {320, 480, 1<<15, 320*2, 2},
    {320, 480, 1<<16, 320*2, 2},
    {320, 480, 1<<24, 320*3, 3},
    {320, 480, 1<<24, 320*4, 4},

    {720, 540, 256, 720, 1},
    {720, 540, 1<<15, 720*2, 2},
    {720, 540, 1<<16, 720*2, 2},
    {720, 540, 1<<24, 720*3, 3},
    {720, 540, 1<<24, 720*4, 4},

    {848, 480, 256, 848, 1},
    {848, 480, 1<<15, 848*2, 2},
    {848, 480, 1<<16, 848*2, 2},
    {848, 480, 1<<24, 848*3, 3},
    {848, 480, 1<<24, 848*4, 4},

    {1072, 600, 256, 1072, 1},
    {1072, 600, 1<<15, 1072*2, 2},
    {1072, 600, 1<<16, 1072*2, 2},
    {1072, 600, 1<<24, 1072*3, 3},
    {1072, 600, 1<<24, 1072*4, 4},

    {1280, 720, 256, 1280, 1},
    {1280, 720, 1<<15, 1280*2, 2},
    {1280, 720, 1<<16, 1280*2, 2},
    {1280, 720, 1<<24, 1280*3, 3},
    {1280, 720, 1<<24, 1280*4, 4},

    {1360, 768, 256, 1360, 1},
    {1360, 768, 1<<15, 1360*2, 2},
    {1360, 768, 1<<16, 1360*2, 2},
    {1360, 768, 1<<24, 1360*3, 3},
    {1360, 768, 1<<24, 1360*4, 4},

    {1800, 1012, 256, 1800, 1},
    {1800, 1012, 1<<15, 1800*2, 2},
    {1800, 1012, 1<<16, 1800*2, 2},
    {1800, 1012, 1<<24, 1800*3, 3},
    {1800, 1012, 1<<24, 1800*4, 4},

    {1920, 1080, 256, 1920, 1},
    {1920, 1080, 1<<15, 1920*2, 2},
    {1920, 1080, 1<<16, 1920*2, 2},
    {1920, 1080, 1<<24, 1920*3, 3},
    {1920, 1080, 1<<24, 1920*4, 4},

    {2048, 1152, 256, 2048, 1},
    {2048, 1152, 1<<15, 2048*2, 2},
    {2048, 1152, 1<<16, 2048*2, 2},
    {2048, 1152, 1<<24, 2048*3, 3},
    {2048, 1152, 1<<24, 2048*4, 4},

    {2048, 1536, 256, 2048, 1},
    {2048, 1536, 1<<15, 2048*2, 2},
    {2048, 1536, 1<<16, 2048*2, 2},
    {2048, 1536, 1<<24, 2048*3, 3},
    {2048, 1536, 1<<24, 2048*4, 4},

    {512, 480, 256, 512, 1},		
    {512, 480, 1<<15, 512*2, 2},
    {512, 480, 1<<16, 512*2, 2},
    {512, 480, 1<<24, 512*3, 3},
    {512, 480, 1<<24, 512*4, 4},

    {400, 600, 256, 400, 1},
    {400, 600, 1<<15, 400*2, 2},
    {400, 600, 1<<16, 400*2, 2},
    {400, 600, 1<<24, 400*3, 3},
    {400, 600, 1<<24, 400*4, 4},

    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0}
};

#define MAX_MODES (sizeof(infotable) / sizeof(struct info))

#ifndef BACKGROUND
void (*__svgalib_go_to_background) (void) = 0;
void (*__svgalib_come_from_background) (void) = 0;
static int release_acquire=0;
#endif /* BACKGROUND */

unsigned long __svgalib_graph_base = GRAPH_BASE;

unsigned char __svgalib_novga = 0;     /* Does not have VGA circuitry on board */
unsigned char __svgalib_vesatext = 0;
unsigned char __svgalib_textprog = 0;  /* run a program when returning to text mode */
unsigned char __svgalib_secondary = 0; /* this is not the main card with VC'S (not yet supported) */
unsigned char __svgalib_novccontrol = 0; /* this is not the main card with VC'S (not yet supported) */
unsigned char __svgalib_simple = 0;
unsigned char __svgalib_ragedoubleclock = 0;

/* default palette values */
static const unsigned char default_red[256]
=
{0, 0, 0, 0, 42, 42, 42, 42, 21, 21, 21, 21, 63, 63, 63, 63,
 0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63,
 0, 16, 31, 47, 63, 63, 63, 63, 63, 63, 63, 63, 63, 47, 31, 16,
 0, 0, 0, 0, 0, 0, 0, 0, 31, 39, 47, 55, 63, 63, 63, 63,
 63, 63, 63, 63, 63, 55, 47, 39, 31, 31, 31, 31, 31, 31, 31, 31,
 45, 49, 54, 58, 63, 63, 63, 63, 63, 63, 63, 63, 63, 58, 54, 49,
 45, 45, 45, 45, 45, 45, 45, 45, 0, 7, 14, 21, 28, 28, 28, 28,
 28, 28, 28, 28, 28, 21, 14, 7, 0, 0, 0, 0, 0, 0, 0, 0,
 14, 17, 21, 24, 28, 28, 28, 28, 28, 28, 28, 28, 28, 24, 21, 17,
 14, 14, 14, 14, 14, 14, 14, 14, 20, 22, 24, 26, 28, 28, 28, 28,
 28, 28, 28, 28, 28, 26, 24, 22, 20, 20, 20, 20, 20, 20, 20, 20,
 0, 4, 8, 12, 16, 16, 16, 16, 16, 16, 16, 16, 16, 12, 8, 4,
 0, 0, 0, 0, 0, 0, 0, 0, 8, 10, 12, 14, 16, 16, 16, 16,
 16, 16, 16, 16, 16, 14, 12, 10, 8, 8, 8, 8, 8, 8, 8, 8,
 11, 12, 13, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 15, 13, 12,
 11, 11, 11, 11, 11, 11, 11, 11, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned char default_green[256]
=
{0, 0, 42, 42, 0, 0, 21, 42, 21, 21, 63, 63, 21, 21, 63, 63,
 0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 31, 47, 63, 63, 63, 63,
 63, 63, 63, 63, 63, 47, 31, 16, 31, 31, 31, 31, 31, 31, 31, 31,
 31, 39, 47, 55, 63, 63, 63, 63, 63, 63, 63, 63, 63, 55, 47, 39,
 45, 45, 45, 45, 45, 45, 45, 45, 45, 49, 54, 58, 63, 63, 63, 63,
 63, 63, 63, 63, 63, 58, 54, 49, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 7, 14, 21, 29, 28, 28, 28, 28, 28, 28, 28, 28, 21, 14, 7,
 14, 14, 14, 14, 14, 14, 14, 14, 14, 17, 21, 24, 28, 28, 28, 28,
 28, 28, 28, 28, 28, 24, 21, 17, 20, 20, 20, 20, 20, 20, 20, 20,
 20, 22, 24, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 26, 24, 22,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 8, 12, 16, 16, 16, 16,
 16, 16, 16, 16, 16, 12, 8, 4, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 10, 12, 14, 16, 16, 16, 16, 16, 16, 16, 16, 16, 14, 12, 10,
 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 13, 15, 16, 16, 16, 16,
 16, 16, 16, 16, 16, 15, 13, 12, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned char default_blue[256]
=
{0, 42, 0, 42, 0, 42, 0, 42, 21, 63, 21, 63, 21, 63, 21, 63,
 0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63,
 63, 63, 63, 63, 63, 47, 31, 16, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 16, 31, 47, 63, 63, 63, 63, 63, 63, 63, 63, 63, 55, 47, 39,
 31, 31, 31, 31, 31, 31, 31, 31, 31, 39, 47, 55, 63, 63, 63, 63,
 63, 63, 63, 63, 63, 58, 54, 49, 45, 45, 45, 45, 45, 45, 45, 45,
 45, 49, 54, 58, 63, 63, 63, 63, 28, 28, 28, 28, 28, 21, 14, 7,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 14, 21, 28, 28, 28, 28,
 28, 28, 28, 28, 28, 24, 21, 17, 14, 14, 14, 14, 14, 14, 14, 14,
 14, 17, 21, 24, 28, 28, 28, 28, 28, 28, 28, 28, 28, 26, 24, 22,
 20, 20, 20, 20, 20, 20, 20, 20, 20, 22, 24, 26, 28, 28, 28, 28,
 16, 16, 16, 16, 16, 12, 8, 4, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 4, 8, 12, 16, 16, 16, 16, 16, 16, 16, 16, 16, 14, 12, 10,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 10, 12, 14, 16, 16, 16, 16,
 16, 16, 16, 16, 16, 15, 13, 12, 11, 11, 11, 11, 11, 11, 11, 11,
 11, 12, 13, 15, 16, 16, 16, 16, 0, 0, 0, 0, 0, 0, 0, 0};


static unsigned char text_regs[MAX_REGS];	/* VGA registers for saved text mode */
static unsigned char graph_regs[MAX_REGS];	/* VGA registers for svgalib mode */

char *__svgalib_TextProg_argv[16]; /* should be enough */
char *__svgalib_TextProg;

/* saved text mode palette values */
static unsigned char text_red[256];
static unsigned char text_green[256];
static unsigned char text_blue[256];

/* saved graphics mode palette values */
static unsigned char graph_red[256];
static unsigned char graph_green[256];
static unsigned char graph_blue[256];

static int prv_mode = TEXT;	/* previous video mode      */
static int flip_mode = TEXT;	/* flipped video mode       */

int CM = TEXT;			/* current video mode       */
struct info CI;			/* current video parameters */
int COL;			/* current color            */


static int initialized = 0;	/* flag: initialize() called ?  */
static int flip = 0;		/* flag: executing vga_flip() ? */

/* svgalib additions: */

int __svgalib_chipset = UNDEFINED;
int __svgalib_driver_report = 1;
	/* report driver used after chipset detection */
int __svgalib_videomemoryused = -1;
int __svgalib_modeX = 0;	/* true after vga_setmodeX() */
int __svgalib_modeflags = 0;	/* copy of flags for current mode */
int __svgalib_critical = 0;	/* indicates blitter is busy */
int __svgalib_screenon = 1;	/* screen visible if != 0 */
RefreshRange __svgalib_horizsync =
{31500U, 0U};			/* horz. refresh (Hz) min, max */
RefreshRange __svgalib_vertrefresh =
{50U, 70U};			/* vert. refresh (Hz) min, max */
int __svgalib_bandwidth=50000;  /* monitor maximum bandwidth (kHz) */
int __svgalib_grayscale = 0;	/* grayscale vs. color mode */
int __svgalib_modeinfo_linearset = 0;	/* IS_LINEAR handled via extended vga_modeinfo */
const int __svgalib_max_modes = MAX_MODES;	/* Needed for dynamical allocated tables in mach32.c */

static unsigned __svgalib_maxhsync[] =
{
    31500, 35100, 35500, 37900, 48300, 56000, 60000
};

static int lastmodenumber = __GLASTMODE;	/* Last defined mode */
static int my_pid = 0;		/* process PID, used with atexit() */
#ifndef BACKGROUND
static int __svgalib_currentpage;
#endif
static int vga_page_offset;	/* offset to add to all vga_set*page() calls */
static int currentlogicalwidth;
static int currentdisplaystart;
static int mouse_support = 0;
int mouse_open = 0;
static int mouse_mode = 0;
static int mouse_type = -1;
static int mouse_modem_ctl = 0;
static char *mouse_device = "/dev/mouse";
#ifndef BACKGROUND
static int __svgalib_oktowrite = 1;
#endif
static int modeinfo_mask = ~0;

int __svgalib_mem_fd = -1;	/* /dev/mem file descriptor  */
int __svgalib_tty_fd = -1;	/* /dev/tty file descriptor */
int __svgalib_nosigint = 0;	/* Don't generate SIGINT in graphics mode */
int __svgalib_runinbackground = 0;
static int svgalib_vc = -1, startup_vc = -1;
static int __svgalib_security_revokeallprivs = 1;
static unsigned fontbufsize = 8192; /* compatibility */

/* Dummy buffer for mmapping grahics memory; points to 64K VGA framebuffer. */
unsigned char *__svgalib_graph_mem;
/* Exported variable (read-only) is shadowed from internal variable, for */
/* better shared library performance. */
unsigned char *graph_mem;

/* static unsigned char saved_text[32768]; */

/* Some new organisation for backgrund running. */
/* Alpha do not have ability to run in background (yet). */
/* Bg runnin has two different methods 1 and (2). */
    
   
#ifdef BACKGROUND

static unsigned char *__svgalib_graph_mem_orginal;
static unsigned char *__svgalib_graph_mem_check;
unsigned char *__svgalib_graph_mem_linear_orginal;
unsigned char *__svgalib_graph_mem_linear_check;
static int __svgalib_linear_is_background=0;
int __svgalib_oktowrite=1;

/* __svgalib_oktowrite tells if it is safe to write registers. */

int __svgalib_currentpage;
int __svgalib_virtual_mem_fd=-1;
static int __svgalib_processnumber=-1;
static unsigned char *graph_buf2 = NULL;	/* saves linear graphics data during flip */

#endif

void *__svgalib_physaddr;
int __svgalib_linear_memory_size;


#ifdef __alpha__

/* same as graph mem, but mapped through sparse memory: */
unsigned char *__svgalib_sparse_mem;

#endif

static unsigned char *graph_buf = NULL;		/* saves graphics data during flip */

static unsigned char *font_buf1;	/* saved font data - plane 2 */
static unsigned char *font_buf2;	/* saved font data - plane 3 */
static unsigned char *text_buf1;	/* saved text data - plane 0 */
static unsigned char *text_buf2;	/* saved text data - plane 1 */

static struct termios text_termio;	/* text mode termio parameters     */
static struct termios graph_termio;	/* graphics mode termio parameters */

int __svgalib_flipchar = '\x1b';		/* flip character - initially  ESCAPE */

/* Chipset specific functions */

DriverSpecs *__svgalib_driverspecs = &__svgalib_vga_driverspecs;

#ifndef BACKGROUND
static void (*__svgalib_setpage) (int);	/* gives little faster vga_setpage() */
static void (*__svgalib_setrdpage) (int);
static void (*__svgalib_setwrpage) (int);
#endif
#ifdef BACKGROUND
void (*__svgalib_setpage) (int);	
void (*__svgalib_setrdpage) (int);
void (*__svgalib_setwrpage) (int);
#endif

static void readconfigfile(void);

DriverSpecs *__svgalib_driverspecslist[] =
{
    NULL,			/* chipset undefined */
    &__svgalib_vga_driverspecs,
#ifdef INCLUDE_ET4000_DRIVER
    &__svgalib_et4000_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_CIRRUS_DRIVER
    &__svgalib_cirrus_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_TVGA_DRIVER
    &__svgalib_tvga8900_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_OAK_DRIVER
    &__svgalib_oak_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_EGA_DRIVER
    &__svgalib_ega_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_S3_DRIVER
    &__svgalib_s3_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_ET3000_DRIVER
    &__svgalib_et3000_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_MACH32_DRIVER
    &__svgalib_mach32_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_GVGA6400_DRIVER
    &__svgalib_gvga6400_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_ARK_DRIVER
    &__svgalib_ark_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_ATI_DRIVER
    &__svgalib_ati_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_ALI_DRIVER
    &__svgalib_ali_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_MACH64_DRIVER
    &__svgalib_mach64_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_CHIPS_DRIVER
    &__svgalib_chips_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_APM_DRIVER
    &__svgalib_apm_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_NV3_DRIVER
    &__svgalib_nv3_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_ET6000_DRIVER
    &__svgalib_et6000_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_VESA_DRIVER
    &__svgalib_vesa_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_MX_DRIVER
    &__svgalib_mx_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_PARADISE_DRIVER
    &__svgalib_paradise_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_RAGE_DRIVER
    &__svgalib_rage_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_BANSHEE_DRIVER
    &__svgalib_banshee_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_SIS_DRIVER
    &__svgalib_sis_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_I740_DRIVER
    &__svgalib_i740_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_NEO_DRIVER
    &__svgalib_neo_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_LAGUNA_DRIVER
    &__svgalib_laguna_driverspecs,
#else
    NULL,
#endif
#ifdef INCLUDE_FBDEV_DRIVER
     &__svgalib_fbdev_driverspecs,
#else
     NULL,
#endif
#ifdef INCLUDE_G400_DRIVER
     &__svgalib_g400_driverspecs,
#else
     NULL,
#endif
#ifdef INCLUDE_R128_DRIVER
     &__svgalib_r128_driverspecs,
#else
     NULL,
#endif
#ifdef INCLUDE_SAVAGE_DRIVER
     &__svgalib_savage_driverspecs,
#else
     NULL,
#endif
};

static char *driver_names[] =
{
"", 
"VGA", 
"ET4000", 
"Cirrus", 
"TVGA", 
"Oak", 
"EGA", 
"S3",
"ET3000", 
"Mach32", 
"GVGA6400",
"ARK",
"ATI",
"ALI",
"Mach64", 
"C&T",
"APM",
"NV3",
"ET6000",
"VESA",
"MX",
"PARADISE",
"RAGE",
"BANSHEE", 
"SIS",
"I740",
"NEOMAGIC",
"LAGUNA",
"FBDev",
"G400",
"R128",
"Savage",
 NULL};

/* Chipset drivers */

/* vgadrv       Standard VGA (also used by drivers below) */
/* et4000       Tseng ET4000 (from original vgalib) */
/* cirrus       Cirrus Logic GD542x */
/* tvga8900     Trident TVGA 8900/9000 (derived from tvgalib) */
/* oak          Oak Technologies 037/067/077 */
/* egadrv       IBM EGA (subset of VGA) */
/* s3           S3 911 */
/* mach32       ATI MACH32 */
/* ark          ARK Logic */
/* gvga6400     Genoa 6400 (old SVGA) */
/* ati          ATI */
/* ali          ALI2301 */
/* mach64	ATI MACH64 */
/* chips	chips & technologies*/
/* et6000       Tseng ET6000 */         /* DS */

/*#define DEBUG */

/* Debug config file parsing.. */
/*#define DEBUG_CONF */

#ifdef DEBUG
static void _DEBUG(int dnr)
{
    static int first = 1;
    FILE *dfile;

    dfile = fopen("svgalib.debug", (first ? "w" : "a"));
    first = 0;
    if (dfile == NULL)
	exit(1);
    fprintf(dfile, "debug #%d\n", dnr);
    fclose(dfile);
    sync();
}
#else
#define _DEBUG(d)
#endif

static void set_graphtermio(void)
{
    /* Leave keyboard alone when rawkeyboard is enabled! */
    if (__svgalib_kbd_fd < 0) {
	/* set graphics mode termio parameters */
	ioctl(0, TCSETSW, &graph_termio);
    }
}


static void set_texttermio(void)
{
    /* Leave keyboard alone when rawkeyboard is enabled! */
    if (__svgalib_kbd_fd < 0) {
	/* restore text mode termio parameters */
	ioctl(0, TCSETSW, &text_termio);
    }
}


static void disable_interrupt(void)
{
    struct termios cur_termio;

    /* Well, one could argue that sigint is not enabled at all when in __svgalib_nosigint
       but sometimes they *still* are enabled b4 graph_termio is set.. */
    ioctl(0, TCGETS, &cur_termio);
    cur_termio.c_lflag &= ~ISIG;
    ioctl(0, TCSETSW, &cur_termio);
}


static void enable_interrupt(void)
{
    struct termios cur_termio;

    if (__svgalib_nosigint) /* do not reenable, they are often reenabled by text_termio */
	return; 
    ioctl(0, TCGETS, &cur_termio);
    cur_termio.c_lflag |= ISIG;
    ioctl(0, TCSETSW, &cur_termio);
}

/* The following is rather messy and inelegant. The only solution I can */
/* see is getting a extra free VT for graphics like XFree86 does. */

void __svgalib_waitvtactive(void)
{
    if (__svgalib_tty_fd < 0)
	return; /* Not yet initialized */

    while (ioctl(__svgalib_tty_fd, VT_WAITACTIVE, svgalib_vc) < 0) {
	if ((errno != EAGAIN) && (errno != EINTR)) {
	    perror("ioctl(VT_WAITACTIVE)");
	    exit(1);
	}
	usleep(150000);
    }
}


/* open /dev/mem */
static void open_mem(void)
{
#ifdef BACKGROUND
#if BACKGROUND == 1
 char tmp[40];
#endif
#endif

    if (CHIPSET == FBDEV)
	return;

    /*  Ensure that the open will get a file descriptor greater
     *  than 2, else problems can occur with stdio functions
     *  under certain strange conditions:  */
    if (fcntl(0,F_GETFD) < 0) open("/dev/null", O_RDONLY);
    if (fcntl(1,F_GETFD) < 0) open("/dev/null", O_WRONLY);
    if (fcntl(2,F_GETFD) < 0) open("/dev/null", O_WRONLY);

    if (__svgalib_mem_fd < 0)
	if ((__svgalib_mem_fd = open("/dev/mem", O_RDWR)) < 0) {
	    printf("svgalib: Cannot open /dev/mem.\n");
	    exit(1);
	}
#ifdef BACKGROUND
#if BACKGROUND == 1

    /* Needs proc-fs. */
    /* printf("procc\n"); */
    /* char tmp[40];*/
    if (__svgalib_virtual_mem_fd<0)
        {
	 __svgalib_processnumber=getpid(); 
	 sprintf(tmp,"/proc/%d/mem",__svgalib_processnumber);
	 if ((__svgalib_virtual_mem_fd = open(tmp,O_RDWR)) < 0) 
	     {
	      printf("svgalib: Cannot open /proc/%d/mem.\n",
	          __svgalib_processnumber);
	      exit(-1);
	     }
	}
#endif
#endif
}

static int check_owner(int vc)
{
    struct stat sbuf;
    char fname[30];

#ifdef ROOT_VC_SHORTCUT
    if (!getuid())
        return 1;               /* root can do it always */
#endif
    sprintf(fname, "/dev/tty%d", vc);
    if ((stat(fname, &sbuf) >= 0) && (getuid() == sbuf.st_uid)) {
        return 1;
    }
    printf("You must be the owner of the current console to use svgalib.\n");
    return 0;
}

void __svgalib_open_devconsole(void)
{
    struct vt_mode vtm;
    struct vt_stat vts;
    struct stat sbuf;
    char fname[30];

    if(__svgalib_novccontrol)return;

    if (__svgalib_tty_fd >= 0)
        return;

    /*  The code below assumes file descriptors 0, 1, and 2
     *  are already open; make sure that's true.  */
    if (fcntl(0,F_GETFD) < 0) open("/dev/null", O_RDONLY);
    if (fcntl(1,F_GETFD) < 0) open("/dev/null", O_WRONLY);
    if (fcntl(2,F_GETFD) < 0) open("/dev/null", O_WRONLY);

    /*
     * Now, it would be great if we could use /dev/tty and see what it is connected to.
     * Alas, we cannot find out reliably what VC /dev/tty is bound to. Thus we parse
     * stdin through stderr for a reliable VC
     */
    for (__svgalib_tty_fd = 0; __svgalib_tty_fd < 3; __svgalib_tty_fd++) {
        if (fstat(__svgalib_tty_fd, &sbuf) < 0)
            continue;
        if (ioctl(__svgalib_tty_fd, VT_GETMODE, &vtm) < 0)
            continue;
        if ((sbuf.st_rdev & 0xff00) != 0x400)
            continue;
        if (!(sbuf.st_rdev & 0xff))
            continue;
        svgalib_vc = sbuf.st_rdev & 0xff;
        return;                 /* perfect */
    }

    if ((__svgalib_tty_fd = open("/dev/console", O_RDWR)) < 0) {
        printf("svgalib: can't open /dev/console \n");
        exit(1);
    }
    if (ioctl(__svgalib_tty_fd, VT_OPENQRY, &svgalib_vc) < 0)
        goto error;
    if (svgalib_vc <= 0)
        goto error;
    sprintf(fname, "/dev/tty%d", svgalib_vc);
    close(__svgalib_tty_fd);
    /* change our control terminal: */
    setpgid(0,getppid());
    setsid();
    /* We must use RDWR to allow for output... */
    if (((__svgalib_tty_fd = open(fname, O_RDWR)) >= 0) &&
        (ioctl(__svgalib_tty_fd, VT_GETSTATE, &vts) >= 0)) {
        if (!check_owner(vts.v_active))
            goto error;
        /* success, redirect all stdios */
        if (DREP)
            printf("[svgalib: allocated virtual console #%d]\n", svgalib_vc);
        fflush(stdin);
        fflush(stdout);
        fflush(stderr);
        close(0);
        close(1);
        close(2);
        dup(__svgalib_tty_fd);
        dup(__svgalib_tty_fd);
        dup(__svgalib_tty_fd);
        /* clear screen and switch to it */
        fwrite("\e[H\e[J", 6, 1, stderr);
        fflush(stderr);
        if (svgalib_vc != vts.v_active) {
            startup_vc = vts.v_active;
	    ioctl(__svgalib_tty_fd, VT_ACTIVATE, svgalib_vc);
            __svgalib_waitvtactive();
	}
    } else {
error:
    if (__svgalib_tty_fd > 2)
	close(__svgalib_tty_fd);
    __svgalib_tty_fd = - 1;
    printf("Not running in a graphics capable console,\n"
	 "and unable to find one.\n");
    }
}

void __svgalib_get_perm(void)
{
    static int done = 0;

    /* Only do this once. */
    if (done)
	return;
    done = 1;

    /* Get I/O permissions for VGA registers. */
    /* If IOPERM is set, assume permissions have already been obtained */
    /* by a calling (exec-ing) process, e.g. ioperm(1). */

    if (CHIPSET != FBDEV && getenv("IOPERM") == NULL)
#ifdef __alpha__
	if (ioperm(0x0000, 0x10000, 1)) {
	    printf("svgalib: Cannot get I/O permissions.\n");
	    exit(1);
	}
#else
	if (ioperm(0x3b4, 0x3df - 0x3b4 + 1, 1)) {
	    printf("svgalib: Cannot get I/O permissions.\n");
	    exit(1);
	}
#endif

    /* Open /dev/mem (also needs supervisor rights; ioperm(1) can be */
    /* used together with a special group that has r/w access on */
    /* /dev/mem to facilitate this). */
    open_mem();

    __svgalib_open_devconsole();

    /* color or monochrome text emulation? */
    if (CHIPSET != EGA && CHIPSET != FBDEV && !__svgalib_novga)
	color_text = port_in(MIS_R) & 0x01;
    else
	color_text = 1;		/* EGA is assumed color */

    /* chose registers for color/monochrome emulation */
    if (color_text) {
	__svgalib_CRT_I = CRT_IC;
	__svgalib_CRT_D = CRT_DC;
	__svgalib_IS1_R = IS1_RC;
    } else {
	__svgalib_CRT_I = CRT_IM;
	__svgalib_CRT_D = CRT_DM;
	__svgalib_IS1_R = IS1_RM;
    }
}


void __svgalib_delay(void)
{
    int i;
    for (i = 0; i < 10; i++);
}

/* The Xfree server uses a slow copy, may help us too ... */
#if defined(CONFIG_ALPHA_JENSEN)
extern unsigned long vga_readl(unsigned long base, unsigned long off);
extern void vga_writel(unsigned long b, unsigned long base, unsigned long off);
static void slowcpy_from_sm(unsigned char *dest, unsigned char *src, unsigned bytes)
{
    long i;
    if (((long) dest & 7) || ((long) src & 7) || bytes & 7) {
	printf("svgalib: unaligned slowcpy()!\n");
	exit(1);
    }
    for (i = 0; i < bytes; i++) {
	*(dest + i) = (*(unsigned long *) (src + (i << 7)) >> ((i & 0x03) * 8))
	    & 0xffUL;
    }
}
static void slowcpy_to_sm(unsigned char *dest, unsigned char *src, unsigned bytes)
{
    long i;
    if (((long) dest & 7) || ((long) src & 7) || bytes & 7) {
	printf("svgalib: unaligned slowcpy()!\n");
	exit(1);
    }
    for (i = 0; i < bytes; i++) {
	*(unsigned long *) (dest + (i << 7)) =
	    (*(unsigned char *) (src + i)) * 0x01010101UL;
    }
}

#else
static void slowcpy(unsigned char *dest, unsigned char *src, unsigned bytes)
{
#ifdef __alpha__
    if (((long) dest & 7) || ((long) src & 7) || bytes & 7) {
	printf("svgalib: unaligned slowcpy()!\n");
	exit(1);
    }
    while (bytes > 0) {
	*(long *) dest = *(long *) src;
	dest += 8;
	src += 8;
	bytes -= 8;
    }
#else
    while (bytes-- > 0)
	*(dest++) = *(src++);
#endif
}
#endif

#define TEXT_SIZE 65536

static void restore_text(void)
{

          __svgalib_outseq(0x02,0x01);
          
#ifdef __alpha__
	  port_out(0x06, GRA_I);
	  port_out(0x00, GRA_D);
#endif

#ifdef BACKGROUND
          if (-1 == mprotect(text_buf1,TEXT_SIZE*2,PROT_READ|PROT_WRITE))
          {
	   printf("svgalib: Memory protect error\n");
	   exit(-1);
	  }
#endif

#if defined(CONFIG_ALPHA_JENSEN)
	  slowcpy_to_sm(SM, text_buf1, TEXT_SIZE);
#else
	  slowcpy(GM, text_buf1, TEXT_SIZE);
#endif

          __svgalib_outseq(0x02,0x02);
#if defined(CONFIG_ALPHA_JENSEN)
	  slowcpy_to_sm(SM, text_buf2, TEXT_SIZE);
#else
	  slowcpy(GM, text_buf2, TEXT_SIZE);
#endif
#ifdef BACKGROUND
          if (-1 == mprotect(text_buf1,TEXT_SIZE*2,PROT_READ))
          {
	   printf("svgalib: Memory protect error\n");
	   exit(1);
	  }
#endif
};

static void save_text(void)
{

#ifndef BACKGROUND
   	text_buf1 = malloc(TEXT_SIZE * 2);
#endif
#ifdef BACKGROUND
        text_buf1 = valloc(TEXT_SIZE * 2); 
#endif
        text_buf2 = text_buf1 + TEXT_SIZE;

        port_out(0x04, GRA_I);
        port_out(0x00, GRA_D);
#ifdef __alpha__
        port_out(0x06, GRA_I);
        port_out(0x00, GRA_D);
#endif
#if defined(CONFIG_ALPHA_JENSEN)
        slowcpy_from_sm(text_buf1, SM, TEXT_SIZE);
#else
        slowcpy(text_buf1, GM, TEXT_SIZE);
#endif

        /* save font data in plane 3 */
        port_out(0x04, GRA_I);
        port_out(0x01, GRA_D);
#if defined(CONFIG_ALPHA_JENSEN)
        slowcpy_from_sm(text_buf2, SM, TEXT_SIZE);
#else
        slowcpy(text_buf2, GM, TEXT_SIZE);
#endif

#ifdef BACKGROUND
        /* Let's protect font. */
        /* Read only */
        if (-1 == mprotect(text_buf1,TEXT_SIZE*2,PROT_READ))
            {
	     printf("svgalib: Memory protect error\n");
	     exit(-1);
	    }
#endif

};

int __svgalib_saveregs(unsigned char *regs)
{
    int i;

    if (__svgalib_chipset == EGA || __svgalib_novga) {
	/* Special case: Don't save standard VGA registers. */
	return chipset_saveregs(regs);
    }
    /* save VGA registers */
    for (i = 0; i < CRT_C; i++) {
        regs[CRT + i] = __svgalib_incrtc(i);
    }
    for (i = 0; i < ATT_C; i++) {
	port_in(__svgalib_IS1_R);
	__svgalib_delay();
	port_out(i, ATT_IW);
	__svgalib_delay();
	regs[ATT + i] = port_in(ATT_R);
	__svgalib_delay();
    }
    for (i = 0; i < GRA_C; i++) {
	port_out(i, GRA_I);
	regs[GRA + i] = port_in(GRA_D);
    }
    for (i = 0; i < SEQ_C; i++) {
	port_out(i, SEQ_I);
	regs[SEQ + i] = port_in(SEQ_D);
    }
    regs[MIS] = __svgalib_inmisc();

    i = chipset_saveregs(regs);	/* save chipset-specific registers */
    /* i : additional registers */
    if (!SCREENON) {		/* We turned off the screen */
	port_in(__svgalib_IS1_R);
	__svgalib_delay();
	port_out(0x20, ATT_IW);
    }
    return CRT_C + ATT_C + GRA_C + SEQ_C + 1 + i;
}


int __svgalib_setregs(const unsigned char *regs)
{
    int i;

    if(__svgalib_novga) return 1;

    if (__svgalib_chipset == EGA) {
	/* Enable graphics register modification */
	port_out(0x00, GRA_E0);
	port_out(0x01, GRA_E1);
    }
    /* update misc output register */
    __svgalib_outmisc(regs[MIS]);

    /* synchronous reset on */
    __svgalib_outseq(0x00,0x01);

    /* write sequencer registers */
    __svgalib_outseq(0x01,regs[SEQ + 1] | 0x20);
    port_out(1, SEQ_I);
    port_out(regs[SEQ + 1] | 0x20, SEQ_D);
    for (i = 2; i < SEQ_C; i++) {
       __svgalib_outseq(i,regs[SEQ + i]);
    }

    /* synchronous reset off */
    __svgalib_outseq(0x00,0x03);

    if (__svgalib_chipset != EGA) {
	/* deprotect CRT registers 0-7 */
        __svgalib_outcrtc(0x11,__svgalib_incrtc(0x11)&0x7f);
    }
    /* write CRT registers */
    for (i = 0; i < CRT_C; i++) {
        __svgalib_outcrtc(i,regs[CRT + i]);
    }

    /* write graphics controller registers */
    for (i = 0; i < GRA_C; i++) {
	port_out(i, GRA_I);
	port_out(regs[GRA + i], GRA_D);
    }

    /* write attribute controller registers */
    for (i = 0; i < ATT_C; i++) {
	port_in(__svgalib_IS1_R);		/* reset flip-flop */
	__svgalib_delay();
	port_out(i, ATT_IW);
	__svgalib_delay();
	port_out(regs[ATT + i], ATT_IW);
	__svgalib_delay();
    }

    return 0;
}

/* We invoke the old interrupt handler after setting text mode */
/* We catch all signals that cause an exit by default (aka almost all) */
static char sig2catch[] =
{SIGHUP, SIGINT, SIGQUIT, SIGILL,
 SIGTRAP, SIGIOT, SIGBUS, SIGFPE,
 SIGSEGV, SIGPIPE, SIGALRM, SIGTERM,
 SIGXCPU, SIGXFSZ, SIGVTALRM,
/* SIGPROF ,*/ SIGPWR};
static struct sigaction old_signal_handler[sizeof(sig2catch)];

struct vt_mode __svgalib_oldvtmode;

static void restoretextmode(void)
{
    /* handle unexpected interrupts - restore text mode and exit */
    keyboard_close();
    /* Restore a setting screwed by keyboard_close (if opened in graphicsmode) */
    set_texttermio();
    if (CM != TEXT)
	vga_setmode(TEXT);
    if (!__svgalib_screenon)
	vga_screenon();
    if (__svgalib_tty_fd >= 0) {
        if (!__svgalib_secondary) {
	    ioctl(__svgalib_tty_fd, KDSETMODE, KD_TEXT);
            ioctl(__svgalib_tty_fd, VT_SETMODE, &__svgalib_oldvtmode);
	}
    }
    if((__svgalib_textprog&3)==3){
       pid_t child;
       if((child=fork())==0){
       execv(__svgalib_TextProg,__svgalib_TextProg_argv);
       } else {
       waitpid(child,NULL,0);
       };
    };
}

static void idle_accel(void) {
    /* wait for the accel to finish, we assume one of the both interfaces suffices */
    if (vga_ext_set(VGA_EXT_AVAILABLE, VGA_AVAIL_ACCEL) & ACCELFLAG_SYNC)
        vga_accel(ACCEL_SYNC);
    else if (vga_getmodeinfo(CM)->haveblit & HAVE_BLITWAIT)
	vga_blitwait();
}

static void signal_handler(int v)
{
    int i;

    /* If we have accelerated functions, possibly wait for the
     * blitter to finish. I hope the PutBitmap functions disable
     * interrupts while writing data to the screen, otherwise
     * this will cause an infinite loop.
     */
    idle_accel();
    
    restoretextmode();
    printf("svgalib: Signal %d: %s received%s.\n", v, strsignal(v),
	   (v == SIGINT) ? " (ctrl-c pressed)" : "");

    for (i = 0; i < sizeof(sig2catch); i++)
	if (sig2catch[i] == v) {
	    sigaction(v, old_signal_handler + i, NULL);
	    raise(v);
	    break;
	}
    if (i >= sizeof(sig2catch)) {
	printf("svgalib: Aieeee! Illegal call to signal_handler, raising segfault.\n");
	raise(SIGSEGV);
    }
}

int __svgalib_getchipset(void)
{
    readconfigfile();		/* Make sure the config file is read. */

/* Unlike the others, the FBDev test needs to be before __svgalib_get_perm() */

#ifdef INCLUDE_FBDEV_DRIVER_TEST
    if (CHIPSET == UNDEFINED && __svgalib_fbdev_driverspecs.test())
    {
	CHIPSET = FBDEV;
	__svgalib_setpage = __svgalib_driverspecs->__svgalib_setpage;
	__svgalib_setrdpage = __svgalib_driverspecs->__svgalib_setrdpage;
	__svgalib_setwrpage = __svgalib_driverspecs->__svgalib_setwrpage;
    }
#endif
    __svgalib_get_perm();
    if (CHIPSET == UNDEFINED) {
	CHIPSET = VGA;		/* Protect against recursion */
#ifdef INCLUDE_NV3_DRIVER_TEST
	if (__svgalib_nv3_driverspecs.test())
	    CHIPSET = NV3;
	else
#endif
#ifdef INCLUDE_G400_DRIVER_TEST
	if (__svgalib_g400_driverspecs.test())
	    CHIPSET = G400;
	else
#endif
#ifdef INCLUDE_R128_DRIVER_TEST
	if (__svgalib_r128_driverspecs.test())
	    CHIPSET = R128;
	else
#endif
#ifdef INCLUDE_BANSHEE_DRIVER_TEST
	if (__svgalib_banshee_driverspecs.test())
	    CHIPSET = BANSHEE;
	else
#endif
#ifdef INCLUDE_SIS_DRIVER_TEST
	if (__svgalib_sis_driverspecs.test())
	    CHIPSET = SIS;
	else
#endif
#ifdef INCLUDE_I740_DRIVER_TEST
	if (__svgalib_i740_driverspecs.test())
	    CHIPSET = I740;
	else
#endif
#ifdef INCLUDE_LAGUNA_DRIVER_TEST
	if (__svgalib_laguna_driverspecs.test())
	    CHIPSET = LAGUNA;
	else
#endif
#ifdef INCLUDE_RAGE_DRIVER_TEST
	if (__svgalib_rage_driverspecs.test())
	    CHIPSET = RAGE;
	else
#endif
#ifdef INCLUDE_MX_DRIVER_TEST
	if (__svgalib_mx_driverspecs.test())
	    CHIPSET = MX;
	else
#endif
#ifdef INCLUDE_SAVAGE_DRIVER_TEST
	if (__svgalib_savage_driverspecs.test())
	    CHIPSET = SAVAGE;
	else
#endif
#ifdef INCLUDE_NEO_DRIVER_TEST
	if (__svgalib_neo_driverspecs.test())
	    CHIPSET = NEOMAGIC;
	else
#endif
#ifdef INCLUDE_CHIPS_DRIVER_TEST
	if (__svgalib_chips_driverspecs.test())
	    CHIPSET = CHIPS;
	else
#endif
#ifdef INCLUDE_MACH64_DRIVER_TEST
	if (__svgalib_mach64_driverspecs.test())
	    CHIPSET = MACH64;
	else
#endif
#ifdef INCLUDE_MACH32_DRIVER_TEST
	if (__svgalib_mach32_driverspecs.test())
	    CHIPSET = MACH32;
	else
#endif
#ifdef INCLUDE_EGA_DRIVER_TEST
	if (__svgalib_ega_driverspecs.test())
	    CHIPSET = EGA;
	else
#endif
#ifdef INCLUDE_ET6000_DRIVER_TEST                   /* DS */
	if (__svgalib_et6000_driverspecs.test())    /* This must be before */
	    CHIPSET = ET6000;                       /* ET4000 or the card  */
	else                                        /* will be called et4k */
#endif
#ifdef INCLUDE_ET4000_DRIVER_TEST
	if (__svgalib_et4000_driverspecs.test())
	    CHIPSET = ET4000;
	else
#endif
#ifdef INCLUDE_TVGA_DRIVER_TEST
	if (__svgalib_tvga8900_driverspecs.test())
	    CHIPSET = TVGA8900;
	else
#endif
#ifdef INCLUDE_CIRRUS_DRIVER_TEST
	    /* The Cirrus detection is not very clean. */
	if (__svgalib_cirrus_driverspecs.test())
	    CHIPSET = CIRRUS;
	else
#endif
#ifdef INCLUDE_OAK_DRIVER_TEST
	if (__svgalib_oak_driverspecs.test())
	    CHIPSET = OAK;
	else
#endif
#ifdef INCLUDE_PARADISE_DRIVER_TEST
	if (__svgalib_paradise_driverspecs.test())
	    CHIPSET = PARADISE;
	else
#endif
#ifdef INCLUDE_S3_DRIVER_TEST
	if (__svgalib_s3_driverspecs.test())
	    CHIPSET = S3;
	else
#endif
#ifdef INCLUDE_ET3000_DRIVER_TEST
	if (__svgalib_et3000_driverspecs.test())
	    CHIPSET = ET3000;
	else
#endif
#ifdef INCLUDE_ARK_DRIVER_TEST
	if (__svgalib_ark_driverspecs.test())
	    CHIPSET = ARK;
	else
#endif
#ifdef INCLUDE_GVGA6400_DRIVER_TEST
	if (__svgalib_gvga6400_driverspecs.test())
	    CHIPSET = GVGA6400;
	else
#endif
#ifdef INCLUDE_ATI_DRIVER_TEST
	if (__svgalib_ati_driverspecs.test())
	    CHIPSET = ATI;
	else
#endif
#ifdef INCLUDE_ALI_DRIVER_TEST
	if (__svgalib_ali_driverspecs.test())
	    CHIPSET = ALI;
	else
#endif
#ifdef INCLUDE_APM_DRIVER_TEST
/* Note: On certain cards this may toggle the video signal on/off which
   is ugly. Hence we test this last. */
	if (__svgalib_apm_driverspecs.test())
	    CHIPSET = APM;
	else
#endif
#ifdef INCLUDE_VESA_DRIVER_TEST
	if (__svgalib_vesa_driverspecs.test())
	    CHIPSET = VESA;
	else
#endif

	if (__svgalib_vga_driverspecs.test())
	    CHIPSET = VGA;
	else
	    /* else */
	{
	    fprintf(stderr, "svgalib: Cannot find EGA or VGA graphics device.\n");
	    exit(1);
	}
	__svgalib_setpage = __svgalib_driverspecs->__svgalib_setpage;
	__svgalib_setrdpage = __svgalib_driverspecs->__svgalib_setrdpage;
	__svgalib_setwrpage = __svgalib_driverspecs->__svgalib_setwrpage;
    }
    return CHIPSET;
}

void vga_setchipset(int c)
{
    CHIPSET = c;
#ifdef DEBUG
    printf("Setting chipset\n");
#endif
    if (c == UNDEFINED)
	return;
    if (__svgalib_driverspecslist[c] == NULL) {
	printf("svgalib: Invalid chipset. The driver may not be compiled in.\n");
	CHIPSET = UNDEFINED;
	return;
    }
    __svgalib_get_perm();
    __svgalib_driverspecslist[c]->init(0, 0, 0);
    __svgalib_setpage = __svgalib_driverspecs->__svgalib_setpage;
    __svgalib_setrdpage = __svgalib_driverspecs->__svgalib_setrdpage;
    __svgalib_setwrpage = __svgalib_driverspecs->__svgalib_setwrpage;
}

void vga_setchipsetandfeatures(int c, int par1, int par2)
{
    CHIPSET = c;
#ifdef DEBUG
    printf("Forcing chipset and features\n");
#endif
    __svgalib_get_perm();
    __svgalib_driverspecslist[c]->init(1, par1, par2);
#ifdef DEBUG
    printf("Finished forcing chipset and features\n");
#endif
    __svgalib_setpage = __svgalib_driverspecs->__svgalib_setpage;
    __svgalib_setrdpage = __svgalib_driverspecs->__svgalib_setrdpage;
    __svgalib_setwrpage = __svgalib_driverspecs->__svgalib_setwrpage;
}


static void savepalette(unsigned char *red, unsigned char *green,
			unsigned char *blue)
{
    int i;

    if (__svgalib_driverspecs->emul && __svgalib_driverspecs->emul->savepalette) 
        return (__svgalib_driverspecs->emul->savepalette(red, green, blue));

    if (CHIPSET == EGA || __svgalib_novga) 
	return;

    /* save graphics mode palette - first select palette index 0 */
    port_out(0, PEL_IR);

    /* read RGB components - index is autoincremented */
    for (i = 0; i < 256; i++) {
	__svgalib_delay();
	*(red++) = port_in(PEL_D);
	__svgalib_delay();
	*(green++) = port_in(PEL_D);
	__svgalib_delay();
	*(blue++) = port_in(PEL_D);
    }
}

static void restorepalette(const unsigned char *red,
		   const unsigned char *green, const unsigned char *blue)
{
    int i;

    if (__svgalib_driverspecs->emul && __svgalib_driverspecs->emul->restorepalette) 
        return (__svgalib_driverspecs->emul->restorepalette(red, green, blue));

    if (CHIPSET == EGA || __svgalib_novga)
	return;

    /* restore saved palette */
    port_out(0, PEL_IW);

    /* read RGB components - index is autoincremented */
    for (i = 0; i < 256; i++) {
	__svgalib_delay();
	port_out(*(red++), PEL_D);
	__svgalib_delay();
	port_out(*(green++), PEL_D);
	__svgalib_delay();
	port_out(*(blue++), PEL_D);
    }
}

#ifndef BACKGROUND

/* Virtual console switching */

static int forbidvtrelease = 0;
static int forbidvtacquire = 0;
static int lock_count = 0;
static int release_flag = 0;

static void __svgalib_takevtcontrol(void);

void __svgalib_flipaway(void);
static void __svgalib_flipback(void);

static void __svgalib_releasevt_signal(int n)
{
    if (lock_count) {
	release_flag = 1;
	return;
    }
#ifdef DEBUG
    printf("Release request.\n");
#endif
    forbidvtacquire = 1;
    if (forbidvtrelease) {
	forbidvtacquire = 0;
	ioctl(__svgalib_tty_fd, VT_RELDISP, 0);
	return;
    }
    if (__svgalib_go_to_background)
	(__svgalib_go_to_background) ();
    __svgalib_flipaway();
        if((__svgalib_textprog&3)==3){
           pid_t child;
           if((child=fork())==0){
           execv(__svgalib_TextProg,__svgalib_TextProg_argv);
           } else {
           waitpid(child,NULL,0);
           };  
        };
    ioctl(__svgalib_tty_fd, VT_RELDISP, 1);
#ifdef DEBUG
    printf("Finished release.\n");
#endif
    forbidvtacquire = 0;

    /* Suspend program until switched to again. */
#ifdef DEBUG
    printf("Suspended.\n");
#endif

    __svgalib_oktowrite = 0;
    if (!__svgalib_runinbackground)
	__svgalib_waitvtactive();
#ifdef DEBUG
    printf("Waked.\n");
#endif
}

static void __svgalib_acquirevt_signal(int n)
{
#ifdef DEBUG
    printf("Acquisition request.\n");
#endif
    forbidvtrelease = 1;
    if (forbidvtacquire) {
	forbidvtrelease = 0;
	return;
    }
    __svgalib_flipback();
    ioctl(__svgalib_tty_fd, VT_RELDISP, VT_ACKACQ);
#ifdef DEBUG
    printf("Finished acquisition.\n");
#endif
    forbidvtrelease = 0;
    if (__svgalib_come_from_background)
	(__svgalib_come_from_background) ();
    __svgalib_oktowrite = 1;
}

#endif

#ifndef BACKGROUND

void __svgalib_takevtcontrol(void)
{
    struct sigaction siga;
    struct vt_mode newvtmode;

    ioctl(__svgalib_tty_fd, VT_GETMODE, &__svgalib_oldvtmode);
    newvtmode = __svgalib_oldvtmode;
    newvtmode.mode = VT_PROCESS;	/* handle VT changes */
    newvtmode.relsig = SVGALIB_RELEASE_SIG;	/* I didn't find SIGUSR1/2 anywhere */
    newvtmode.acqsig = SVGALIB_ACQUIRE_SIG;	/* in the kernel sources, so I guess */
    /* they are free */
    SETSIG(siga, SVGALIB_RELEASE_SIG, __svgalib_releasevt_signal);
    SETSIG(siga, SVGALIB_ACQUIRE_SIG, __svgalib_acquirevt_signal); 
    ioctl(__svgalib_tty_fd, VT_SETMODE, &newvtmode);
}

#endif

#ifdef LINEAR_DEBUG
void dump_mem(unsigned char *name)
{
  unsigned char bu[128];
  sprintf(bu,"cat /proc/%d/maps > /tmp/%s",getpid(),name);
  system(bu);
}
#endif

#ifdef BACKGROUND
#if BACKGROUND == 1
void __svgalib_map_virtual_screen(int page)
{
 if (page==1000000)
     {
      __svgalib_graph_mem = 
          (unsigned char *) mmap((caddr_t) GM, GRAPH_SIZE,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_FIXED,
				 __svgalib_virtual_mem_fd,
				 (int)__svgalib_graph_mem_orginal);
      if (__svgalib_graph_mem!=__svgalib_graph_mem_check)
          {
	   printf("svgalib: mmap error in paged screen memory.\n");
	   exit(-1);
	  }
      if (__svgalib_modeinfo_linearset&IS_LINEAR)
          {
           __svgalib_linearframebuffer = 
           (unsigned char *) mmap((caddr_t) __svgalib_linearframebuffer, 
	                         __svgalib_linear_memory_size,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_FIXED,
				 __svgalib_virtual_mem_fd,
				 (int)__svgalib_graph_mem_linear_orginal);
           if (__svgalib_linearframebuffer!=__svgalib_graph_mem_linear_check)
              {
	       printf("svgalib: mmap error in linear screen memory.\n");
	       exit(-1);
	      }
	   __svgalib_linear_is_background=0;
	  }
     }
   else
     {
      /*  Program is now in the background. */
      __svgalib_graph_mem = 
          (unsigned char *) mmap((caddr_t) GM, GRAPH_SIZE,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_FIXED,
				 __svgalib_virtual_mem_fd,
				 (int)(graph_buf+(GRAPH_SIZE*page)));
      if (__svgalib_graph_mem!=__svgalib_graph_mem_check)
          {
	   printf("svgalib: mmap error in paged background memory.\n");
	   exit(-1);
	  }
      if (!__svgalib_linear_is_background && 
          __svgalib_modeinfo_linearset&IS_LINEAR)
          {
#ifdef LINEAR_DEBUG
           dump_mem("testmaps-before");
           printf("svgalib: trying to map regular mem at %p, size=0x%08x from %p\n",
                  __svgalib_linearframebuffer,__svgalib_linear_memory_size,graph_buf2);
#endif
           munmap(__svgalib_linearframebuffer,__svgalib_linear_memory_size);
#ifdef LINEAR_DEBUG
           dump_mem("testmaps-after");
#endif
    __svgalib_linearframebuffer = 
           (unsigned char *) mmap((caddr_t) __svgalib_linearframebuffer, 
	                         __svgalib_linear_memory_size,
				 PROT_READ | PROT_WRITE,
				 MAP_SHARED | MAP_FIXED,
				 __svgalib_virtual_mem_fd,
				 (int)(graph_buf2));
           if (__svgalib_linearframebuffer!=__svgalib_graph_mem_linear_check)
              {
	       printf("svgalib: mmap error in linear background memory.\n");
	       exit(-1);
	      }
	   __svgalib_linear_is_background=1;
          }
     }
 return;
}

#endif
#if BACKGROUND == 2

#endif
#endif


static void __vga_mmap(void)
{
#ifdef LINEAR_DEBUG
    printf("__vga_mmap() called, __svgalib_graph_base=0x%08lx\n",__svgalib_graph_base);
#endif
    /* This assumes pl10+. */
    /* Still seems to waste 64K, so don't bother. */
#ifndef BACKGROUND
    GM = (unsigned char *) BANKED_MEM_POINTER;
#endif
#ifdef BACKGROUND
#if BACKGROUND == 1
    if ((__svgalib_graph_mem = valloc(GRAPH_SIZE)) == NULL) {
	printf("svgalib: allocation error \n");
	exit(-1);
    }
    __svgalib_graph_mem_check=__svgalib_graph_mem;
    __svgalib_graph_mem_orginal = (unsigned char *) BANKED_MEM_POINTER;
    __svgalib_map_virtual_screen(1000000); /* Video page */
#endif	
#if BACKGROUND == 2
    GM = (unsigned char *) BANKED_MEM_POINTER;
    __svgalib_graph_mem_orginal=GM;
#endif
#endif

#ifdef __alpha__
    SM = (unsigned char *) mmap(
				   (caddr_t) 0,
				   GRAPH_SIZE << MEM_SHIFT,
				   PROT_READ | PROT_WRITE,
				   MAP_SHARED,
				   __svgalib_mem_fd,
				   SPARSE_GRAPH_BASE
	);
#endif
    graph_mem = __svgalib_graph_mem;	/* Exported variable. */
}

static void __vga_atexit(void)
{
    if (getpid() == my_pid)	/* protect against forked processes */
	restoretextmode();
    if (__svgalib_tty_fd >= 0 && startup_vc > 0)
	    ioctl(__svgalib_tty_fd, VT_ACTIVATE, startup_vc);
}

static void setcoloremulation(void)
{
    /* shift to color emulation */
    __svgalib_CRT_I = CRT_IC;
    __svgalib_CRT_D = CRT_DC;
    __svgalib_IS1_R = IS1_RC;
    if (CHIPSET != EGA && !__svgalib_novga)  
	port_out(port_in(MIS_R) | 0x01, MIS_W);
}

static void initialize(void)
{
    int i;
    struct sigaction siga;

    __svgalib_open_devconsole();
    if (__svgalib_tty_fd < 0) {
	exit(1);
    }

    /* Make sure that textmode is restored at exit(). */
    if (my_pid == 0)
	my_pid = getpid();
    atexit(__vga_atexit);

#ifndef DONT_WAIT_VC_ACTIVE
    __svgalib_waitvtactive();
#endif

    /* save text mode termio parameters */
    ioctl(0, TCGETS, &text_termio);

    graph_termio = text_termio;

    /* change termio parameters to allow our own I/O processing */
    graph_termio.c_iflag &= ~(BRKINT | PARMRK | INPCK | IUCLC | IXON | IXOFF);
    graph_termio.c_iflag |= (IGNBRK | IGNPAR);

    graph_termio.c_oflag &= ~(ONOCR);

    graph_termio.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | NOFLSH);
    if (__svgalib_nosigint)
	graph_termio.c_lflag &= ~ISIG;	/* disable interrupt */
    else
	graph_termio.c_lflag |=  ISIG;	/* enable interrupt */

    graph_termio.c_cc[VMIN] = 1;
    graph_termio.c_cc[VTIME] = 0;
    graph_termio.c_cc[VSUSP] = 0;	/* disable suspend */

    disable_interrupt();	/* Is reenabled later by set_texttermio */

    __svgalib_getchipset();		/* make sure a chipset has been selected */
    chipset_unlock();

    /* disable text output to console */
    if (!__svgalib_secondary)
	ioctl(__svgalib_tty_fd, KDSETMODE, KD_GRAPHICS);

    __svgalib_takevtcontrol();	/* HH: Take control over VT */

    /* open /dev/mem */
    open_mem();

    /* mmap graphics memory */
    if(B8000_MEM_POINTER==NULL){
        if(__svgalib_banked_mem_base==0)__svgalib_banked_mem_base=0xa0000;
        if(__svgalib_banked_mem_size==0)__svgalib_banked_mem_size=0x10000;
                      BANKED_MEM_POINTER=mmap((caddr_t) 0,
			                      __svgalib_banked_mem_size,
				              PROT_READ | PROT_WRITE,
				              MAP_SHARED,
				              __svgalib_mem_fd,
				              __svgalib_banked_mem_base
                           
                      );
        if(__svgalib_linear_mem_size) {
                      LINEAR_MEM_POINTER=mmap((caddr_t) 0,
				              __svgalib_linear_mem_size,
			                      PROT_READ | PROT_WRITE,
                                              MAP_SHARED,
				              __svgalib_mem_fd,
                                              (off_t) __svgalib_linear_mem_base
                      );
        };/* else LINEAR_MEM_POINTER=NULL;*/
    
        if(__svgalib_mmio_size)
                      MMIO_POINTER=mmap((caddr_t) 0,
				       __svgalib_mmio_size,
				       PROT_READ | PROT_WRITE,
				       MAP_SHARED,
				       __svgalib_mem_fd,
                                       (off_t) __svgalib_mmio_base
                      ); else MMIO_POINTER=NULL;
    
        B8000_MEM_POINTER=mmap((caddr_t) 0,
       			        32768,
			        PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                __svgalib_mem_fd,
                                (off_t) 0xb8000);
    };
    __vga_mmap();

    if ((long) GM < 0) {
	printf("svgalib: mmap error rrr\n");
	exit(1);
    }
    /* disable video */
    vga_screenoff();

    /* Sanity check: (from painful experience) */

    i = __svgalib_saveregs(text_regs);
    if (i > MAX_REGS) {
	puts("svgalib: FATAL internal error:");
	printf("Set MAX_REGS at least to %d in src/driver.h and recompile everything.\n",
	       i);
	exit(1);
    }
    /* This appears to fix the Trident 8900 rebooting problem. */
    if (__svgalib_chipset == TVGA8900) {
	port_out(0x0c, SEQ_I);	/* reg 12 */
	text_regs[EXT + 11] = port_in(SEQ_D);
	port_out(0x1f, __svgalib_CRT_I);
	text_regs[EXT + 12] = port_in(__svgalib_CRT_D);
    }
    /* save text mode palette - first select palette index 0 */
    if(!__svgalib_novga)
	port_out(0, PEL_IR);

    /* read RGB components - index is autoincremented */
    savepalette(text_red, text_green, text_blue);

    /* shift to color emulation */
    setcoloremulation();

    /* save font data - first select a 16 color graphics mode */
    if (__svgalib_driverspecs->emul && __svgalib_driverspecs->emul->savefont) {
	 __svgalib_driverspecs->emul->savefont();
    } else if(!__svgalib_novga) {
        __svgalib_driverspecs->setmode(GPLANE16, prv_mode);
        save_text();

        /* Allocate space for textmode font. */
#ifndef BACKGROUND
        font_buf1 = malloc(FONT_SIZE * 2);
#endif
#ifdef BACKGROUND
        font_buf1 = valloc(FONT_SIZE * 2); 
#endif
        font_buf2 = font_buf1 + FONT_SIZE;

    /* save font data in plane 2 */
        port_out(0x04, GRA_I);
        port_out(0x02, GRA_D);
#ifdef __alpha__
        port_out(0x06, GRA_I);
        port_out(0x00, GRA_D);
#endif
#if defined(CONFIG_ALPHA_JENSEN)
        slowcpy_from_sm(font_buf1, SM, FONT_SIZE);
#else
        slowcpy(font_buf1, GM, FONT_SIZE);
#endif

        /* save font data in plane 3 */
        port_out(0x04, GRA_I);
        port_out(0x03, GRA_D);
#if defined(CONFIG_ALPHA_JENSEN)
        slowcpy_from_sm(font_buf2, SM, FONT_SIZE);
#else
        slowcpy(font_buf2, GM, FONT_SIZE);
#endif

#ifdef BACKGROUND
        /* Let's protect font. */
        /* Read only */
        if (-1 == mprotect(font_buf1,FONT_SIZE*2,PROT_READ))
            {
	     printf("svgalib: Memory protect error\n");
	     exit(-1);
	    }
#endif
    }
    initialized = 1;

    /* do our own interrupt handling */
    for (i = 0; i < sizeof(sig2catch); i++) {
	siga.sa_handler = signal_handler;
	siga.sa_flags = 0;
	zero_sa_mask(&(siga.sa_mask));
	sigaction((int) sig2catch[i], &siga, old_signal_handler + i);
    }

    /* vga_unlockvc(); */
}

#ifndef BACKGROUND

inline void vga_setpage(int p)
{
    p += vga_page_offset;
    if (p == __svgalib_currentpage && !__svgalib_simple)
	return;
    (*__svgalib_setpage) (p);
    __svgalib_currentpage = p;
}


void vga_setreadpage(int p)
{
    p += vga_page_offset;
    if (p == __svgalib_currentpage)
	return;
    (*__svgalib_setrdpage) (p);
    __svgalib_currentpage = -1;
}


void vga_setwritepage(int p)
{
    p += vga_page_offset;
    if (p == __svgalib_currentpage)
	return;
    (*__svgalib_setwrpage) (p);
    __svgalib_currentpage = -1;
}

#endif

void vga_safety_fork(void (*shutdown_routine) (void))
{
    pid_t childpid;
    int child_status, oldkbmode;

    if (initialized) {
	printf("svgalib: warning: vga_safety_fork() called when already initialized\n");
	goto no_fork;
    }
    initialize();

    /*
     * get current keyboard mode:
     *  If this didn't suffice we claim we are on an old system and just don't
     *  need to restore it.
     */
    ioctl(__svgalib_tty_fd, KDGKBMODE, &oldkbmode);

    childpid = fork();
    if (childpid < 0) {
      no_fork:
	printf("svgalib: warning: can't fork to enhance reliability; proceeding anyway");
	return;
    }
    if (childpid) {
	ioctl(__svgalib_tty_fd, (int) TIOCNOTTY, (char *)0);
	for (;;) {
	    while (waitpid(childpid, &child_status, WUNTRACED) != childpid);

	    if (shutdown_routine)
		shutdown_routine();

	    vga_setmode(TEXT);	/* resets termios as well */
	    ioctl(__svgalib_tty_fd, KDSKBMODE, oldkbmode);

	    if (WIFEXITED(child_status))
		exit(WEXITSTATUS(child_status));

	    if (WCOREDUMP(child_status))
		puts("svgalib:vga_safety_fork: Core dumped!");

	    if (WIFSIGNALED(child_status)) {
		printf("svgalib:vga_safety_fork: Killed by signal %d, %s.\n",
		       WTERMSIG(child_status),
		       strsignal(WTERMSIG(child_status)));
		exit(1);
	    }
	    if (WIFSTOPPED(child_status)) {
		printf("svgalib:vga_safety_fork: Stopped by signal %d, %s.\n",
		       WSTOPSIG(child_status),
		       strsignal(WSTOPSIG(child_status)));
		puts("\aWARNING! Continue stopped svgalib application at own risk. You are better\n"
		     "off killing it NOW!");
		continue;
	    }
	}
    }
    /* These need to be done again because the child doesn't inherit them.  */
    __svgalib_get_perm();

#ifdef BACKGROUND
    if (__svgalib_virtual_mem_fd>0)close(__svgalib_virtual_mem_fd);
    {
    char tmp[50];
    __svgalib_processnumber=getpid(); 
    sprintf(tmp,"/proc/%d/mem",__svgalib_processnumber);
    if ((__svgalib_virtual_mem_fd = open(tmp,O_RDWR)) < 0) 
       {
          printf("svgalib: Cannot open /proc/%d/mem.\n",
	         __svgalib_processnumber);
          exit(-1);
       }
    };
#endif
    /*
     * But alas. That doesn't suffice. We raise the iopl here what merely makes
     * the previous call pointless.
     *
     * If IOPERM is set, assume permissions have already been set by Olaf Titz'
     * ioperm(1).
     */

    if (CHIPSET != FBDEV && getenv("IOPERM") == NULL) {
	if (iopl(3) < 0) {
	    printf("svgalib(vga_safety_fork): Cannot get I/O permissions.\n");
	    exit(1);
	}
    }
    /*
     * Actually the mmap's are inherited anyway (and not all are remade here),
     * but it does not really harm.
     */
#ifdef BACKGROUND
    /* 
     *  __vga_mmap() call is now really dangerous. You can only call 
     *  mmap once. Once virtual screen has been allocated its better
     *  stay in one place. At least for now. Anyway this works now.
     */
#else
    __vga_mmap();
#endif


    /*
     * We might still want to do vc switches.
     */

    __svgalib_takevtcontrol();
}

static void prepareforfontloading(void)
{
    if (__svgalib_chipset == CIRRUS) {
	outb(SEQ_I, 0x0f);
	/* Disable CRT FIFO Fast-Page mode. */
	outb(SEQ_D, inb(SEQ_D) | 0x40);
    }
}

static void fontloadingcomplete(void)
{
    if (__svgalib_chipset == CIRRUS) {
	outb(SEQ_I, 0x0f);
	/* Re-enable CRT FIFO Fast-Page mode. */
	outb(SEQ_D, inb(SEQ_D) & 0xbf);
    }
}


int vga_setmode(int mode)
{
    int modeflags=mode&0xfffff000;
    
    if(mode==-1)return vga_version;
    
    mode&=0xfff;
#ifdef BACKGROUND
    __svgalib_dont_switch_vt_yet();
    /* Can't initialize if screen is flipped. */
    if (__svgalib_oktowrite)
#endif
    if (!initialized)
	initialize();

    if (mode != TEXT && !chipset_modeavailable(mode))
       {
#ifdef BACKGROUND
         __svgalib_is_vt_switching_needed();
#endif
	return -1;
       }
       
#ifdef BACKGROUND
    if (!__svgalib_oktowrite)
        {
	 prv_mode=CM;
	 CM=mode;
	 vga_setpage(0);
	 __svgalib_is_vt_switching_needed();
	 return(0);
	 /* propably this was enough. */
	 /* hmm.. there... virtual screen... */
	}
#endif

/*    if (!flip)
   vga_lockvc(); */
    disable_interrupt();

    prv_mode = CM;
    CM = mode;

    /* disable video */
    vga_screenoff();

    if(!__svgalib_novga) {
    /* Should be more robust (eg. grabbed X modes) */
	if (__svgalib_getchipset() == ET4000
	     && prv_mode != G640x480x256
	     && SVGAMODE(prv_mode))
	    chipset_setmode(G640x480x256, prv_mode);

	/* This is a hack to get around the fact that some C&T chips
	 * are programmed to ignore syncronous resets. So if we are
	 * a C&T wait for retrace start
	 */
	if (__svgalib_getchipset() == CHIPS) {
	   while (((port_in(__svgalib_IS1_R)) & 0x08) == 0x08 );/* wait VSync off */
	   while (((port_in(__svgalib_IS1_R)) & 0x08) == 0 );   /* wait VSync on  */
	   port_outw(0x07,SEQ_I);         /* reset hsync - just in case...  */
	}
    }

    if (mode == TEXT) {
	/* Returning to textmode. */

	if (SVGAMODE(prv_mode))
	    vga_setpage(0);

	/* The extended registers are restored either by the */
	/* chipset setregs function, or the chipset setmode function. */

	/* restore font data - first select a 16 color graphics mode */
	/* Note: this should restore the old extended registers if */
	/* setregs is not defined for the chipset. */
        if(__svgalib_novga) __svgalib_driverspecs->setmode(TEXT, prv_mode);
        if (__svgalib_driverspecs->emul && __svgalib_driverspecs->emul->restorefont) {
           __svgalib_driverspecs->emul->restorefont(); 
	   chipset_setregs(text_regs, mode);
        } else if(!__svgalib_novga) {
	  __svgalib_driverspecs->setmode(GPLANE16, prv_mode);

	  if (CHIPSET != EGA)
	      /* restore old extended regs */
	      chipset_setregs(text_regs, mode);

	  /* disable Set/Reset Register */
	  port_out(0x01, GRA_I);
	  port_out(0x00, GRA_D);

	  prepareforfontloading();
          restore_text();

	  /* restore font data in plane 2 - necessary for all VGA's */
	  port_out(0x02, SEQ_I);
	  port_out(0x04, SEQ_D);
#ifdef __alpha__
	  port_out(0x06, GRA_I);
	  port_out(0x00, GRA_D);
#endif

#ifdef BACKGROUND
          if (-1 == mprotect(font_buf1,FONT_SIZE*2,PROT_READ|PROT_WRITE))
          {
	   printf("svgalib: Memory protect error\n");
	   exit(-1);
	  }
#endif

#if defined(CONFIG_ALPHA_JENSEN)
	  slowcpy_to_sm(SM, font_buf1, FONT_SIZE);
#else
	  slowcpy(GM, font_buf1, FONT_SIZE);
#endif

	  /* restore font data in plane 3 - necessary for Trident VGA's */
	  port_out(0x02, SEQ_I);
	  port_out(0x08, SEQ_D);
#if defined(CONFIG_ALPHA_JENSEN)
	  slowcpy_to_sm(SM, font_buf2, FONT_SIZE);
#else
	  slowcpy(GM, font_buf2, FONT_SIZE);
#endif
#ifdef BACKGROUND
          if (-1 == mprotect(font_buf1,FONT_SIZE*2,PROT_READ))
          {
	   printf("svgalib: Memory protect error\n");
	   exit(1);
	  }
#endif
	  fontloadingcomplete();

	  /* change register adresses if monochrome text mode */
	  /* EGA is assumed to use color emulation. */
	  if (!color_text) {
	      __svgalib_CRT_I = CRT_IM;
	      __svgalib_CRT_D = CRT_DM;
	      __svgalib_IS1_R = IS1_RM;
	      port_out(port_in(MIS_R) & 0xFE, MIS_W);
	  }
        } else chipset_setregs(text_regs, mode);
	/* restore saved palette */
	restorepalette(text_red, text_green, text_blue);

	/* restore text mode VGA registers */
	__svgalib_setregs(text_regs);

	/* Set VMEM to some minimum value .. probably pointless.. */
	{
	    vga_claimvideomemory(12);
	}

/*      if (!flip) */
	/* enable text output - restores the screen contents */
        if (!__svgalib_secondary)
	    ioctl(__svgalib_tty_fd, KDSETMODE, KD_TEXT);

        /* now wait for signal to stabilize, but don't do it on C&T chips. */
        /* This is needed to restore correct text mode stretching.         */
	if (__svgalib_chipset != CHIPS)
	    usleep(MODESWITCHDELAY);

	/* enable video */
	vga_screenon();

	if (!flip)
	    /* restore text mode termio */
	    set_texttermio();
    } else {
	/* Setting a graphics mode. */

	/* disable text output */
        if (!__svgalib_secondary)
	    ioctl(__svgalib_tty_fd, KDSETMODE, KD_GRAPHICS);

	if (SVGAMODE(prv_mode)) {
	    /* The current mode is an SVGA mode, and we now want to */
	    /* set a standard VGA mode. Make sure the extended regs */
	    /* are restored. */
	    /* Also used when setting another SVGA mode to hopefully */
	    /* eliminate lock-ups. */
	    vga_setpage(0);
	    chipset_setregs(text_regs, mode);
	    /* restore old extended regs */
	}
	/* shift to color emulation */
	setcoloremulation();

	CI.xdim = infotable[mode].xdim;
	CI.ydim = infotable[mode].ydim;
	CI.colors = infotable[mode].colors;
	CI.xbytes = infotable[mode].xbytes;
	CI.bytesperpixel = infotable[mode].bytesperpixel;

	chipset_setmode(mode, prv_mode);
	MODEX = 0;

	/* Set default claimed memory (moved here from initialize - Michael.) */
	if (mode == G320x200x256)
	    VMEM = 65536;
	else if (STDVGAMODE(mode))
	    VMEM = 256 * 1024;	/* Why always 256K ??? - Michael */
	else {
	    vga_modeinfo *modeinfo;

	    modeinfo = vga_getmodeinfo(mode);
	    VMEM = modeinfo->linewidth * modeinfo->height;
            CI.xbytes = modeinfo->linewidth;
	}

	if (!flip) {
	    /* set default palette */
	    if (CI.colors <= 256)
		restorepalette(default_red, default_green, default_blue);

	    /* clear screen (sets current color to 15) */
	    __svgalib_currentpage = -1;
            if(!(modeflags&0x8000))vga_clear();

	    if (SVGAMODE(__svgalib_cur_mode))
		vga_setpage(0);
	}
	__svgalib_currentpage = -1;
	currentlogicalwidth = CI.xbytes;
	currentdisplaystart = 0;

	usleep(MODESWITCHDELAY);	/* wait for signal to stabilize */

	/* enable video */
	if (!flip)
	    vga_screenon();

	if (mouse_support && mouse_open) {
		/* vga_lockvc(); */
		mouse_setxrange(0, CI.xdim - 1);
		mouse_setyrange(0, CI.ydim - 1);
		mouse_setwrap(MOUSE_NOWRAP);
		mouse_mode = mode;
	} {
	    vga_modeinfo *modeinfo;
	    modeinfo = vga_getmodeinfo(mode);
	    MODEX = ((MODEFLAGS = modeinfo->flags) & IS_MODEX);
	}

	if (!flip)
	    /* set graphics mode termio */
	    set_graphtermio();
	else if (__svgalib_kbd_fd < 0)
	    enable_interrupt();
    }

/*    if (!flip)
   vga_unlockvc(); */
#ifdef BACKGROUND
 __svgalib_is_vt_switching_needed();
#endif
    return 0;
}

void vga_gettextfont(void *font)
{
    unsigned int getsize;

    getsize = fontbufsize;
    if (getsize > FONT_SIZE)
	getsize = FONT_SIZE;
    memcpy(font, font_buf1, getsize);
    if (fontbufsize > getsize)
	memset(((char *)font) + getsize, 0, (size_t)(fontbufsize - getsize));
}

void vga_puttextfont(void *font)
{
    unsigned int putsize;

#ifdef BACKGROUND
        if (-1 == mprotect(font_buf1,FONT_SIZE*2,PROT_READ|PROT_WRITE))
        {
	 printf("svgalib: Memory protect error\n");
	 exit(-1);
	}
#endif

    putsize = fontbufsize;
    if (putsize > FONT_SIZE)
	putsize = FONT_SIZE;
    memcpy(font_buf1, font, putsize);
    memcpy(font_buf2, font, putsize);
    if (putsize < FONT_SIZE) {
	memset(font_buf1 + putsize, 0, (size_t)(FONT_SIZE - putsize));
	memset(font_buf2 + putsize, 0, (size_t)(FONT_SIZE - putsize));
    }

#ifdef BACKGROUND
        if (-1 == mprotect(font_buf1,FONT_SIZE*2,PROT_READ))
        {
	 printf("svgalib: Memory protect error\n");
	 exit(-1);
	}
#endif
}

void vga_gettextmoderegs(void *regs)
{
    memcpy(regs, text_regs, MAX_REGS);
}

void vga_settextmoderegs(void *regs)
{
    memcpy(text_regs, regs, MAX_REGS);
}

int vga_getcurrentmode(void)
{
    return CM;
}

int vga_getcurrentchipset(void)
{
    return __svgalib_getchipset();
}

void vga_disabledriverreport(void)
{
    DREP = 0;
}

vga_modeinfo *vga_getmodeinfo(int mode)
{
    static vga_modeinfo modeinfo;
    int is_modeX = (CM == mode) && MODEX;

    modeinfo.linewidth = infotable[mode].xbytes;
    __svgalib_getchipset();
    if (mode > vga_lastmodenumber())
	return NULL;
    modeinfo.width = infotable[mode].xdim;
    modeinfo.height = infotable[mode].ydim;
    modeinfo.bytesperpixel = infotable[mode].bytesperpixel;
    modeinfo.colors = infotable[mode].colors;
    if (is_modeX) {
	modeinfo.linewidth = modeinfo.width / 4;
	modeinfo.bytesperpixel = 0;
    }
    if (mode == TEXT) {
	modeinfo.flags = HAVE_EXT_SET;
	return &modeinfo;
    }
    modeinfo.flags = 0;
    if ((STDVGAMODE(mode) && mode != G320x200x256) || is_modeX)
	__svgalib_vga_driverspecs.getmodeinfo(mode, &modeinfo);
    else
	/* Get chipset specific info for SVGA modes and */
	/* 320x200x256 (chipsets may support more pages) */
	chipset_getmodeinfo(mode, &modeinfo);

    if (modeinfo.colors == 256 && modeinfo.bytesperpixel == 0)
	modeinfo.flags |= IS_MODEX;
    if (mode > __GLASTMODE)
	modeinfo.flags |= IS_DYNAMICMODE;

    /* Maskout CAPABLE_LINEAR if requested by config file */
    modeinfo.flags &= modeinfo_mask;

    /* Many cards have problems with linear 320x200x256 mode */
    if(mode==G320x200x256)modeinfo.flags &= (~CAPABLE_LINEAR) & (~IS_LINEAR) ;

    /* If all needed info is here, signal if linear support has been enabled */
    if ((modeinfo.flags & (CAPABLE_LINEAR | EXT_INFO_AVAILABLE)) ==
	(CAPABLE_LINEAR | EXT_INFO_AVAILABLE)) {
	modeinfo.flags |= __svgalib_modeinfo_linearset;
    }

#ifdef BACKGROUND
#if 0 /* vga_set[rw]page() don't work if compile for background, even when in foreground. */
    if (__svgalib_runinbackground) /* these cannot be provided if we are really in background */
#endif
	modeinfo.flags &= ~HAVE_RWPAGE;
#endif

    return &modeinfo;
}

int vga_hasmode(int mode)
{
    __svgalib_getchipset();		/* Make sure the chipset is known. */
    if (mode == TEXT)
	return 1;
    if (mode < 0 || mode > lastmodenumber)
	return 0;
    return (chipset_modeavailable(mode) != 0);
}


int vga_lastmodenumber(void)
{
    __svgalib_getchipset();
    return lastmodenumber;
}


int __svgalib_addmode(int xdim, int ydim, int cols, int xbytes, int bytespp)
{
    int i;

    for (i = 0; i <= lastmodenumber; ++i)
	if (infotable[i].xdim == xdim &&
	    infotable[i].ydim == ydim &&
	    infotable[i].colors == cols &&
	    infotable[i].bytesperpixel == bytespp &&
	    infotable[i].xbytes == xbytes)
	    return i;
    if (lastmodenumber >= MAX_MODES - 1)
	return -1;		/* no more space available */
    ++lastmodenumber;
    infotable[lastmodenumber].xdim = xdim;
    infotable[lastmodenumber].ydim = ydim;
    infotable[lastmodenumber].colors = cols;
    infotable[lastmodenumber].xbytes = xbytes;
    infotable[lastmodenumber].bytesperpixel = bytespp;

    return lastmodenumber;
}

int vga_getcrtcregs(unsigned char *regs) {
    int i;

    for (i = 0; i < CRT_C; i++) {
        regs[i] = __svgalib_incrtc(i);
    }
    return 0;
}

int vga_setcrtcregs(unsigned char *regs) {
    int i;
    
    if(CM>9) return -1; 
    
    __svgalib_outcrtc(0x11,__svgalib_incrtc(0x11)&0x7f);
    for (i = 0; i < CRT_C; i++) {
        __svgalib_outcrtc(i,regs[i]);
    }

    return 0;
}

int vga_setcolor(int color)
{
    switch (CI.colors) {
    case 2:
	if (color != 0)
	    color = 15;
    case 16:			/* update set/reset register */
#ifdef BACKGROUND
        __svgalib_dont_switch_vt_yet();
        if (!__svgalib_oktowrite)
           {
            color=color & 15;
            COL = color;
           }
	 else
	   {
#endif
	port_out(0x00, GRA_I);
	port_out((color & 15), GRA_D);
#ifdef BACKGROUND
           }
        __svgalib_is_vt_switching_needed();
#endif
	break;
    default:
	COL = color;
	break;
    }
    return 0;
}


int vga_screenoff(void)
{
    int tmp = 0;

    SCREENON = 0;

    if(__svgalib_novga) return 0; 
#ifdef BACKGROUND 
    __svgalib_dont_switch_vt_yet();
    if (!__svgalib_oktowrite) {
	__svgalib_is_vt_switching_needed();
	return(0);
    }
#endif

    if (__svgalib_driverspecs->emul && __svgalib_driverspecs->emul->screenoff) {
	tmp = __svgalib_driverspecs->emul->screenoff();
    } else {
	/* turn off screen for faster VGA memory acces */
	if (CHIPSET != EGA) {
	    port_out(0x01, SEQ_I);
	    port_out(port_in(SEQ_D) | 0x20, SEQ_D);
	}
	/* Disable video output */
#ifdef DISABLE_VIDEO_OUTPUT
	port_in(__svgalib_IS1_R);
	__svgalib_delay();
	port_out(0x00, ATT_IW);
#endif
    }

#ifdef BACKGROUND
    __svgalib_is_vt_switching_needed();
#endif
    return tmp;
}


int vga_screenon(void)
{
    int tmp = 0;

    SCREENON = 1;
    if(__svgalib_novga) return 0; 
#ifdef BACKGROUND
    __svgalib_dont_switch_vt_yet();
    if (!__svgalib_oktowrite) {
	__svgalib_is_vt_switching_needed();
	return(0);
    }
#endif
    if (__svgalib_driverspecs->emul && __svgalib_driverspecs->emul->screenon) {
	tmp = __svgalib_driverspecs->emul->screenon();
    } else {
	/* turn screen back on */
	if (CHIPSET != EGA) {
	    port_out(0x01, SEQ_I);
	    port_out(port_in(SEQ_D) & 0xDF, SEQ_D);
	}
/* #ifdef DISABLE_VIDEO_OUTPUT */
	/* enable video output */
	port_in(__svgalib_IS1_R);
	__svgalib_delay();
	port_out(0x20, ATT_IW);
/* #endif */
    }

#ifdef BACKGROUND
    __svgalib_is_vt_switching_needed();
#endif
    return 0;
}


int vga_getxdim(void)
{
    return CI.xdim;
}


int vga_getydim(void)
{
    return CI.ydim;
}


int vga_getcolors(void)
{
    return CI.colors;
}

int vga_white(void)
{
    switch (CI.colors) {
    case 2:
    case 16:
    case 256:
	return 15;
    case 1 << 15:
	return 32767;
    case 1 << 16:
	return 65535;
    case 1 << 24:
	return (1 << 24) - 1;
    }
    return CI.colors - 1;
}

int vga_claimvideomemory(int m)
{
    vga_modeinfo *modeinfo;
    int cardmemory;

    modeinfo = vga_getmodeinfo(CM);
    if (m < VMEM)
	return 0;
    if (modeinfo->colors == 16)
	cardmemory = modeinfo->maxpixels / 2;
    else
	cardmemory = (modeinfo->maxpixels * modeinfo->bytesperpixel
		      + 2) & 0xffff0000;
    /* maxpixels * bytesperpixel can be 2 less than video memory in */
    /* 3 byte-per-pixel modes; assume memory is multiple of 64K */
    if (m > cardmemory)
	return -1;
    VMEM = m;
    return 0;
}

int vga_setmodeX(void)
{
    switch (CM) {
    case TEXT:
/*    case G320x200x256: */
    case G320x240x256:
    case G320x400x256:
    case G360x480x256:
	return 0;
    }
    if (CI.colors == 256 && VMEM < 256 * 1024) {
	port_out(4, SEQ_I);	/* switch from linear to plane memory */
	port_out((port_in(SEQ_D) & 0xF7) | 0x04, SEQ_D);
	port_out(0x14, __svgalib_CRT_I);	/* switch double word mode off */
	port_out((port_in(__svgalib_CRT_D) & 0xBF), __svgalib_CRT_D);
	port_out(0x17, __svgalib_CRT_I);
	port_out((port_in(__svgalib_CRT_D) | 0x40), __svgalib_CRT_D);
	CI.xbytes = CI.xdim / 4;
	vga_setpage(0);
	MODEX = 1;
	return 1;
    }
    return 0;
}


static int saved_page;
static int saved_logicalwidth;
static int saved_displaystart;
static int saved_modeX;
#ifdef BACKGROUND
static int saved_linearset;
#endif

static void savestate(void)
{
    int i;

    vga_screenoff();

    savepalette(graph_red, graph_green, graph_blue);

    saved_page = __svgalib_currentpage;
    saved_logicalwidth = currentlogicalwidth;
    saved_displaystart = currentdisplaystart;
    saved_modeX = MODEX;
#ifdef BACKGROUND
    saved_linearset = __svgalib_modeinfo_linearset;
#endif

    if (CM == G320x200x256 && VMEM <= 65536) {
#ifndef BACKGROUND
	/* 320x200x256 is a special case; only 64K is addressable */
	/* (unless more has been claimed, in which case we assume */
	/* SVGA bank-switching) */
	if ((graph_buf = malloc(GRAPH_SIZE)) == NULL) {
#endif	
#ifdef BACKGROUND
#if BACKGROUND == 1
        if ((graph_buf = valloc(GRAPH_SIZE)) == NULL)
	    {
#endif
#if BACKGROUND == 2
	if ((graph_buf = malloc(GRAPH_SIZE)) == NULL) {
#endif
#endif
	    printf("Cannot allocate memory for VGA state\n");
	    vga_setmode(TEXT);
	    exit(1);
	}
	memcpy(graph_buf, GM, GRAPH_SIZE);
    } else if (MODEX || CM == G800x600x16 || (STDVGAMODE(CM) && CM != G320x200x256)) {
	/* for planar VGA modes, save the full 256K */
	__svgalib_vga_driverspecs.setmode(GPLANE16, prv_mode);
#ifndef BACKGROUND	
	if ((graph_buf = malloc(4 * GRAPH_SIZE)) == NULL) {
#endif	
#ifdef BACKGROUND
#if BACKGROUND == 1
        if ((graph_buf = valloc(4 * GRAPH_SIZE)) == NULL)
	    {
#endif
#if BACKGROUND == 2
        if ((graph_buf = malloc(4 * GRAPH_SIZE)) == NULL) {
#endif 
#endif	
	    printf("Cannot allocate memory for VGA state\n");
	    vga_setmode(TEXT);
	    exit(1);
	}
	for (i = 0; i < 4; i++) {
	    /* save plane i */
	    port_out(0x04, GRA_I);
	    port_out(i, GRA_D);
	    memcpy(graph_buf + i * GRAPH_SIZE, GM, GRAPH_SIZE);
	}
    } else if (CI.colors == 16) {
	int page, size, sbytes;
	unsigned char *sp;

	size = VMEM;
#ifndef BACKGROUND
	if ((graph_buf = malloc(4 * size)) == NULL) {
#endif
#ifdef BACKGROUND
#if BACKGROUND == 1
	if ((graph_buf = valloc(4 * size)) == NULL) {
#endif
#if BACKGROUND == 2
	if ((graph_buf = malloc(4 * size)) == NULL) {
#endif
#endif
	    printf("Cannot allocate memory for VGA state\n");
	    vga_setmode(TEXT);
	    exit(1);
	}
	sp = graph_buf;
	for (page = 0; size > 0; ++page) {
	    vga_setpage(page);
	    sbytes = (size > GRAPH_SIZE) ? GRAPH_SIZE : size;
	    for (i = 0; i < 4; i++) {
		/* save plane i */
		port_out(0x04, GRA_I);
		port_out(i, GRA_D);
		memcpy(sp, GM, sbytes);
		sp += sbytes;
	    }
	    size -= sbytes;
	}
#ifdef BACKGROUND
    } else if (__svgalib_modeinfo_linearset & IS_LINEAR) {   /* linear modes */
        if (! graph_buf2) { /* graph_buf2 only gets allocated _once_, and then reused */
            int page_size=getpagesize();
            if (! (graph_buf2=malloc(__svgalib_linear_memory_size+page_size-1))) {
                printf("Cannot allocate memory for linear state.\n");
                vga_setmode(TEXT);
                exit(1);
            }
#ifdef LINEAR_DEBUG
            printf("Allocated %d bytes for background screen storage\n",
                   __svgalib_linear_memory_size);
#endif
            graph_buf2 = (unsigned char *) /* make graph_buf2 page aligned */
                         (((unsigned long)graph_buf2 + page_size - 1) & ~(page_size-1));
        }

        if (! (graph_buf=malloc(GRAPH_SIZE/*__svgalib_linear_memory_size*/))) {
	    printf("Cannot allocate memory for planar state.\n");
	    vga_setmode(TEXT);
	    exit(1);
        }
        memcpy(graph_buf2,__svgalib_linearframebuffer,__svgalib_linear_memory_size);
        /* cpg: the following memcpy is needed, else the mmap() call in     */
        /* __svgalib_map_virtual_screen() bombs out with "invalid argument" */
        /* (kernel 2.1.125) */
        memcpy(graph_buf,__svgalib_linearframebuffer,GRAPH_SIZE/*__svgalib_linear_memory_size*/);
#endif
    } else {			/* SVGA, and SVGA 320x200x256 if videomemoryused > 65536 */
	int size;
	int page;

	size = VMEM;

#ifdef DEBUG
	printf("Saving %dK of video memory.\n", (size + 2) / 1024);
#endif
#ifndef BACKGROUND
	if ((graph_buf = malloc(size)) == NULL) {
#endif
#ifdef BACKGROUND
#if BACKGROUND == 1
        /* Must allocate hole videopage. */
	if ((graph_buf = valloc((size/GRAPH_SIZE+1)*GRAPH_SIZE)) == NULL) {
#endif
#if BACKGROUND == 2
	if ((graph_buf = malloc(size)) == NULL) {
#endif
#endif
	    printf("Cannot allocate memory for SVGA state.\n");
	    vga_setmode(TEXT);
	    exit(1);
	}
	page = 0;
	while (size >= 65536) {
	    vga_setpage(page);
	    memcpy(graph_buf + page * 65536, GM, 65536);
	    page++;
	    size -= 65536;
	}
#ifdef BACKGROUND
#if BACKGROUND == 1
        /* Whole page must be written for mmap(). */
	if (size > 0) {
	    vga_setpage(page);
	    memcpy(graph_buf + page * 65536, GM, GRAPH_SIZE);
	}
#endif
#if BACKGROUND == 2
	if (size > 0) {
	    vga_setpage(page);
	    memcpy(graph_buf + page * 65536, GM, size);
	}
#endif
#endif
#ifndef BACKGROUND
	if (size > 0) {
	    vga_setpage(page);
	    memcpy(graph_buf + page * 65536, GM, size);
	}
#endif
    }
}

static void restorestate(void)
{
    int i;

    vga_screenoff();

    if (saved_modeX)
	vga_setmodeX();

    restorepalette(graph_red, graph_green, graph_blue);

    if (CM == G320x200x256 && VMEM <= 65536) {
	memcpy(GM, graph_buf, 65536);
    } else if (MODEX || CM == G800x600x16 || (STDVGAMODE(CM) && CM != G320x200x256)) {
	int setresetreg, planereg;
	/* disable Set/Reset Register */
	port_out(0x01, GRA_I);
	setresetreg = inb(GRA_D);
	port_out(0x00, GRA_D);
	outb(SEQ_I, 0x02);
	planereg = inb(SEQ_D);

	for (i = 0; i < 4; i++) {
	    /* restore plane i */
	    port_out(0x02, SEQ_I);
	    port_out(1 << i, SEQ_D);
	    memcpy(GM, graph_buf + i * GRAPH_SIZE, GRAPH_SIZE);
	}
	outb(GRA_I, 0x01);
	outb(GRA_D, setresetreg);
	outb(SEQ_I, 0x02);
	outb(SEQ_D, planereg);
    } else if (CI.colors == 16) {
	int page, size, rbytes;
	unsigned char *rp;
	int setresetreg, planereg;

	/* disable Set/Reset Register */
	port_out(0x01, GRA_I);
	if (CHIPSET == EGA)
	    setresetreg = 0;
	else
	    setresetreg = inb(GRA_D);
	port_out(0x00, GRA_D);
	port_out(0x02, SEQ_I);
	if (CHIPSET == EGA)
	    planereg = 0;
	else
	    planereg = inb(SEQ_D);

	size = VMEM;
	rp = graph_buf;
	for (page = 0; size > 0; ++page) {
	    vga_setpage(page);
	    rbytes = (size > GRAPH_SIZE) ? GRAPH_SIZE : size;
	    for (i = 0; i < 4; i++) {
		/* save plane i */
		port_out(0x02, SEQ_I);
		port_out(1 << i, SEQ_D);
		memcpy(GM, rp, rbytes);
		rp += rbytes;
	    }
	    size -= rbytes;
	}

	outb(GRA_I, 0x01);
	outb(GRA_D, setresetreg);
	outb(SEQ_I, 0x02);
	outb(SEQ_D, planereg);
#ifdef BACKGROUND
    } else if (saved_linearset & IS_LINEAR) {
        memcpy(__svgalib_linearframebuffer,graph_buf2,__svgalib_linear_memory_size);
#ifdef LINEAR_DEBUG
        dump_mem("restore-lin");
#endif
#endif
    } else {
/*              vga_modeinfo *modeinfo; */
	int size;
	int page;
	size = VMEM;

#ifdef DEBUG
	printf("Restoring %dK of video memory.\n", (size + 2) / 1024);
#endif
	page = 0;
	while (size >= 65536) {
	    vga_setpage(page);
	    memcpy(GM, graph_buf + page * 65536, 65536);
	    size -= 65536;
	    page++;
	}
	if (size > 0) {
	    vga_setpage(page);
	    memcpy(GM, graph_buf + page * 65536, size);
	}
    }

    if (saved_logicalwidth != CI.xbytes)
	vga_setlogicalwidth(saved_logicalwidth);
    if (saved_page != 0)
	vga_setpage(saved_page);
    if (saved_displaystart != 0)
	vga_setdisplaystart(saved_displaystart);

    vga_screenon();

    free(graph_buf);
}


int vga_getch(void)
{
    char c;

    if (CM == TEXT)
	return -1;

    while ((read(__svgalib_tty_fd, &c, 1) < 0) && (errno == EINTR));

    return c;
}

#ifdef BACKGROUND
int __svgalib_flip_status(void)

{
 return(flip);
}
#endif

/* I have kept the slightly funny 'flip' terminology. */

void __svgalib_flipaway(void)
{
    /* Leaving console. */
    flip_mode = CM;
#ifndef SVGA_AOUT
    __joystick_flip_vc(0);
#endif
    if (CM != TEXT) {
	/* wait for any blitter operation to finish */
	idle_accel();
	/* Save state and go to textmode. */
        __svgalib_saveregs(graph_regs);
	savestate();
	flip = 1;
        if(!__svgalib_secondary)vga_setmode(TEXT);
	flip = 0;

        if(!__svgalib_secondary){
#ifdef BACKGROUND
/* Let's put bg screen to right place */

#if BACKGROUND == 1
          if (__svgalib_currentpage<0) __svgalib_currentpage=0;
          __svgalib_map_virtual_screen(__svgalib_currentpage);
#endif
#if BACKGROUND == 2
          __svgalib_graph_mem=graph_buf+(GRAPH_SIZE*__svgalib_currentpage);
#endif
	  __svgalib_oktowrite=0; /* screen is fliped, not __svgalib_oktowrite. */
#endif
        }
    }
}

#ifndef BACKGROUND
static void __svgalib_flipback(void)
#endif
#ifdef BACKGROUND
void __svgalib_flipback(void)
#endif
{
#ifdef BACKGROUND
 int tmp_page=__svgalib_currentpage;
#endif
    /* Entering console. */
    /* Hmmm... and how about unlocking anything someone else locked? */
#ifndef SVGA_AOUT
    __joystick_flip_vc(1);
#endif
    chipset_unlock();
    if (flip_mode != TEXT) {
	/* Restore graphics mode and state. */
        if(!__svgalib_secondary){
#ifdef BACKGROUND
          __svgalib_oktowrite=1;
#endif
  	  flip = 1;
	  vga_setmode(flip_mode);
	  flip = 0;
#ifdef BACKGROUND
#if BACKGROUND == 1
          __svgalib_map_virtual_screen(1000000);	
#endif
#if BACKGROUND == 2
          __svgalib_graph_mem=__svgalib_graph_mem_orginal;
#endif
        /* do we need to explicitly enable linear? */
          if ((saved_linearset & IS_LINEAR) &&   /* if old mode was linear */
              __svgalib_driverspecs->linear) {   /* and linear function present */
              __svgalib_driverspecs->linear(LINEAR_ENABLE, (long)__svgalib_physaddr);
          }
#endif	
          __svgalib_setregs(graph_regs);
	  restorestate();
#ifdef BACKGROUND
 /* Has to make sure right page is on. */
          vga_setpage(tmp_page);
	  vga_setcolor(COL);
#endif
        }  
    }
}

int vga_flip(void)
{
#ifdef BACKGROUND
 int tmp_page=__svgalib_currentpage;
#endif    
    if (CM != TEXT) {		/* save state and go to textmode */
	savestate();
	flip_mode = CM;
	flip = 1;
	vga_setmode(TEXT);
	flip = 0;
#ifdef BACKGROUND
/* Lets put bg screen to right place */

#if BACKGROUND == 1
        if (__svgalib_currentpage<0) __svgalib_currentpage=0;
        __svgalib_map_virtual_screen(__svgalib_currentpage);
#endif
#if BACKGROUND == 2
        __svgalib_graph_mem=graph_buf+(GRAPH_SIZE*__svgalib_currentpage);;
#endif
	__svgalib_oktowrite=0; /* screen is fliped, not __svgalib_oktowrite. */
#endif
    } else {			/* restore graphics mode and state */
#ifdef BACKGROUND
        __svgalib_oktowrite=1;
	/* Probably here too ...  */
	chipset_unlock();
#endif
	flip = 1;
	vga_setmode(flip_mode);
	flip = 0;
#ifdef BACKGROUND
#if BACKGROUND == 1
        __svgalib_map_virtual_screen(1000000);	
#endif
#if BACKGROUND == 2
        __svgalib_graph_mem=__svgalib_graph_mem_orginal;
#endif
        if (__svgalib_modeinfo_linearset & IS_LINEAR) {
            int (*lfn) (int op, int param) = __svgalib_driverspecs->linear;
            if (lfn) (*lfn) (LINEAR_ENABLE, (long)__svgalib_physaddr);
        }
#endif	
	restorestate();
#ifdef BACKGROUND
 /* Has to make sure right page is on. */
        vga_setpage(tmp_page);
	vga_setcolor(COL);
#endif
    }
    return 0;
}


int vga_setflipchar(int c)
/* This function is obsolete. Retained for VGAlib compatibility. */
{
    __svgalib_flipchar = c;

    return 0;
}

void vga_setlogicalwidth(int w)
{
    __svgalib_driverspecs->setlogicalwidth(w);
    currentlogicalwidth = w;
}

void vga_setdisplaystart(int a)
{
    currentdisplaystart = a;
    if (CHIPSET != VGA && CHIPSET != EGA)
	if (MODEX || CI.colors == 16) {
	    /* We are currently using a Mode X-like mode on a */
	    /* SVGA card, use the standard VGA function */
	    /* that works properly for Mode X. */
	    /* Same goes for 16 color modes. */
	    __svgalib_vga_driverspecs.setdisplaystart(a);
	    return;
	}
    /* Call the regular display start function for the chipset */
    __svgalib_driverspecs->setdisplaystart(a);
}

void vga_bitblt(int srcaddr, int destaddr, int w, int h, int pitch)
{
    __svgalib_driverspecs->bitblt(srcaddr, destaddr, w, h, pitch);
}

void vga_imageblt(void *srcaddr, int destaddr, int w, int h, int pitch)
{
    __svgalib_driverspecs->imageblt(srcaddr, destaddr, w, h, pitch);
}

void vga_fillblt(int destaddr, int w, int h, int pitch, int c)
{
    __svgalib_driverspecs->fillblt(destaddr, w, h, pitch, c);
}

void vga_hlinelistblt(int ymin, int n, int *xmin, int *xmax, int pitch,
		      int c)
{
    __svgalib_driverspecs->hlinelistblt(ymin, n, xmin, xmax, pitch, c);
}

void vga_blitwait(void)
{
    __svgalib_driverspecs->bltwait();
}

int vga_ext_set(unsigned what,...)
{
    va_list params;
    register int retval = 0;

    switch(what) {
	case VGA_EXT_AVAILABLE:
	    /* Does this use of the arglist corrupt non-AVAIL_ACCEL ext_set? */
	    va_start(params, what);
	    switch (va_arg(params, int)) {
		case VGA_AVAIL_ACCEL:
		if (__svgalib_driverspecs->accelspecs != NULL)
		    retval = __svgalib_driverspecs->accelspecs->operations;
		break;
	    case VGA_AVAIL_ROP:
		if (__svgalib_driverspecs->accelspecs != NULL)
		    retval = __svgalib_driverspecs->accelspecs->ropOperations;
		break;
	    case VGA_AVAIL_TRANSPARENCY:
		if (__svgalib_driverspecs->accelspecs != NULL)
		    retval = __svgalib_driverspecs->accelspecs->transparencyOperations;
		break;
	    case VGA_AVAIL_ROPMODES:
		if (__svgalib_driverspecs->accelspecs != NULL)
		    retval = __svgalib_driverspecs->accelspecs->ropModes;
		break;
	    case VGA_AVAIL_TRANSMODES:
		if (__svgalib_driverspecs->accelspecs != NULL)
		    retval = __svgalib_driverspecs->accelspecs->transparencyModes;
		break;
	    case VGA_AVAIL_SET:
		retval = (1 << VGA_EXT_PAGE_OFFSET) |
			 (1 << VGA_EXT_FONT_SIZE);	/* These are handled by us */
		break;
	    }
	    va_end(params);
	    break;
	case VGA_EXT_PAGE_OFFSET:
	    /* Does this use of the arglist corrupt it? */
	    va_start(params, what);
	    retval = vga_page_offset;
	    vga_page_offset = va_arg(params, int);
	    va_end(params);
	    return retval;
	case VGA_EXT_FONT_SIZE:
	    va_start(params, what);
	    what = va_arg(params, unsigned int);
	    va_end(params);
	    if (!what)
		return FONT_SIZE;
	    retval = fontbufsize;
	    fontbufsize = what;
	    return retval;
    }
    if ((CM != TEXT) && (MODEFLAGS & HAVE_EXT_SET)) {
	va_start(params, what);
	retval |= __svgalib_driverspecs->ext_set(what, params);
	va_end(params);
    }
    return retval;
}

/* Parse a string for options.. str is \0-terminated source,
   commands is an array of char ptrs (last one is NULL) containing commands
   to parse for. (if first char is ! case sensitive),
   func is called with ind the index of the detected command.
   func has to return the ptr to the next unhandled token returned by strtok(NULL," ").
   Use strtok(NULL," ") to get the next token from the file..
   mode is 1 when reading from conffile and 0 when parsing the env-vars. This is to
   allow disabling of dangerous (hardware damaging) options when reading the ENV-Vars
   of Joe user.
   Note: We use strtok, that is str is destroyed! */
static void parse_string(char *str, char **commands, char *(*func) (int ind, int mode), int mode)
{
    int index;
    register char *ptr, **curr;

    /*Pass one, delete comments,ensure only whitespace is ' ' */
    for (ptr = str; *ptr; ptr++) {
	if (*ptr == '#') {
	    while (*ptr && (*ptr != '\n')) {
		*ptr++ = ' ';
	    }
	    if (*ptr)
		*ptr = ' ';
	} else if (isspace(*ptr)) {
	    *ptr = ' ';
	}
    }
    /*Pass two, parse commands */
    ptr = strtok(str, " ");
    while (ptr) {
#ifdef DEBUG_CONF
	printf("Parsing: %s\n", ptr);
#endif
	for (curr = commands, index = 0; *curr; curr++, index++) {
#ifdef DEBUG_CONF
	    printf("Checking: %s\n", *curr);
#endif
	    if (**curr == '!') {
		if (!strcmp(*curr + 1, ptr)) {
		    ptr = (*func) (index, mode);
		    break;
		}
	    } else {
		if (!strcasecmp(*curr, ptr)) {
		    ptr = (*func) (index, mode);
		    break;
		}
	    }
	}
	if (!*curr)		/*unknow command */
	    ptr = strtok(NULL, " ");	/* skip silently til' next command */
    }
}

static int allowoverride = 0;	/* Allow dangerous options in ENV-Var or in */
				/* the $HOME/.svgalibrc */

static void process_config_file(FILE *file, int mode, char **commands,
		 			char *(*func)(int ind, int mode)) {
 struct stat st;
 char *buf, *ptr;
 int i;

  fstat(fileno(file), &st);	/* Some error analysis may be fine here.. */
  if ( (buf = alloca(st.st_size + 1)) == 0) {	/* + a final \0 */
    puts("svgalib: out of mem while parsing config file !");
    return;
  }
  fread(buf, 1, st.st_size, file);
  for (i = 0, ptr = buf; i < st.st_size; i++, ptr++) {
    if (!*ptr)
      *ptr = ' ';			/* Erase any maybe embedded \0 */
    }
  *ptr = 0;					/* Trailing \0 */
  parse_string(buf, commands, func, mode);	/* parse config file */
}

/* This is a service function for drivers. Commands and func are as above.
   The following config files are parsed in this order:
    - /etc/vga/libvga.conf (#define SVGALIB_CONFIG_FILE)
    - ~/.svgalibrc
    - the file where env variavle SVGALIB_CONFIG_FILE points
    - the env variable SVGALIB_CONFIG (for compatibility, but I would remove
      it, we should be more flexible...  Opinions ?)
    - MW: I'd rather keep it, doesn't do too much harm and is sometimes nice
      to have.
*/
void __svgalib_read_options(char **commands, char *(*func) (int ind, int mode)) {
    FILE *file;
    char *buf = NULL, *ptr;
    int i;

    if ( (file = fopen(SVGALIB_CONFIG_FILE, "r")) != 0) {
#ifdef DEBUG_CONF
  printf("Processing config file \'%s\'\n", SVGALIB_CONFIG_FILE);
#endif
      process_config_file(file, 1, commands, func);
      fclose(file);
    } else {
	fprintf(stderr, "svgalib: Configuration file \'%s\' not found.\n", SVGALIB_CONFIG_FILE);
    }

    if ( (ptr = getenv("HOME")) != 0) {
      char *filename; 

      filename = alloca(strlen(ptr) + 20);
      if (!filename) {
	puts("svgalib: out of mem while parsing SVGALIB_CONFIG_FILE !");
      } else {
	strcpy(filename, ptr);
	strcat(filename, "/.svgalibrc");
	if ( (file = fopen(filename, "r")) != 0) {
#ifdef DEBUG_CONF
	  printf("Processing config file \'%s\'\n", filename);
#endif
	  process_config_file(file, allowoverride, commands, func);
	  fclose(file);
	}
      }
    }

    if ( (ptr = getenv("SVGALIB_CONFIG_FILE")) != 0) {
      if ( (file = fopen(ptr, "r")) != 0) {
#ifdef DEBUG_CONF
  printf("Processing config file \'%s\'\n", ptr);
#endif
	process_config_file(file, allowoverride, commands, func);
	fclose(file);
      } else {
        fprintf(stderr, "svgalib: warning: config file \'%s\', pointed to by SVGALIB_CONFIG_FILE, not found !\n", ptr);
      }
    }

    if ( (ptr = getenv("SVGALIB_CONFIG")) != 0  &&  (i = strlen(ptr)) != 0) {
      buf = alloca(i + 1);
      if (!buf) {
	puts("svgalib: out of mem while parsing SVGALIB_CONFIG !");
      } else {
	strcpy(buf, ptr);		/* Copy for safety and strtok!! */
#ifdef DEBUG_CONF
	puts("Parsing env variable \'SVGALIB_CONFIG\'");
#endif
	parse_string(buf, commands, func, allowoverride);
      }
    }
}

/* Configuration file, mouse interface, initialization. */

static int configfileread = 0;	/* Boolean. */

/* What are these m0 m1 m... things ? Shouldn't they be removed ? */
static char *vga_conf_commands[] = {
    "mouse", "monitor", "!m", "!M", "chipset", "overrideenable", "!m0", "!m1", "!m2", "!m3",
    "!m4", "!m9", "!M0", "!M1", "!M2", "!M3", "!M4", "!M5", "!M6", "nolinear",
    "linear", "!C0", "!C1", "!C2", "!C3", "!C4", "!C5", "!C6", "!C7", "!C8", "!C9",
    "!c0", "!c1", "monotext", "colortext", "!m5",
    "leavedtr", "cleardtr", "setdtr", "leaverts", "clearrts",
    "setrts", "grayscale", "horizsync", "vertrefresh", "modeline",
    "security","mdev", "default_mode", "nosigint", "sigint",
    "joystick0", "joystick1", "joystick2", "joystick3",
    "textprog", "vesatext", "vesasave", "secondary", "bandwidth", 
    "novccontrol", "newmode", "noprocpci", "vesatextmode", "pcistart",
    "ragedoubleclock", "vesacputype",
    NULL};

static char *conf_mousenames[] =
{
  "Microsoft", "MouseSystems", "MMSeries", "Logitech", "Busmouse", "PS2",
    "MouseMan", "gpm", "Spaceball", "none", "IntelliMouse", "IMPS2", "pnp", 
    "WacomGraphire", "DRMOUSE4DS", NULL};

static int check_digit(char *ptr, char *digits)
{
    if (ptr == NULL)
	return 0;
    return strlen(ptr) == strspn(ptr, digits);
}

static char *process_option(int command, int mode)
{
    static char digits[] = ".0123456789";
    char *ptr, **tabptr, *ptb;
    int i, j;
    float f;

#ifdef DEBUG_CONF
    printf("command %d detected.\n", command);
#endif
    switch (command) {
    case 5:
#ifdef DEBUG_CONF
	puts("Allow override");
#endif
	if (mode)
	    allowoverride = 1;
	else
	    puts("Overrideenable denied. (Gee.. Do you think I'm that silly?)");
	break;
    case 0:			/* mouse */
    case 2:			/* m */
	ptr = strtok(NULL, " ");
	if (ptr == NULL)
	    goto inv_mouse;
	if (check_digit(ptr, digits + 1)) {	/* It is a number.. */
	    i = atoi(ptr);
	    if ((i < 0) || (i > 9))
		goto inv_mouse;
	    mouse_type = i;
	} else {		/* parse for symbolic name.. */
	    for (i = 0, tabptr = conf_mousenames; *tabptr; tabptr++, i++) {
		if (!strcasecmp(ptr, *tabptr)) {
		    mouse_type = i;
#ifdef DEBUG_DRMOUSE4DS
	fprintf(stderr, "mouse type: %d: %s \n", i, conf_mousenames[i]);
#endif
		    goto leave;
		}
	    }
	  inv_mouse:
	    printf("svgalib: Illegal mouse setting: {mouse|m} %s\n"
		   "Correct usage: {mouse|m} mousetype\n"
		   "where mousetype is one of 0, 1, 2, 3, 4, 5, 6, 7, 9,\n",
		   (ptr != NULL) ? ptr : "");
	    for (tabptr = conf_mousenames, i = 0; *tabptr; tabptr++, i++) {
		if (i == MOUSE_NONE)
		    continue;
		printf("%s, ", *tabptr);
	    }
	    puts("or none.");
	    return ptr;		/* Allow a second parse of str */
	}
	break;
    case 1:			/* monitor */
    case 3:			/* M */
	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits + 1)) {	/* It is an int.. */
	    i = atoi(ptr);
	    if (i < 7) {
		command = i + 12;
		goto monnum;
	    } else {
		f = i;
		goto monkhz;
	    }
	} else if (check_digit(ptr, digits)) {	/* It is a float.. */
	    f = atof(ptr);
	  monkhz:
	    if (!mode)
		goto mon_deny;
	    __svgalib_horizsync.max = f * 1000.0f;
	} else {
	    printf("svgalib: Illegal monitor setting: {monitor|M} %s\n"
		   "Correct usage: {monitor|M} monitortype\n"
		   "where monitortype is one of 0, 1, 2, 3, 4, 5, 6, or\n"
		   "maximal horz. scan frequency in khz.\n"
		   "Example: monitor 36.5\n",
		   (ptr != NULL) ? ptr : "");
	    return ptr;		/* Allow a second parse of str */
	}
	break;
    case 4:			/* chipset */
	ptr = strtok(NULL, " ");
	if (ptr == NULL) {
	    puts("svgalib: Illegal chipset setting: no chipset given");
	    goto chip_us;
	}
	/*First param is chipset */
	for (i = 0, tabptr = driver_names; *tabptr; tabptr++, i++) {
	    if (!strcasecmp(ptr, *tabptr)) {
		if (!__svgalib_driverspecslist[i]) {
		    printf("svgalib: Illegal chipset setting: Driver for %s is NOT compiled in.\n",
			ptr);
		    continue; /* The for above will loop a few more times and fail */
		}
		ptr = strtok(NULL, " ");
		if (check_digit(ptr, digits + 1)) {
		    j = atoi(ptr);
		    ptr = strtok(NULL, " ");
		    if (check_digit(ptr, digits + 1)) {
			if (mode)
			    vga_setchipsetandfeatures(i, j, atoi(ptr));
			else {
			  chipdeny:
			    puts("chipset override from environment denied.");
			}
			return strtok(NULL, " ");
		    } else {
			puts("svgalib: Illegal chipset setting: memory is not a number");
			goto chip_us;
		    }
		}
		if (mode)
		    vga_setchipset(i);
		else
		    puts("chipset override from environment denied.");
		return ptr;
	    }
	}
	printf("svgalib: Illegal chipset setting: chipset %s\n", ptr);
      chip_us:
	puts("Correct usage: chipset driver [par1 par2]\n"
	     "where driver is one of:");
	ptb = "%s";
	for (i = 0, tabptr = driver_names; *tabptr; tabptr++, i++) {
	    if (__svgalib_driverspecslist[i] != NULL) {
		printf(ptb, *tabptr);
		ptb = ", %s";
	    }
	}
	puts("\npar1 and par2 are river dependant integers.\n"
	     "Example: Chipset VGA    or\n"
	     "Chipset VGA 0 512");
	return ptr;
    case 6:			/* oldstyle config: m0-m4 */
    case 7:
    case 8:
    case 9:
    case 10:
	mouse_type = command - 6;
	break;
    case 11:			/* m9 */
	mouse_type = MOUSE_NONE;
	break;
    case 12:			/* oldstyle config: M0-M6 */
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
      monnum:
	if (!mode) {
	  mon_deny:
	    puts("Monitor setting from environment denied.");
	    break;
	} else {
	    __svgalib_horizsync.max = __svgalib_maxhsync[command - 12];
	}
	break;
    case 19:			/*nolinear */
	modeinfo_mask &= ~CAPABLE_LINEAR;
	break;
    case 20:			/*linear */
	modeinfo_mask |= CAPABLE_LINEAR;
	break;
    case 21:			/* oldstyle chipset C0 - C9 */
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
	if (!mode)
	    goto chipdeny;
	vga_setchipset(command - 21);
	break;
    case 31:			/* c0-c1 color-text selection */
	if (!mode) {
	  coltexdeny:
	    puts("Color/mono text selection from environment denied.");
	    break;
	}
	color_text = 0;
	break;
    case 32:
	if (!mode) {
	    puts("Color/mono text selection from environment denied.");
	    break;
	}
	color_text = 1;
	break;
    case 33:
    case 34:
	if (!mode)
	    goto coltexdeny;
	color_text = command - 32;
	break;
    case 35:			/* Mouse type 5 - "PS2". */
	mouse_type = 5;
	break;
    case 36:
	mouse_modem_ctl &= ~(MOUSE_CHG_DTR | MOUSE_DTR_HIGH);
	break;
    case 37:
	mouse_modem_ctl &= ~MOUSE_DTR_HIGH;
	mouse_modem_ctl |= MOUSE_CHG_DTR;
	break;
    case 38:
	mouse_modem_ctl |= (MOUSE_CHG_RTS | MOUSE_RTS_HIGH);
	break;
    case 39:
	mouse_modem_ctl &= ~(MOUSE_CHG_RTS | MOUSE_RTS_HIGH);
	break;
    case 40:
	mouse_modem_ctl &= ~MOUSE_RTS_HIGH;
	mouse_modem_ctl |= MOUSE_CHG_RTS;
	break;
    case 41:
	mouse_modem_ctl |= (MOUSE_CHG_RTS | MOUSE_RTS_HIGH);
	break;
    case 42:			/* grayscale */
	__svgalib_grayscale = 1;
	break;
    case 43:			/* horizsync */
	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits)) {		/* It is a float.. */
	    f = atof(ptr);
	    if (!mode)
		goto mon_deny;
	    __svgalib_horizsync.min = f * 1000;
	} else
	    goto hs_bad;

	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits)) {		/* It is a float.. */
	    f = atof(ptr);
	    if (!mode)
		goto mon_deny;
	    __svgalib_horizsync.max = f * 1000;
	} else {
	  hs_bad:
	    printf("svgalib: Illegal HorizSync setting.\n"
		   "Correct usage: HorizSync min_kHz max_kHz\n"
		   "Example: HorizSync 31.5 36.5\n");
	}
	break;
    case 44:			/* vertrefresh */
	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits)) {		/* It is a float.. */
	    f = atof(ptr);
	    if (!mode)
		goto mon_deny;
	    __svgalib_vertrefresh.min = f;
	} else
	    goto vr_bad;

	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits)) {		/* It is a float.. */
	    f = atof(ptr);
	    if (!mode)
		goto mon_deny;
	    __svgalib_vertrefresh.max = f;
	} else {
	  vr_bad:
	    printf("svgalib: Illegal VertRefresh setting.\n"
		   "Correct usage: VertRefresh min_Hz max_Hz\n"
		   "Example: VertRefresh 50 70\n");
	}
	break;
    case 45:{			/* modeline */
	    MonitorModeTiming mmt;
	    const struct {
		char *name;
		int val;
	    } options[] = {
		{
		    "-hsync", NHSYNC
		},
		{
		    "+hsync", PHSYNC
		},
		{
		    "-vsync", NVSYNC
		},
		{
		    "+vsync", PVSYNC
		},
		{
		    "interlace", INTERLACED
		},
		{
		    "interlaced", INTERLACED
		},
		{
		    "doublescan", DOUBLESCAN
		}
	    };
#define ML_NR_OPTS (sizeof(options)/sizeof(*options))

	    /* Skip the name of the mode */
	    ptr = strtok(NULL, " ");
	    if (!ptr)
		break;

	    ptr = strtok(NULL, " ");
	    if (!ptr)
		break;
	    mmt.pixelClock = atof(ptr) * 1000;

#define ML_GETINT(x) \
	ptr = strtok(NULL, " "); if(!ptr) break; \
	mmt.##x = atoi(ptr);

	    ML_GETINT(HDisplay);
	    ML_GETINT(HSyncStart);
	    ML_GETINT(HSyncEnd);
	    ML_GETINT(HTotal);
	    ML_GETINT(VDisplay);
	    ML_GETINT(VSyncStart);
	    ML_GETINT(VSyncEnd);
	    ML_GETINT(VTotal);
	    mmt.flags = 0;
	    while ((ptr = strtok(NULL, " "))) {
		for (i = 0; i < ML_NR_OPTS; i++)
		    if (!strcasecmp(ptr, options[i].name))
                      {
                        mmt.flags |= options[i].val;
                        break;
                      }
		if (i == ML_NR_OPTS)
		    break;
	    }
#undef ML_GETINT
#undef ML_NR_OPTS

	    __svgalib_addusertiming(&mmt);
	    return ptr;
	    
	}
    case 46:
	if (!mode) {
	    puts("Security setting from environment denied.");
	    break;
	}
	if ( (ptr = strtok( NULL, " ")) ) {
	    if (!strcasecmp("revoke-all-privs", ptr)) {
		 __svgalib_security_revokeallprivs = 1;
		 break;
	    } else if (!strcasecmp("compat", ptr)) {
		 __svgalib_security_revokeallprivs = 0;
		 break;
	    }
	} 
	puts("svgalib: Unknown security options\n");
	break;
    case 47:
	ptr = strtok(NULL," ");
	if (ptr) {
	    mouse_device = strdup(ptr);
	    if (mouse_device == NULL) {
	      nomem:
		puts("svgalib: Fatal error: out of memory.");
		exit(1);
	    }
	} else
	    goto param_needed;
	break;
    case 48:		/* default_mode */
	if ( (ptr = strtok(NULL, " ")) != 0) {
	 int mode = vga_getmodenumber(ptr);
	  if (mode != -1) {
	    __svgalib_default_mode = mode;
	  } else {
	    printf("svgalib: config: illegal mode \'%s\' for \'%s\'\n",
	   			  ptr, vga_conf_commands[command]);
	  }
	} else {
  param_needed:
  	  printf("svgalib: config: \'%s\' requires parameter(s)",
  	  				vga_conf_commands[command]);
	  break;
	}
	break;
    case 49: /* nosigint */
	__svgalib_nosigint = 1;
	break;
    case 50: /* sigint */
	__svgalib_nosigint = 0;
	break;
    case 51: /* joystick0 */
    case 52: /* joystick1 */
    case 53: /* joystick2 */
    case 54: /* joystick3 */
	if (! (ptr = strtok(NULL, " ")) )
		goto param_needed;
#ifndef SVGA_AOUT
	if (__joystick_devicenames[command - 51])
	    free(__joystick_devicenames[command - 51]);
	__joystick_devicenames[command - 51] = strdup(ptr);
	if (!__joystick_devicenames[command - 51])
	    goto nomem;
#else
	printf("svgalib: No joystick support in a.out version.\n");
#endif
	break;
    case 55: /* TextProg */
	ptr = strtok(NULL," ");
        if(ptr==NULL)break;
        __svgalib_textprog|=2;
	__svgalib_TextProg = strdup(ptr);
	if (!__svgalib_TextProg)
	    goto nomem;
        i=1;
        while(((ptr=strtok(NULL," "))!=NULL) &&
	       (i< ((sizeof(__svgalib_TextProg_argv) / sizeof(char *)) + 1)) &&
	       strcmp(ptr,"END")){
	   __svgalib_TextProg_argv[i]=strdup(ptr);
	   if (!__svgalib_TextProg_argv[i])
	       goto nomem;
	   i++;
        };
        __svgalib_TextProg_argv[i]=NULL;
        ptb=strrchr(__svgalib_TextProg,'/');
        __svgalib_TextProg_argv[0]=ptb?ptb + 1:__svgalib_TextProg;
        break;
    case 56:
#ifdef INCLUDE_VESA_DRIVER
        __svgalib_vesatext=1;
        break;
#else
       printf("svgalib: Warning: VESA support not enabled!\n");
#endif
    case 57: /* Vesa save bitmap */  
#ifdef INCLUDE_VESA_DRIVER
       ptr = strtok(NULL, " ");
       if(ptr!=NULL){
         j = atoi(ptr);
         __svgalib_VESA_savebitmap=j;
       };
#else
       printf("svgalib: Warning: VESA support not enabled!\n");
#endif
       break;
    case 58:
        __svgalib_secondary=1;
        break;
    case 59:		/* max bandwidth */
	if ( (ptr = strtok(NULL, " ")) != 0) {
	    if (check_digit(ptr, digits)) {
	        int f = atoi(ptr);
	        if (f<31000)f=31000;
	        __svgalib_bandwidth = f;
	    }
	} else {
  	    printf("svgalib: config: \'%s\' requires parameter(s)",
  	  				vga_conf_commands[command]);
	    break;
	}
	break;
    case 60:
        __svgalib_novccontrol=1;
        break;
    case 61: {
        int x,y,c,p,b;

    	ptr = strtok(NULL, " "); 
        if(!ptr) break; 
        x = atoi(ptr);
    	ptr = strtok(NULL, " "); 
        if(!ptr) break; 
        y = atoi(ptr);
    	ptr = strtok(NULL, " "); 
        if(!ptr) break; 
        c = atoi(ptr);
    	ptr = strtok(NULL, " "); 
        if(!ptr) break; 
        p = atoi(ptr);
    	ptr = strtok(NULL, " "); 
        if(!ptr) break; 
        b = atoi(ptr);

        vga_addmode(x,y,c,p,b);
        };
        break;
    case 62:
        __svgalib_use_procpci=0;
        break;
    case 63: /* Vesa text mode number */  
#ifdef INCLUDE_VESA_DRIVER
       ptr = strtok(NULL, " ");
       if(ptr!=NULL){
         j = atoi(ptr);
         __svgalib_VESA_textmode=j;
       };
#else
       printf("svgalib: Warning: VESA support not enabled!\n");
#endif
       break;
    case 64:			/* pci initial values */
	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits)) {
            j = atoi(ptr);
	    if((j<16)&&(j>=0))__svgalib_pci_ibus = j;
	} else
	    goto ps_bad;

	ptr = strtok(NULL, " ");
	if (check_digit(ptr, digits)) {	
            j = atoi(ptr);
	    if((j<256)&&(j>=0))__svgalib_pci_idev = j;
	} else {
        ps_bad:
	    printf("svgalib: Illegal PCI initial values setting.\n"
		   "Correct usage: PCIStart initial_bus initial_dev"
		   "Example: PCIStart 1 0\n");
	}
	break;
    case 65:
        __svgalib_ragedoubleclock=1;
        break;
    case 66: /* cpu type */
        ptr = strtok(NULL, " ");
#ifdef INCLUDE_VESA_DRIVER
        if(ptr!=NULL){
            j = atoi(ptr);
            __svgalib_lrmi_cpu_type=j;
        };
#endif
        break;
  }
  leave:
    return strtok(NULL, " ");
}

static void readconfigfile(void)
{
    if (configfileread)
	return;
    configfileread = 1;
    mouse_type = -1;

    {
        struct stat buf;
        
        if(!stat("/proc/bus/pci",&buf))__svgalib_use_procpci=1;
          else __svgalib_use_procpci=0;
    }

    __svgalib_read_options(vga_conf_commands, process_option);
    if (mouse_type == -1) {
	mouse_type = MOUSE_MICROSOFT;	/* Default. */
	puts("svgalib: Assuming Microsoft mouse.");
    }
    if (__svgalib_horizsync.max == 0U) {
	/* Default monitor is low end SVGA/8514. */
	__svgalib_horizsync.min = 31500U;
	__svgalib_horizsync.max = 35500U;
	puts("svgalib: Assuming low end SVGA/8514 monitor (35.5 KHz).");
    }
#ifdef DEBUG_CONF
    printf("Mouse is: %d Monitor is: H(%5.1f, %5.1f) V(%u,%u)\n", mouse_type,
      __svgalib_horizsync.min / 1000.0, __svgalib_horizsync.max / 1000.0,
	   __svgalib_vertrefresh.min, __svgalib_vertrefresh.max);
    printf("Mouse device is: %s",mouse_device);
#endif
}

int vga_getmousetype(void)
{
    readconfigfile();
    return mouse_type | mouse_modem_ctl;
}

int vga_getmonitortype(void)
{				/* obsolete */
    int i;
    readconfigfile();
    for (i = 1; i <= MON1024_72; i++)
	if (__svgalib_horizsync.max < __svgalib_maxhsync[i])
	    return i - 1;

    return MON1024_72;
}

void vga_setmousesupport(int s)
{
    mouse_support = s;
}

#ifndef BACKGROUND

void vga_lockvc(void)
{
    lock_count++;
    if (flip)
	__svgalib_waitvtactive();
}

void vga_unlockvc(void)
{
    if (--lock_count <= 0) {
	lock_count = 0;
	if (release_flag) {
	    release_flag = 0;
	    __svgalib_releasevt_signal(SVGALIB_RELEASE_SIG);
	}
    }
}

void vga_runinbackground(int stat, ...)
{
    va_list params;
    
    va_start(params,stat);
    
    switch (stat) {
    case VGA_GOTOBACK:
	__svgalib_go_to_background = va_arg(params, void *);
	break;
    case VGA_COMEFROMBACK:
	__svgalib_come_from_background = va_arg(params, void *);
	break;
    default:
        __svgalib_runinbackground = stat;
        break;
    };
}

#endif

int vga_runinbackground_version(void)

/*  Program can check, if it is safe to in background. */

{
 int version=0;
 
#ifdef BACKGROUND
#if BACKGROUND == 1
 version=1;
#endif
#if BACKGROUND == 2
 version=2;
#endif
#endif
 return(version);
}

#ifdef BACKGROUND

unsigned char *__svgalib_give_graph_red(void)

{
 return(graph_red);
}

unsigned char *__svgalib_give_graph_green(void)

{
 return(graph_green);
}

unsigned char *__svgalib_give_graph_blue(void)

{
 return(graph_blue);
}

#endif

int vga_oktowrite(void)
{
    if(__svgalib_secondary)return 1;
    return __svgalib_oktowrite;
}

void vga_chipset_setregs(unsigned char regs[])
{
    chipset_setregs(regs, TEXT); /* Why TEXT? Can't think of smthg else*/
}

void vga_chipset_saveregs(unsigned char regs[])
{
    chipset_saveregs(regs);
}

int vga_simple_init(void)
{
    __svgalib_simple = 1;
    __svgalib_novccontrol = 1;
    __svgalib_driver_report = 0;
    readconfigfile();
    vga_hasmode(TEXT);
    if(__svgalib_mmio_size)
                  MMIO_POINTER=mmap((caddr_t) 0,
				   __svgalib_mmio_size,
				   PROT_READ | PROT_WRITE,
				   MAP_SHARED,
				   __svgalib_mem_fd,
                                   (off_t) __svgalib_mmio_base
                  ); else MMIO_POINTER=NULL;
    close(__svgalib_mem_fd);
    return 0;
}

int vga_initf(int flags) {
    if(flags&1)
        __svgalib_novccontrol=2;
    if(flags&2)
        __svgalib_secondary=2;
    return vga_init();
}


int vga_init(void)
{
    int retval = 0;

#if 0
    __svgalib_open_devconsole();
    if (__svgalib_tty_fd < 0) {
	/* Return with error code. */
	/* Since the configuration file hasnt been read yet, security will
	   only be set to 1 if it has been compiled into the program */
	retval = -1;
    } else {
	readconfigfile();
	vga_hasmode(TEXT); /* Force driver message and initialization. */
    }
#else
    readconfigfile();
    vga_hasmode(TEXT);
    if(__svgalib_driver_report) {
        printf("svgalib %s\n", versionstr);
    }    
    if(!__svgalib_novccontrol)__svgalib_open_devconsole();
#endif

if(B8000_MEM_POINTER==NULL){
    if(__svgalib_banked_mem_base==0)__svgalib_banked_mem_base=0xa0000;
    if(__svgalib_banked_mem_size==0)__svgalib_banked_mem_size=0x10000;
                  BANKED_MEM_POINTER=mmap((caddr_t) 0,
			                  __svgalib_banked_mem_size,
				          PROT_READ | PROT_WRITE,
				          MAP_SHARED,
				          __svgalib_mem_fd,
				          __svgalib_banked_mem_base
                           
                  );
    if(__svgalib_linear_mem_size) {
                  LINEAR_MEM_POINTER=mmap((caddr_t) 0,
				          __svgalib_linear_mem_size,
			                  PROT_READ | PROT_WRITE,
                                          MAP_SHARED,
				          __svgalib_mem_fd,
                                          (off_t) __svgalib_linear_mem_base
                  );
    };/* else LINEAR_MEM_POINTER=NULL;*/

    if(__svgalib_mmio_size)
                  MMIO_POINTER=mmap((caddr_t) 0,
				   __svgalib_mmio_size,
				   PROT_READ | PROT_WRITE,
				   MAP_SHARED,
				   __svgalib_mem_fd,
                                   (off_t) __svgalib_mmio_base
                  ); else MMIO_POINTER=NULL;

    B8000_MEM_POINTER=mmap((caddr_t) 0,
       			    32768,
			    PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            __svgalib_mem_fd,
                            (off_t) 0xb8000);
};
   
    close(__svgalib_mem_fd);

#ifdef DEBUG
	    printf("svgalib: Opening mouse (type = %x).\n", mouse_type | mouse_modem_ctl);
#endif
	    if (mouse_init(mouse_device, mouse_type | mouse_modem_ctl, MOUSE_DEFAULTSAMPLERATE))
		printf("svgalib: Failed to initialize mouse.\n");
	    else
		mouse_open = 1;

    /* Michael: I assume this is a misunderstanding, when svgalib was developed,
       there were no saved uids, thus setting effective uid sufficed... */
    if ( __svgalib_security_revokeallprivs == 1 ) {
	setuid(getuid());  
	setgid(getgid());
    }
    seteuid(getuid());
    setegid(getgid());
    return retval;
}

int vga_addtiming( int pixelClock,
   		   int HDisplay,	
                   int HSyncStart,
                   int HSyncEnd,
                   int HTotal,
                   int VDisplay,
                   int VSyncStart,
                   int VSyncEnd,
                   int VTotal,
                   int flags) {

   MonitorModeTiming mmt;

   mmt.pixelClock=pixelClock;
   mmt.HDisplay=HDisplay;
   mmt.HSyncStart=HSyncStart;
   mmt.HSyncEnd=HSyncEnd;
   mmt.HTotal=HTotal;
   mmt.VDisplay=VDisplay;
   mmt.VSyncStart=VSyncStart;
   mmt.VSyncEnd=VSyncEnd;
   mmt.VTotal=VTotal;
   mmt.flags=flags;
   
   __svgalib_addusertiming(&mmt);

   return 1;

};

int vga_addmode(int xdim, int ydim, int cols, 
                          int xbytes, int bytespp)
{
   int i;

   i=__svgalib_addmode(xdim, ydim, cols, xbytes, bytespp);

   return i;
};


#ifdef __alpha__

#define vuip	volatile unsigned int *

extern void sethae(unsigned long hae);

static struct hae hae;

int iopl(int level)
{
    return 0;
}

unsigned long vga_readb(unsigned long base, unsigned long off)
{
    unsigned long result, shift;
#if !defined(CONFIG_ALPHA_JENSEN)
    unsigned long msb;
#endif

    shift = (off & 0x3) * 8;
#if !defined(CONFIG_ALPHA_JENSEN)
    if (off >= (1UL << 24)) {
	msb = off & 0xf8000000;
	off -= msb;
	if (msb && msb != hae.cache) {
	    sethae(msb);
	}
    }
#endif
    result = *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_BYTE);
    result >>= shift;
    return 0xffUL & result;
}

unsigned long vga_readw(unsigned long base, unsigned long off)
{
    unsigned long result, shift;
#if !defined(CONFIG_ALPHA_JENSEN)
    unsigned long msb;
#endif

    shift = (off & 0x3) * 8;
#if !defined(CONFIG_ALPHA_JENSEN)
    if (off >= (1UL << 24)) {
	msb = off & 0xf8000000;
	off -= msb;
	if (msb && msb != hae.cache) {
	    sethae(msb);
	}
    }
#endif
    result = *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_WORD);
    result >>= shift;
    return 0xffffUL & result;
}

#if defined(CONFIG_ALPHA_JENSEN)
unsigned long vga_readl(unsigned long base, unsigned long off)
{
    unsigned long result;
    result = *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_LONG);
    return 0xffffffffUL & result;
}
#endif

void vga_writeb(unsigned char b, unsigned long base, unsigned long off)
{
#if !defined(CONFIG_ALPHA_JENSEN)
    unsigned long msb;
    unsigned int w;

    if (off >= (1UL << 24)) {
	msb = off & 0xf8000000;
	off -= msb;
	if (msb && msb != hae.cache) {
	    sethae(msb);
	}
    }
  asm("insbl %2,%1,%0": "r="(w):"ri"(off & 0x3), "r"(b));
    *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_BYTE) = w;
#else
    *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_BYTE) = b * 0x01010101;
#endif
}

void vga_writew(unsigned short b, unsigned long base, unsigned long off)
{
#if !defined(CONFIG_ALPHA_JENSEN)
    unsigned long msb;
    unsigned int w;

    if (off >= (1UL << 24)) {
	msb = off & 0xf8000000;
	off -= msb;
	if (msb && msb != hae.cache) {
	    sethae(msb);
	}
    }
  asm("inswl %2,%1,%0": "r="(w):"ri"(off & 0x3), "r"(b));
    *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_WORD) = w;
#else
    *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_WORD) = b * 0x00010001;
#endif
}

#if defined(CONFIG_ALPHA_JENSEN)
void vga_writel(unsigned long b, unsigned long base, unsigned long off)
{
    *(vuip) ((off << MEM_SHIFT) + base + MEM_TYPE_LONG) = b;
}

#endif

#endif
