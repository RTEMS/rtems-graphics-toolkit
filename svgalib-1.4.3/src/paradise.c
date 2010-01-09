/* Western Digital Paradise WD90C31 driver for VGAlib                 */
/* (c) 1998 Petr Kulhavy   <brain@artax.karlin.mff.cuni.cz>           */
/*                                                                    */
/* This driver is absolutely free software. You can redistribute      */
/* and/or it modify without any restrictions. But it's WITHOUT ANY    */
/* WARRANTY.                                                          */

#include <stdio.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"

#include "paradise.regs"

#undef DEBUG

/* static int paradise_chiptype; */
static int paradise_memory;	/* amount of video memory in K */

static int paradise_init(int, int, int);
static void paradise_unlock(void);

/* Mode table */
static ModeTable paradise_modes_512[] =
{				/* 512k, non-interlaced modes */
/* *INDENT-OFF* */
    OneModeEntry(640x480x256),
    OneModeEntry(800x600x16),
    OneModeEntry(800x600x256),
    OneModeEntry(1024x768x16),
    END_OF_MODE_TABLE
/* *INDENT-ON* */
};

/* Interlaced modes suck, I hate them */

static ModeTable *paradise_modes = NULL;

static void nothing(void)
{
}

void inline _outb(unsigned port,unsigned value)
{
#ifdef DEBUG
 printf ("0x%x, 0x%x\n",port,value);
#endif
outb(port,value);
}

/* Fill in chipset specific mode information */

static void paradise_getmodeinfo(int mode, vga_modeinfo * modeinfo)
{
#ifdef DEBUG
 printf("paradise_getmodeinfo\n");
#endif
    switch(modeinfo->colors)
     {
     case 16:			/* 16 colors, 4 planes */
     modeinfo->maxpixels=65536*8;
     modeinfo->startaddressrange = 0x7ffff;
     break;
     default:
     if (modeinfo->bytesperpixel > 0)
 	modeinfo->maxpixels = paradise_memory * 1024 /
 	    modeinfo->bytesperpixel;
     else
 	modeinfo->maxpixels = paradise_memory * 1024;
     modeinfo->startaddressrange = 0x3ffff;
     break;
     }
    modeinfo->maxlogicalwidth = 2040;
    modeinfo->haveblit = 0;
    modeinfo->flags |= HAVE_RWPAGE;
}


/* select the correct register table */
static void setup_registers(void)
{
#ifdef DEBUG
 printf("setup_registers\n");
#endif
    if (paradise_modes == NULL) {
	    paradise_modes = paradise_modes_512;
    }
}


/* Read and store chipset-specific registers */

static int paradise_saveregs(unsigned char regs[])
{
unsigned a,b;

#ifdef DEBUG
 printf("paradise_saveregs\n");
#endif

paradise_unlock();
b=0;
for (a=0x03;a<=0x15;)
 { 
 outb(SEQ_I,a);
 regs[EXT+b]=inb(SEQ_D);
 b++;
 if (a==0x03)a=0x06;
 else if (a==0x07)a=0x10;
 else if (a==0x12)a=0x14;
 else a++;
 }
for (a=0x09;a<=0x0f;a++)
 {
 outb(GRA_I,a);
 regs[EXT+b]=inb(GRA_D);
 b++;
 }  
for (a=0x29;a<=0x3e;)
 {
 outb(CRT_IC,a);
 regs[EXT+b]=inb(CRT_DC);
 b++;
 if (a==0x30)a=0x32;
 else if (a==0x32)a=0x34;
 else if (a==0x34)a=0x3e;
 else a++;
 }  
/* VGA registers, not set by shitty svgalib */
outb(CRT_IC,0x18);
regs[EXT+b+1]=inb(CRT_DC);
outb(ATT_IW,0x20);
__svgalib_delay();
regs[EXT+b+2]=inb(ATT_IW);
return 28;			/* Paradise requires 28 additional registers */
}

static void paradise_unlock(void)
{
unsigned char b;
#ifdef DEBUG
 printf("paradise_unlock\n");
#endif
outw(GRA_I,0x050f);
outw(CRT_IC,0x8529);
outw(SEQ_I,0x4806);
/* let's unlock sync, timing ... */
outb(GRA_I,0xd);
b=inb (GRA_D);
b&=30;
b|=2;
outb(GRA_I,0xd);
outb(GRA_D,b);
outb(GRA_I,0xe);
b=inb(GRA_D);
b&=251;
outb(GRA_I,0xe);
outb(GRA_D,b);
outb(CRT_IC,0x2a);
b=inb(CRT_DC);
b&=248;
outb(CRT_IC,0x2a);
outb(CRT_DC,b);
}

static void paradise_lock(void)
{
#ifdef DEBUG
 printf("paradise_lock\n");
#endif
outw(GRA_I,0x000f);
outw(CRT_IC,0x0029);
outw(SEQ_I,0x0006);
}

/* Set chipset-specific registers */

static void paradise_setregs(const unsigned char regs[], int mode)
{
unsigned a,b;
#ifdef DEBUG
printf("paradise_setregs\n");
#endif

paradise_unlock();

b=0;
for (a=0x03;a<=0x15;)
 { 
 _outb(SEQ_I,a);
 _outb(SEQ_D,regs[EXT+b]);
 b++;
 if (a==0x03)a=0x06;
 else if (a==0x07)a=0x10;
 else if (a==0x12)a=0x14;
 else a++;
 }
for (a=0x09;a<=0x0f;a++)
 {
 _outb(GRA_I,a);
 _outb(GRA_D,regs[EXT+b]);
 b++;
 }  
for (a=0x29;a<=0x3e;)
 {
 _outb(CRT_IC,a);
 _outb(CRT_DC,regs[EXT+b]);
 b++;
 if (a==0x30)a=0x32;
 else if (a==0x32)a=0x34;
 else if (a==0x34)a=0x3e;
 else a++;
 }  
/* VGA registers, not set by shitty svgalib */
_outb(CRT_IC,0x18);
_outb(CRT_DC,regs[EXT+b+1]);
_outb(0x3da,0x100);
_outb(ATT_IW,0x20);
__svgalib_delay();
_outb(ATT_IW,regs[EXT+b+2]);
}


/* Return nonzero if mode is available */

static int paradise_modeavailable(int mode)
{
    const unsigned char *regs;
    struct info *info;
#ifdef DEBUG
 printf("paradise_modeavailable\n");
#endif

    regs = LOOKUPMODE(paradise_modes, mode);
    if (regs == NULL || mode == GPLANE16)
	return __svgalib_vga_driverspecs.modeavailable(mode);
    if (regs == DISABLE_MODE || mode <= TEXT || mode > GLASTMODE)
	return 0;

    info = &__svgalib_infotable[mode];
    if (paradise_memory * 1024 < info->ydim * info->xbytes)
	return 0;

    return SVGADRV;
}

/* Set a mode */

static int paradise_setmode(int mode, int prv_mode)
{
    const unsigned char *regs;

#ifdef DEBUG
 printf("paradise_setmode\n");
#endif

    regs = LOOKUPMODE(paradise_modes, mode);
    if (regs == NULL)
	return (int) (__svgalib_vga_driverspecs.setmode(mode, prv_mode));
    if (!paradise_modeavailable(mode))
	return 1;
    paradise_unlock();
    __svgalib_setregs(regs);
    paradise_setregs(regs, mode);
    return 0;
}


/* Indentify chipset; return non-zero if detected */

static int paradise_test(void)
{
    /* 
     * Check first that we have a Paradise card.
     */
    int a;
    unsigned char old_value;
    unsigned char txt[6];
#ifdef DEBUG
 printf("paradise_test\n");
#endif
    txt[5]=0;
    old_value=inb(CRT_IC);
    for (a=0;a<5;a++)
     {
     outb(CRT_IC,0x31+a);
     txt[a]=inb(CRT_DC);
     }
    if (strcmp(txt,"WD90C"))
    {
	outb(CRT_IC,old_value);
	return 0;
    }
    /* check version */
    outb(CRT_IC, 0x36);
    switch (inb(CRT_DC))
    {
    case 0x32:  /* WD90C2x */
    outb(CRT_IC, 0x37);
    switch (inb(CRT_DC))
     {
     case 0x34:  /* WD90C24 */
     case 0x36:  /* WD90C26 */
     break;
     default:
     return 0; 
     }
    break;
    case 0x33:  /* WD90C3x */
    outb(CRT_IC, 0x37);
    outb(CRT_IC, 0x37);
    switch (inb(CRT_DC))
     {
     case 0x30:  /* WD90C30 */
     case 0x31:  /* WD90C31 */
     case 0x33:  /* WD90C31 */
     break;
     default:
     return 0; 
     }
    break;
    default:
    return 0;
    }

    paradise_init(0, 0, 0);
    return 1;
}


/* Bank switching function - set 64K bank number */

static void paradise_setpage(int page)
{
#ifdef DEBUG
 printf("paradise_setpage\n");
#endif
paradise_unlock();
/* set read-write paging mode */
outb(SEQ_I,0x11);
outb(SEQ_D,inb(SEQ_D)|0x80);
outb(GRA_I,0x0b);
outb(GRA_D,inb(GRA_D)|0x80);
/* set bank number */
outb(GRA_I,0x09);
outb(GRA_D,page<<4);
outb(GRA_I,0x0a);
outb(GRA_D,page<<4);
}


/* Set display start address (not for 16 color modes) */

static void paradise_setdisplaystart(int address)
{unsigned char bits,orig;
#ifdef DEBUG
 printf("paradise_setdisplaystart\n");
#endif
paradise_unlock();
outb(CRT_IC,0x2f);
bits=address>>13;
orig=inb(CRT_DC);
orig^=bits;
orig&=0xe7;
orig^=bits;
outb(CRT_DC,orig);
}


/* Set logical scanline length (usually multiple of 8) */

static void paradise_setlogicalwidth(int width)
{
#ifdef DEBUG
 printf("paradise_setlogicalwidth\n");
#endif
paradise_unlock();
    outw(CRT_IC, 0x13 + (width >> 3) * 256);
}


/* Function table */
DriverSpecs __svgalib_paradise_driverspecs =
{
    paradise_saveregs,
    paradise_setregs,
    paradise_unlock,
    paradise_lock,
    paradise_test,
    paradise_init,
    paradise_setpage,
    (void (*)(int)) nothing,	/* __svgalib_setrdpage */
    (void (*)(int)) nothing,	/* __svgalib_setwrpage */
    paradise_setmode,
    paradise_modeavailable,
    paradise_setdisplaystart,
    paradise_setlogicalwidth,
    paradise_getmodeinfo,  /*?*/
    0,				/* bitblt */
    0,				/* imageblt */
    0,				/* fillblt */
    0,				/* hlinelistblt */
    0,				/* bltwait */
    0,				/* extset */
    0,
    0,				/* linear */
    NULL			/* accelspecs */
};


/* Initialize chipset (called after detection) */

static int paradise_init(int force, int par1, int par2)
{
#ifdef DEBUG
 printf("paradise_init\n");
#endif
    if (force) {
#ifdef DEBUG
 printf("forcing memory to %dkB\n",par1);
#endif
	paradise_memory = par1;
    } else {
	outb(GRA_I,0x0b);
	paradise_memory=(inb(GRA_D)>>6);
	switch (paradise_memory)
	 {
	 case 0:
	 case 1:
	 paradise_memory=256;
	 break;
	 case 2:
	 paradise_memory=512;
	 break;
	 case 3:
	 paradise_memory=1024;
	 break;
	 }
    }

    if (__svgalib_driver_report) {
	printf("Using WD90C31 Paradise driver (%dK non-interlaced).\n",
	       paradise_memory);
    }
    __svgalib_driverspecs = &__svgalib_paradise_driverspecs;
    setup_registers(); 
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    return 0;
}
