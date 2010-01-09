/*
Savage chipset driver 

Written by Matan Ziv-Av (matan@svgalib.org)

Based on XFree 3.3.6 driver by S. Marineau and Tim Roberts.

*/
#include <stdlib.h>
#include <stdio.h>		
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"
#include "timing.h"
#include "vgaregs.h"
#include "interface.h"
#include "accel.h"
#include "vgapci.h"

#define SAVAGEREG_SAVE(i) (VGA_TOTAL_REGS+i)
#define SAVAGE_TOTAL_REGS (VGA_TOTAL_REGS + 64)

typedef struct {     

   unsigned char SR8, SR10, SR11, SR12, SR13, SR15, SR18, SR29; /* SR9-SR1C, ext seq. */
   /*unsigned char SR54, SR55, SR56, SR57;*/
   unsigned char Clock;
//   unsigned char s3DacRegs[0x101];
   unsigned char CR31, CR33, CR34, CR36, CR3A, CR3B, CR3C;
   unsigned char CR40, CR42, CR43, CR45;
   unsigned char CR50, CR51, CR53, CR58, CR5B, CR5D, CR5E;
   unsigned char CR65, CR66, CR67, CR68, CR69, CR6F; /* Video attrib. */
   unsigned char CR86, CR88;
   unsigned char CR90, CR91, CRB0;
   unsigned int  MMPR0, MMPR1, MMPR2, MMPR3;   /* MIU regs */
} vgaS3VRec, *vgaS3VPtr;

#define BSWAP(x) x = (x>>24) | ((x>>8) & 0xff00)| ((x<<8) & 0xff0000)|(x<<24)

static int savage_init(int, int, int);
static void savage_unlock(void);
static void savage_lock(void);

void __svgalib_savageaccel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

enum { UNKNOWN, SAVAGE3D, SAVAGEMX, SAVAGE4, SAVAGEPRO, SAVAGE2000 };

static int savage_memory, savage_chipset;
static int savage_is_linear, savage_linear_base;

static CardSpecs *cardspecs;

static void savage_setpage(int page)
{
    __svgalib_outcrtc(0x6a, page);
}

static int __svgalib_savage_inlinearmode(void)
{
return savage_is_linear;
}

/* Fill in chipset specific mode information */

static void savage_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = savage_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 4088;
    modeinfo->startaddressrange = savage_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(savage_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_savage_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

static int savage_saveregs(unsigned char regs[])
{ 
    unsigned char cr3a, cr66;
    vgaS3VPtr save = (vgaS3VPtr)(regs+VGA_TOTAL_REGS);

    savage_unlock();		

    cr66 = __svgalib_incrtc(0x66);
    __svgalib_outcrtc(0x66, cr66 | 0x80);
    cr3a = __svgalib_incrtc(0x3a);
    __svgalib_outcrtc(0x3a, cr3a | 0x80);

    cr66 = __svgalib_incrtc(0x66);
    __svgalib_outcrtc(0x66, cr66 | 0x80);
    cr3a = __svgalib_incrtc(0x3a);
    __svgalib_outcrtc(0x3a, cr3a | 0x80);

#if 0
   save = (vgaS3VPtr)vgaHWSave((vgaHWPtr)save, sizeof(vgaS3VRec));
#endif

    __svgalib_outcrtc(0x66, cr66);
    __svgalib_outcrtc(0x3a, cr3a);
    __svgalib_outcrtc(0x66, cr66);
    __svgalib_outcrtc(0x3a, cr3a);

   /* First unlock extended sequencer regs */
    save->SR8 = __svgalib_inseq(0x08);
    __svgalib_outseq(0x08, 0x06);

   /* Now we save all the s3 extended regs we need */
    save->CR31 = __svgalib_incrtc(0x31);
    save->CR34 = __svgalib_incrtc(0x34);
    save->CR36 = __svgalib_incrtc(0x36);
    save->CR3A = __svgalib_incrtc(0x3a);
    save->CR40 = __svgalib_incrtc(0x40);
    save->CR42 = __svgalib_incrtc(0x42);
    save->CR45 = __svgalib_incrtc(0x45);
    save->CR50 = __svgalib_incrtc(0x50);
    save->CR51 = __svgalib_incrtc(0x51);
    save->CR53 = __svgalib_incrtc(0x53);
    save->CR58 = __svgalib_incrtc(0x58);
    save->CR66 = __svgalib_incrtc(0x66);
    save->CR67 = __svgalib_incrtc(0x67);
    save->CR68 = __svgalib_incrtc(0x68);
    save->CR69 = __svgalib_incrtc(0x69);
    save->CR6F = __svgalib_incrtc(0x6f);
    save->CR33 = __svgalib_incrtc(0x33);
    save->CR86 = __svgalib_incrtc(0x86);
    save->CR88 = __svgalib_incrtc(0x88);
    save->CR90 = __svgalib_incrtc(0x90);
    save->CR91 = __svgalib_incrtc(0x91);
    save->CRB0 = __svgalib_incrtc(0xb0) | 0x80;
    save->CR3B = __svgalib_incrtc(0x3b);
    save->CR3C = __svgalib_incrtc(0x3c);
    save->CR43 = __svgalib_incrtc(0x43);
    save->CR5D = __svgalib_incrtc(0x5d);
    save->CR5E = __svgalib_incrtc(0x5e);
    save->CR65 = __svgalib_incrtc(0x65);

   /* Save sequencer extended regs for DCLK PLL programming */
    save->SR10 = __svgalib_inseq(0x10);
    save->SR11 = __svgalib_inseq(0x11);
    save->SR12 = __svgalib_inseq(0x12);
    save->SR13 = __svgalib_inseq(0x13);
    save->SR29 = __svgalib_inseq(0x29);
    save->SR15 = __svgalib_inseq(0x15);
    save->SR18 = __svgalib_inseq(0x18);

//    __svgalib_outcrtc(0x53, cr53);
    __svgalib_outcrtc(0x3a, cr3a);
    __svgalib_outcrtc(0x66, cr66);

    return SAVAGE_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void savage_setregs(const unsigned char regs[], int mode)
{  
    int tmp;
    vgaS3VPtr restore = (vgaS3VPtr)(regs+VGA_TOTAL_REGS);
    
    savage_unlock();

#if 0
   /* Are we going to reenable STREAMS in this new mode? */
   s3vPriv.STREAMSRunning = restore->CR67 & 0x0c; 

   /* First reset GE to make sure nothing is going on */
   outb(vgaCRIndex, 0x66);
   if(inb(vgaCRReg) & 0x01) S3SAVGEReset(0,__LINE__,__FILE__);
#endif
   
   /* As per databook, always disable STREAMS before changing modes */
    __svgalib_outcrtc(0x67, __svgalib_incrtc(0x67)&0xf3);

    __svgalib_outcrtc(0x66, restore->CR66);
    __svgalib_outcrtc(0x3a, restore->CR3A);
    __svgalib_outcrtc(0x31, restore->CR31);
    __svgalib_outcrtc(0x58, restore->CR58);
    __svgalib_outseq(0x08, 0x06);
    __svgalib_outseq(0x12, restore->SR12);    
    __svgalib_outseq(0x13, restore->SR13);    
    __svgalib_outseq(0x29, restore->SR29);    
    __svgalib_outseq(0x15, restore->SR15);

#if 0
   /* Restore the standard VGA registers */
   vgaHWRestore((vgaHWPtr)restore);
#endif

    __svgalib_outcrtc(0x53, restore->CR53);
    __svgalib_outcrtc(0x5d, restore->CR5D);
    __svgalib_outcrtc(0x5e, restore->CR5E);
    __svgalib_outcrtc(0x3b, restore->CR3B);
    __svgalib_outcrtc(0x3c, restore->CR3C);
    __svgalib_outcrtc(0x43, restore->CR43);
    __svgalib_outcrtc(0x65, restore->CR65);

   /* Restore the desired video mode with CR67 */

    __svgalib_outcrtc(0x67, 0x50 | (__svgalib_incrtc(0x67)&0xf3));
    usleep(10000);
    __svgalib_outcrtc(0x67, restore->CR67&0xf3);
        
    __svgalib_outcrtc(0x34, restore->CR34);
    __svgalib_outcrtc(0x40, restore->CR40);
    __svgalib_outcrtc(0x42, restore->CR42);
    __svgalib_outcrtc(0x45, restore->CR45);
    __svgalib_outcrtc(0x50, restore->CR50);
    __svgalib_outcrtc(0x51, restore->CR51);
    __svgalib_outcrtc(0x36, restore->CR36);
    __svgalib_outcrtc(0x68, restore->CR68);
    __svgalib_outcrtc(0x69, restore->CR69);
    __svgalib_outcrtc(0x6f, restore->CR6F);
    __svgalib_outcrtc(0x33, restore->CR33);
    __svgalib_outcrtc(0x86, restore->CR86);
    __svgalib_outcrtc(0x88, restore->CR88);
    __svgalib_outcrtc(0x90, restore->CR90);
    __svgalib_outcrtc(0x91, restore->CR91);
    __svgalib_outcrtc(0xb0, restore->CRB0);

    __svgalib_outseq(0x08, 0x06);
   /* Restore extended sequencer regs for MCLK. SR10 == 255 indicates that 
    * we should leave the default SR10 and SR11 values there.
    */

    if (restore->SR10 != 255) {   
        __svgalib_outseq(0x10, restore->SR10);    
        __svgalib_outseq(0x11, restore->SR11);    
    }

   /* Restore extended sequencer regs for DCLK */

    __svgalib_outseq(0x12, restore->SR12);    
    __svgalib_outseq(0x13, restore->SR13);    
    __svgalib_outseq(0x29, restore->SR29);    
    __svgalib_outseq(0x18, restore->SR18);

#if 0
   /* Load new m,n PLL values for DCLK & MCLK */
   outb(0x3c4, 0x15);
   tmp = inb(0x3c5) & ~0x21;

   outb(0x3c5, tmp | 0x03);
   outb(0x3c5, tmp | 0x23);
   outb(0x3c5, tmp | 0x03);
   outb(0x3c5, restore->SR15);
#else
   tmp = __svgalib_inseq(0x15) & ~0x21;
   __svgalib_outseq(0x15, tmp | 0x03);
   __svgalib_outseq(0x15, tmp | 0x23);
   __svgalib_outseq(0x15, tmp | 0x03);
   __svgalib_outseq(0x15, restore->SR15);
#endif

    __svgalib_outseq(0x08, restore->SR8);


   /* Now write out CR67 in full, possibly starting STREAMS */
   
    __svgalib_outcrtc(0x67, 0x50);
    usleep(10000);
    __svgalib_outcrtc(0x67, restore->CR67&0xf3);

    __svgalib_outcrtc(0x53, restore->CR53);
    __svgalib_outcrtc(0x66, restore->CR66);
    __svgalib_outcrtc(0x3a, restore->CR3A);
}


/* Return nonzero if mode is available */

static int savage_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if (IS_IN_STANDARD_VGA_DRIVER(mode))
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    
    if (savage_memory * 1024 < info->ydim * info->xbytes)
	return 0;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    if(modeinfo->bytesPerPixel==3) return 0;

    modetiming = malloc(sizeof(ModeTiming));

    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 0;
    }

    free(modetiming);
    free(modeinfo);
    return SVGADRV;
}

/* Local, called by savage_setmode(). */
#define BASE_FREQ 14.31818

void savageCalcClock(long freq, int min_m, int min_n1, int max_n1, int min_n2, int max_n2, 
		long freq_min, long freq_max, unsigned int *mdiv, unsigned int *ndiv, unsigned int *r)
{
   double ffreq, ffreq_min, ffreq_max;
   double div, diff, best_diff;
   unsigned int m;
   unsigned char n1, n2;
   unsigned char best_n1=16+2, best_n2=2, best_m=125+2;

   ffreq     = freq     / 1000.0 / BASE_FREQ;
   ffreq_min = freq_min / 1000.0 / BASE_FREQ;
   ffreq_max = freq_max / 1000.0 / BASE_FREQ;

   if (ffreq < ffreq_min / (1<<max_n2)) {
      ffreq = ffreq_min / (1<<max_n2);
   }
   if (ffreq > ffreq_max / (1<<min_n2)) {
      ffreq = ffreq_max / (1<<min_n2);
   }

   /* work out suitable timings */

   best_diff = ffreq;
   
   for (n2=min_n2; n2<=max_n2; n2++) {
      for (n1 = min_n1+2; n1 <= max_n1+2; n1++) {
	 m = (int)(ffreq * n1 * (1<<n2) + 0.5) ;
	 if (m < min_m+2 || m > 127+2) 
	    continue;
	 div = (double)(m) / (double)(n1);	 
	 if ((div >= ffreq_min) &&
	     (div <= ffreq_max)) {
	    diff = ffreq - div / (1<<n2);
	    if (diff < 0.0) 
	       diff = -diff;
	    if (diff < best_diff) {
	       best_diff = diff;
	       best_m    = m;
	       best_n1   = n1;
	       best_n2   = n2;
	    }
	 }
      }
   }
   
   *ndiv = best_n1 - 2;
   *r = best_n2;
   *mdiv = best_m - 2;
}


static void savage_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ 
    int i, j, dclk, width, tmp;
    
    vgaS3VPtr new = (vgaS3VPtr)(moderegs+VGA_TOTAL_REGS);
   
    if(modeinfo->bitsPerPixel==16) {
        modetiming->HDisplay *=2;
        modetiming->HSyncStart *=2;
        modetiming->HSyncEnd *=2;
        modetiming->HTotal *=2;
    }

    __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

    tmp = __svgalib_incrtc(0x3a);
    if( 0 /*!OFLG_ISSET(OPTION_PCI_BURST_ON, &vga256InfoRec.options)*/) 
        new->CR3A = tmp | 0x95;      /* ENH 256, no PCI burst! */
    else 
        new->CR3A = (tmp & 0x7f) | 0x15; /* ENH 256, PCI burst */

    new->CR53 &= ~0x08;     /* Enables MMIO */
    new->CR31 = 0x09;     /* Dis. 64k window, en. ENH maps */

    /* Enables S3D graphic engine and PCI disconnects */
    new->CR66 = 0x89; 

/* Now set linear addr. registers */
/* LAW size: always 8 MB for Savage3D */

    new->CR58 = (__svgalib_incrtc(0x58) & 0x80) | 0x13;

    new->CR58=0; /* seems to be 0 for paged mode and 0x13 for linear */
    
/* Now do clock PLL programming. Use the s3gendac function to get m,n */
/* Also determine if we need doubling etc. */

    dclk = modetiming->pixelClock;
    new->CR67 = 0x00;             /* Defaults */
    new->SR15 = 0x03 | 0x80; 
    new->SR18 = 0x00;
    new->CR43 = 0x00;
    new->CR45 = 0x00;
    new->CR65 = 0x00;

    new->CR40 = __svgalib_incrtc(0x40) & 0xfe ;
   
    /* Memory controller registers. Optimize for better graphics engine 
     * performance. These settings are adjusted/overridden below for other bpp/
     * XConfig options.The idea here is to give a longer number of contiguous
     * MCLK's to both refresh and the graphics engine, to diminish the 
     * relative penalty of 3 or 4 mclk's needed to setup memory transfers. 
     */
    new->MMPR0 = 0x010400; /* defaults */
    new->MMPR1 = 0x00;   
    new->MMPR2 = 0x0808;  
    new->MMPR3 = 0x08080810; 


#if 0
   if (OFLG_ISSET(OPTION_FIFO_AGGRESSIVE, &vga256InfoRec.options) || 
      OFLG_ISSET(OPTION_FIFO_MODERATE, &vga256InfoRec.options) ||
      OFLG_ISSET(OPTION_FIFO_CONSERV, &vga256InfoRec.options)) {
         new->MMPR1 = 0x0200;   /* Low P. stream waits before filling */
         new->MMPR2 = 0x1808;   /* Let the FIFO refill itself */
         new->MMPR3 = 0x08081810; /* And let the GE hold the bus for a while */
      }
#endif

   /* And setup here the new value for MCLK. We use the XConfig 
    * option "set_mclk", whose value gets stored in vga256InfoRec.s3MClk.
    * I'm not sure what the maximum "permitted" value should be, probably
    * 100 MHz is more than enough for now.  
    */

    new->SR10 = 255; /* This is a reserved value, so we use as flag */
    new->SR11 = 255; 

    if (modeinfo->colorBits == 8) {
        if (dclk <= 110000) new->CR67 = 0x00; /* 8bpp, 135MHz */
        else new->CR67 = 0x10;                /* 8bpp, 220MHz */
    } else if (modeinfo->colorBits == 15) {
        if (dclk <= 110000) new->CR67 = 0x20; /* 15bpp, 135MHz */
        else new->CR67 = 0x30;                /* 15bpp, 220MHz */
    } else if (modeinfo->colorBits == 16) {
        if (dclk <= 110000) new->CR67 = 0x40; /* 16bpp, 135MHz */
        else new->CR67 = 0x50;                /* 16bpp, 220MHz */
    } else if (modeinfo->colorBits == 24) {
        new->CR67 = 0xd0;                     /* 24bpp, 135MHz */
    }
    {
        unsigned int m, n, r;

        savageCalcClock(dclk, 1, 1, 127, 0, 4, 180000, 360000, &m, &n, &r);
        new->SR12 = (r << 6) | (n & 0x3F);
        new->SR13 = m & 0xFF;
        new->SR29 = (r & 4) | (m & 0x100) >> 5 | (n & 0x40) >> 2;
    }

#if 0
   /* Now adjust the value of the FIFO based upon options specified */
   if(OFLG_ISSET(OPTION_FIFO_MODERATE, &vga256InfoRec.options)) {
      if(modeinfo->colorBits < 24)
         new->MMPR0 -= 0x8000;
      else 
         new->MMPR0 -= 0x4000;
      }
   else if(OFLG_ISSET(OPTION_FIFO_AGGRESSIVE, &vga256InfoRec.options)) {
      if(modeinfo->colorBits < 24)
         new->MMPR0 -= 0xc000;
      else 
         new->MMPR0 -= 0x6000;
      }
#endif

   /* If we have an interlace mode, set the interlace bit. Note that mode
    * vertical timings are already adjusted by the standard VGA code 
    */
    if (modetiming->flags & INTERLACED) {
        new->CR42 = 0x20; /* Set interlace mode */
    } else {
        new->CR42 = 0x00;
    }

    /* Set display fifo */
    new->CR34 = 0x10;  

    /* Now we adjust registers for extended mode timings */
    /* This is taken without change from the accel/s3_virge code */

    i = ((((modetiming->CrtcHTotal >> 3) - 5) & 0x100) >> 8) |
        ((((modetiming->CrtcHDisplay >> 3) - 1) & 0x100) >> 7) |
        ((((modetiming->CrtcHSyncStart >> 3) - 1) & 0x100) >> 6) |
        ((modetiming->CrtcHSyncStart & 0x800) >> 7);

    if ((modetiming->CrtcHSyncEnd >> 3) - (modetiming->CrtcHSyncStart >> 3) > 64)
        i |= 0x08;   /* add another 64 DCLKs to blank pulse width */

    if ((modetiming->CrtcHSyncEnd >> 3) - (modetiming->CrtcHSyncStart >> 3) > 32)
        i |= 0x20;   /* add another 32 DCLKs to hsync pulse width */

    j = (  moderegs[0] + ((i&0x01)<<8)
         + moderegs[4] + ((i&0x10)<<4) + 1) / 2;

    if (j-(moderegs[4] + ((i&0x10)<<4)) < 4) {
        if (moderegs[4] + ((i&0x10)<<4) + 4 <= moderegs[0]+ ((i&0x01)<<8))
            j = moderegs[4] + ((i&0x10)<<4) + 4;
        else
            j = moderegs[0]+ ((i&0x01)<<8) + 1;
    }
    
    new->CR3B = j & 0xFF;
    i |= (j & 0x100) >> 2;
    new->CR3C = (moderegs[0] + ((i&0x01)<<8))/2;

    new->CR5D = i;

    new->CR5E = (((modetiming->CrtcVTotal - 2) & 0x400) >> 10)  |
                (((modetiming->CrtcVDisplay - 1) & 0x400) >> 9) |
                (((modetiming->CrtcVSyncStart) & 0x400) >> 8)   |
                (((modetiming->CrtcVSyncStart) & 0x400) >> 6)   | 0x40;
   
    width = modeinfo->lineWidth >> 3;
    moderegs[19] = 0xFF & width;
    new->CR51 = (0x300 & width) >> 4; /* Extension bits */
   
    /* And finally, select clock source 2 for programmable PLL */
    moderegs[VGA_MISCOUTPUT] |= 0x0c;      

    /* Set frame buffer description */
    if (modeinfo->colorBits <= 8) {
        new->CR50 = 0;
    } else {
        if (modeinfo->colorBits <= 16) {
            new->CR50 = 0x10;
        } else {
            new->CR50 = 0x30;
        }
    }

    if (modeinfo->width == 640)
        new->CR50 |= 0x40;
    else if (modeinfo->width == 800)
        new->CR50 |= 0x80;
    else if (modeinfo->width == 1024);
    else if (modeinfo->width == 1152)
        new->CR50 |= 0x01;
    else if (modeinfo->width == 1280)
        new->CR50 |= 0x41;
    else if (modeinfo->width == 2048 && new->CR31 & 2);
    else if (modeinfo->width == 1600)
        new->CR50 |= 0x81; /* TODO: need to consider bpp=4 */
    else
        new->CR50 |= 0xC1; /* default to use GlobalBD */

    new->CR33 = 0x08;

   /* Now we handle various XConfig memory options and others */

    new->CR36 = __svgalib_incrtc(0x36);
   
#if 0
   if (mode->Private) {
      new->CR67 &= ~1;
      if( 
        (s3vPriv.chip != S3_SAVAGE2000) &&
        (mode->Private[0] & (1 << S3_INVERT_VCLK)) &&
	(mode->Private[S3_INVERT_VCLK])
      )
	 new->CR67 |= 1;

      if (mode->Private[0] & (1 << S3_BLANK_DELAY)) {
	 new->CR65 = (new->CR65 & ~0x38) 
	    | (mode->Private[S3_BLANK_DELAY] & 0x07) << 3;
      }
   }
#endif

    new->CR68 = __svgalib_incrtc(0x68);
    new->CR69 = 0;
    new->CR6F = __svgalib_incrtc(0x6f);
    new->CR86 = __svgalib_incrtc(0x86);
    new->CR88 = __svgalib_incrtc(0x88);
    new->CRB0 = __svgalib_incrtc(0xb0) | 0x80;

    return ;
}


static int savage_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if (IS_IN_STANDARD_VGA_DRIVER(mode)) {
        __svgalib_outcrtc(0x34,0);
	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!savage_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(SAVAGE_TOTAL_REGS);

    savage_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    savage_setregs(moderegs, mode);		/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);


    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void savage_unlock(void)
{

    __svgalib_outcrtc(0x11,__svgalib_incrtc(0x11)&0x7f);
    __svgalib_outcrtc(0x38, 0x48);
    __svgalib_outcrtc(0x39, 0xa5);
    __svgalib_outcrtc(0x40,__svgalib_incrtc(0x40)&0xfe);    
    
}

static void savage_lock(void)
{
}


#define VENDOR_ID 0x5333

/* Indentify chipset, initialize and return non-zero if detected */

static int savage_test(void)
{
    int found;
    int id;
    unsigned long buf[64];
    
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    id=(buf[0]>>16)&0xffff;
    if(found)return 0;
    switch(id) {
        case 0x8a20:
        case 0x8a21:
        case 0x8a22:
        case 0x8a23:
        case 0x8c10:
        case 0x8c12:
        case 0x9102:
            savage_init(0,0,0);
            return 1;
            break;
        default:
            return 0;
    }
}


/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void savage_setdisplaystart(int address)
{ 
  address=address >> 2;
  __svgalib_outcrtc(0x0d,address&0xff);
  __svgalib_outcrtc(0x0c,(address>>8)&0xff);
  __svgalib_outcrtc(0x69,(address>>16)&0xff);
  
}

/* Set logical scanline length (usually multiple of 8) */
/* Cirrus supports multiples of 8, up to 4088 */

static void savage_setlogicalwidth(int width)
{   
    int offset = width >> 3;
 
    __svgalib_outcrtc(0x13,offset&0xff);
}

static int savage_linear(int op, int param)
{
    if (op==LINEAR_ENABLE){
       __svgalib_outcrtc(0x58,0x13);
       savage_is_linear=1; 
       return 0;
    };
    if (op==LINEAR_DISABLE) {
       __svgalib_outcrtc(0x58,0);
       savage_is_linear=0; 
       return 0;
    };
    if (op==LINEAR_QUERY_BASE) return savage_linear_base;
    if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
        else return -1;		/* Unknown function. */
}

static int savage_match_programmable_clock(int clock)
{
return clock ;
}

static int savage_map_clock(int bpp, int clock)
{
return clock ;
}

static int savage_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */
DriverSpecs __svgalib_savage_driverspecs =
{
    savage_saveregs,
    savage_setregs,
    savage_unlock,
    savage_lock,
    savage_test,
    savage_init,
    savage_setpage,
    NULL,
    NULL,
    savage_setmode,
    savage_modeavailable,
    savage_setdisplaystart,
    savage_setlogicalwidth,
    savage_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    savage_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int savage_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    unsigned long savage_mmio_base;
    int found=0, config1;
    int mems[8]={2,4,8,12,16,32,64,2};
    char *chipnames[6] = {"Unknown", "Savage3D", "SavageMX", "Savage4", "SavagePro", "Savage2000"};
    int id;
    
    savage_unlock();
    if (force) {
	savage_memory = par1;
        savage_chipset = par2;
    } else {

    };

    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    savage_linear_base=buf[5]&0xffffff00;
    savage_mmio_base  =buf[4]&0xffffff00;

    config1=__svgalib_incrtc(0x36);

#if 0
    id=__svgalib_incrtc(0x2e) | (__svgalib_incrtc(0x2d)<<8)
#else
    id=(buf[0]>>16)&0xffff;
#endif

    switch(id) {
        case 0x8a20:
        case 0x8a21:
            savage_chipset = SAVAGE3D;
            break;
        case 0x8c10:
        case 0x8c12:
            savage_chipset = SAVAGEMX;
            break;
        case 0x8a22:
        case 0x8a23:
            savage_chipset = SAVAGE4;
            break;
        case 0x9102:
            savage_chipset = SAVAGE2000;
            break;
        default:
            savage_chipset = UNKNOWN;
    }
    
    if(savage_chipset >= SAVAGE4) {
        savage_memory=mems[config1>>5]*1024;
    } else {
        switch(config1>>6) {
            case 0:
                savage_memory=8192;
                break;
            case 0x40:
            case 0x80:
                savage_memory=4096;
                break;
            case 0xC0:
                savage_memory=2048;
                break;
        }
    }

    if (__svgalib_driver_report) {
	printf("Using SAVAGE driver, %iKB. Chipset: %s\n",savage_memory, chipnames[savage_chipset]);
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = savage_memory;
    cardspecs->maxPixelClock4bpp = 0;	
    cardspecs->maxPixelClock8bpp = 250000;	
    cardspecs->maxPixelClock16bpp = 250000;	
    cardspecs->maxPixelClock24bpp = 220000;
    cardspecs->maxPixelClock32bpp = 220000;
    cardspecs->flags = INTERLACE_DIVIDE_VERT | CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 4088;
    cardspecs->nClocks =0;
    cardspecs->mapClock = savage_map_clock;
    cardspecs->mapHorizontalCrtc = savage_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=savage_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_savage_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=savage_linear_base;
    __svgalib_linear_mem_size=savage_memory*0x400;
    return 0;
}
