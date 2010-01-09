/*
3dfx Voodoo Banshee driver 
*/


#include <stdlib.h>
#include <stdio.h>		/* for printf */
#include <string.h>		/* for memset */
#include <unistd.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"


/* New style driver interface. */
#include "timing.h"
#include "vgaregs.h"
#include "interface.h"
#include "accel.h"
#include "vgapci.h"

#define BANSHEEREG_SAVE(i) (VGA_TOTAL_REGS+i)
#define BANSHEE_TOTAL_REGS (VGA_TOTAL_REGS + 2 + 40)

static int banshee_init(int, int, int);
static void banshee_unlock(void);
static void banshee_lock(void);

void __svgalib_bansheeaccel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int banshee_memory,banshee_chiptype;
static int banshee_is_linear, banshee_linear_base, banshee_io_base;

static CardSpecs *cardspecs;

static void banshee_setpage(int page)
{
   page<<=1;
   outl(banshee_io_base+0x2c,(inl(banshee_io_base+0x2c)&0xfff00000)|(page)|(page<<10));
}

static int __svgalib_banshee_inlinearmode(void)
{
return banshee_is_linear;
}

/* Fill in chipset specific mode information */

static void banshee_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = banshee_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 4088;
    modeinfo->startaddressrange = banshee_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(banshee_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_banshee_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

typedef struct {
   unsigned int pllCtrl0, pllCtrl1, dacMode, dacAddr,
      		vidProcCfg, vidScreenSize, vgaInit0,
                vgaInit1, vidDesktopStartAddr,vidDesktopOverlayStride;
} *HWRecPtr;

static int banshee_saveregs(unsigned char regs[])
{ 
  HWRecPtr save;

  banshee_unlock();		/* May be locked again by other programs (e.g. X) */
  
  save=(HWRecPtr)(regs+62);
  
  regs[BANSHEEREG_SAVE(0)]=__svgalib_inCR(0x1a);
  regs[BANSHEEREG_SAVE(1)]=__svgalib_inCR(0x1b);
  save->pllCtrl0=inl(banshee_io_base+0x40);
  save->pllCtrl1=inl(banshee_io_base+0x44);
  save->dacMode=inl(banshee_io_base+0x4c);
  save->dacAddr=inl(banshee_io_base+0x50);
  save->vidProcCfg=inl(banshee_io_base+0x5c);
  save->vidScreenSize=inl(banshee_io_base+0x98);
  save->vgaInit0=inl(banshee_io_base+0x28);
  save->vgaInit1=inl(banshee_io_base+0x2c);
  save->vidDesktopStartAddr=inl(banshee_io_base+0xe4);
  save->vidDesktopOverlayStride=inl(banshee_io_base+0xe8);
  
  return BANSHEE_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void banshee_setregs(const unsigned char regs[], int mode)
{  
    HWRecPtr restore;

    banshee_unlock();		/* May be locked again by other programs (eg. X) */
  
    restore=(HWRecPtr)(regs+62);
  
    __svgalib_outCR(0x1a,regs[BANSHEEREG_SAVE(0)]);
    __svgalib_outCR(0x1b,regs[BANSHEEREG_SAVE(1)]);
    outl(banshee_io_base+0x40,restore->pllCtrl0);
    outl(banshee_io_base+0x44,restore->pllCtrl1);
    outl(banshee_io_base+0x4c,restore->dacMode);
    outl(banshee_io_base+0x50,restore->dacAddr);
    outl(banshee_io_base+0x5c,restore->vidProcCfg);
    outl(banshee_io_base+0x98,restore->vidScreenSize);
    outl(banshee_io_base+0x28,restore->vgaInit0);
    outl(banshee_io_base+0x2c,restore->vgaInit1);
    outl(banshee_io_base+0xe4,restore->vidDesktopStartAddr);
    outl(banshee_io_base+0xe8,restore->vidDesktopOverlayStride);
    outl(banshee_io_base+0x5c,restore->vidProcCfg&0xfffffffe);
    outl(banshee_io_base+0x5c,restore->vidProcCfg|1);
    outl(banshee_io_base+0x5c,restore->vidProcCfg);

}

/* Return nonzero if mode is available */

static int banshee_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (banshee_memory * 1024 < info->ydim * info->xbytes)
	return 0;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    if((modeinfo->bitsPerPixel==16)&&(modeinfo->greenWeight==5)) {
	free(modeinfo);
        return 0;
    }

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	return 0;
    }
    free(modetiming);
    free(modeinfo);

    return SVGADRV;
}

static unsigned comp_lmn(int freq) ;
/* Set a mode */

/* Local, called by banshee_setmode(). */

static void banshee_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ /* long k; */

    int vd,vt,vbs,vbe,ht,hd,hss,hse;

    HWRecPtr banshee_regs;

    banshee_regs=(HWRecPtr)(moderegs+62);
   
    banshee_saveregs(moderegs);

    __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

    hd = (modetiming->CrtcHDisplay>>3)-1;
    hss = (modetiming->CrtcHSyncStart>>3);
    hse = (modetiming->CrtcHSyncEnd>>3);
    ht = (modetiming->CrtcHTotal>>3)-5;
    moderegs[BANSHEEREG_SAVE(0)]=((ht&0x100)>>8) |
       			    ((hd&0x100)>>6) |
                            ((hd&0x100)>>4) |
                            ((ht&0x40)>>1) |
                            ((hss&0x100)>>2) |
                            ((hse&0x20)<<2) ; 

    vd    = modetiming->CrtcVDisplay - 1;
    vt = modetiming->CrtcVTotal - 2;
    vbs  = modetiming->CrtcVSyncStart - 1;
    vbe  = vt; 
    moderegs[BANSHEEREG_SAVE(1)]=((vt & 0x400)>>10) | 
		            ((vd  & 0x400)>>8) |
		            ((vbs & 0x400)>>6) |
		            ((vbe & 0x400)>>4);
/*
    if (modetiming->flags & INTERLACED)
	    moderegs[MXREG_SAVE(3)] |= 0x8;
*/

    banshee_regs->vidProcCfg&=0xf7e30000;
    banshee_regs->vidProcCfg|=0x00000c81;

    banshee_regs->vidScreenSize=modeinfo->width|(modeinfo->height<<12);

    if (modetiming->flags & DOUBLESCAN)
	    banshee_regs->vidProcCfg |= 0x10;

    switch (modeinfo->bitsPerPixel)
    {
	    case 8: 
               	    banshee_regs->vidProcCfg|=0<<18;
		    break;
	    case 15: 
	    case 16:if(modeinfo->greenWeight==5){
                        banshee_regs->vidProcCfg|=1<<18;
                    } else banshee_regs->vidProcCfg|=1<<18;
		    break;
	    case 24: 
               	    banshee_regs->vidProcCfg|=2<<18;
		    break;
	    case 32: 
               	    banshee_regs->vidProcCfg|=3<<18;
		    break;
	    default: 
		    break;
    }
    
    banshee_regs->vgaInit0&=0xfffffffb;
    if(modeinfo->bitsPerPixel!=8){
        banshee_regs->vgaInit0|=4;
    };

    banshee_regs->pllCtrl0=comp_lmn(modetiming->pixelClock);
    moderegs[VGA_MISCOUTPUT]|=0x0c;

    banshee_regs->vidDesktopStartAddr=0;
    banshee_regs->vidDesktopOverlayStride=modeinfo->lineWidth;    

    banshee_regs->vgaInit0=0x1140;
    banshee_regs->vgaInit1=0x00100000;

    moderegs[41]=0;

    if(modeinfo->bitsPerPixel==8){
       moderegs[79]=0;
    };

    banshee_is_linear=0;

return ;

}

static int banshee_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256 /*&& mode != G320x200x256*/)
	|| mode == G720x348x2) {

	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!banshee_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    if (__svgalib_getmodetiming(&modetiming, modeinfo, cardspecs)) {
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(BANSHEE_TOTAL_REGS);

    banshee_initializemode(moderegs, &modetiming, modeinfo, mode);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    banshee_setregs(moderegs, mode);		/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);

    free(modeinfo);


    return 0;
}


/* Unlock chipset-specific registers */

static void banshee_unlock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);
    
    outl(banshee_io_base+0x28,(inl(banshee_io_base+0x28)&0xffffffbf)|(1<<6));
}

static void banshee_lock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);
    
    outl(banshee_io_base+0x28,(inl(banshee_io_base+0x28)&0xffffffbf));
}


/* Indentify chipset, initialize and return non-zero if detected */

static int banshee_test(void)
{
   int found,_ioperm=0;
   unsigned long buf[64];

   if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: banshee: cannot get I/O permissions\n");
        exit(1);
      }
   }
   found=(!__svgalib_pci_find_vendor_vga(0x121a,buf,0))&&
          (((buf[0]>>16)==0x0003)||
          ((buf[0]>>16)==0x0009)||
          ((buf[0]>>16)==0x0005));
    
   if (_ioperm) iopl(0);

   if(found)banshee_init(0,0,0); 
   return found;
}


/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void banshee_setdisplaystart(int address)
{ 
  outw(CRT_IC, ((address>>2) & 0x00FF00) | 0x0C);
  outw(CRT_IC, (((address>>2) & 0x00FF) << 8) | 0x0D);
  outl(banshee_io_base+0xe4,address);

}


/* Set logical scanline length (usually multiple of 8) */

static void banshee_setlogicalwidth(int width)
{   
    int offset = width >> 3;
 
    __svgalib_outCR(0x13,offset&0xff);
    outl(banshee_io_base+0xe8,width);
}

static int banshee_linear(int op, int param)
{
if (op==LINEAR_ENABLE || op==LINEAR_DISABLE){ banshee_is_linear=1-banshee_is_linear; return 0;}
if (op==LINEAR_QUERY_BASE) return banshee_linear_base;
if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
    else return -1;		/* Unknown function. */
}

static int banshee_match_programmable_clock(int clock)
{
return clock ;
}

static int banshee_map_clock(int bpp, int clock)
{
return clock ;
}

static int banshee_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_banshee_driverspecs =
{
    banshee_saveregs,
    banshee_setregs,
    banshee_unlock,
    banshee_lock,
    banshee_test,
    banshee_init,
    banshee_setpage,
    NULL,
    NULL,
    banshee_setmode,
    banshee_modeavailable,
    banshee_setdisplaystart,
    banshee_setlogicalwidth,
    banshee_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    banshee_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int banshee_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    int found=0;
    int _ioperm=0;

    if (force) {
	banshee_memory = par1;
        banshee_chiptype = par2;
    } else {

    };
    
    if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: banshee: cannot get I/O permissions\n");
        exit(1);
      }
    }
     
    found=(!__svgalib_pci_find_vendor_vga(0x121a,buf,0))&&
            (((buf[0]>>16)==0x0003)||
            ((buf[0]>>16)==0x0009)||
            ((buf[0]>>16)==0x0005));
    
/*     if (_ioperm) iopl(0);*/
    if (found){
       banshee_linear_base=buf[5]&0xffffff00;
       banshee_io_base=buf[6]&0xff00;
    };

    if(banshee_memory==0) {
       unsigned int draminit0,draminit1;
       
       draminit0=inl(banshee_io_base+0x18);
       draminit1=inl(banshee_io_base+0x1c);
       if(draminit1&0x40000000) {
          /* SDRAM */
          banshee_memory=16*1024;
       } else {
          /* SGRAM */
          banshee_memory=1024*4*
             (1+((draminit0>>27)&1))* /* SGRAM type - 8MBIT or 16MBIT */
             (1+((draminit0>>26)&1)); /* Number of sgram chips (4 or 8) */
       }
    }
    
    banshee_unlock();
    
    if (__svgalib_driver_report) {
	printf("Using Banshee / Voodoo3 driver, %iKB.\n",banshee_memory);
    }
    
    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = banshee_memory;
    cardspecs->maxPixelClock4bpp = 270000;	
    cardspecs->maxPixelClock8bpp = 270000;	
    cardspecs->maxPixelClock16bpp = 270000;	
    cardspecs->maxPixelClock24bpp = 270000;
    cardspecs->maxPixelClock32bpp = 270000;
    cardspecs->flags = INTERLACE_DIVIDE_VERT | CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 4088;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->mapClock = banshee_map_clock;
    cardspecs->mapHorizontalCrtc = banshee_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=banshee_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_banshee_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=banshee_linear_base;
    __svgalib_linear_mem_size=banshee_memory*0x400;
    return 0;
}

#define REFFREQ 14318.18

static unsigned
comp_lmn(int freq)
{
  int m, n, k, best_m, best_n, best_k, f_cur, best_error;

  best_error=freq;
  best_n=best_m=best_k=0;
  for (n=1; n<256; n++) {
    f_cur=REFFREQ*(n+2);
    if (f_cur<freq) {
      f_cur=f_cur/3;
      if (freq-f_cur<best_error) {
        best_error=freq-f_cur;
        best_n=n;
        best_m=1;
        best_k=0;
        continue;
      }
    }
    for (m=1; m<64; m++) {
      for (k=0; k<4; k++) {
        f_cur=REFFREQ*(n+2)/(m+2)/(1<<k);
        if (abs(f_cur-freq)<best_error) {
          best_error=abs(f_cur-freq);
          best_n=n;
          best_m=m;
          best_k=k;
	}
      }
    }
  }
  return (best_n << 8) | (best_m<<2) | best_k;
}

