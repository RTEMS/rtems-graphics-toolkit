/*
Riva 128 driver - Matan Ziv-Av zivav@cs.bgu.ac.il
please report problems to me,

This driver is based on the XFREE86 nv3 driver, developed by
David J. Mckay.

I used the file cirrus.c in this directory as a skeleton.

there are still the following problems:
  * no 24bit modes. (maybe the hardware does not support)
  * pageflipping (in threeDKit) does not work. 
  * no acceleration (is there a program that uses it anyway?).
*/


#include <stdlib.h>
#include <stdio.h>		/* for printf */
#include <string.h>		/* for memset */
#include <sys/mman.h>		
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"


/* New style driver interface. */
#include "timing.h"
#include "vgaregs.h"
#include "interface.h"
#include "accel.h"
#include "nv3ref.h"
#include "nvreg.h"
#include "vgapci.h"

#define NV3REG_SAVE(i) (VGA_TOTAL_REGS+i)
#define NV3_TOTAL_REGS (VGA_TOTAL_REGS + 24 + 33 )
#define M_MIN 7
#define M_MAX 13

#define P_MIN 0
#define P_MAX 7 /* Not sure about this. Could be 4 */

#define SetBitField(value,from,to) SetBF(to,GetBF(value,from))
#define SetBit(n) (1<<(n))
#define Set8Bits(value) ((value)&0xff)

static volatile unsigned  *nvPFBPort;
static volatile unsigned  *nvPRAMDACPort;
static volatile unsigned  *nvPEXTDEVPort;

static int nv3_init(int, int, int);
static void nv3_unlock(void);

void __svgalib_nv3accel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int nv3_memory,nv3_chiptype;
static int nv3_is_linear;
static CardSpecs *cardspecs;
static int PLL_INPUT_FREQ;

static unsigned long MMIOBASE=0, LINEARBASE;

enum {
   Riva128 = 0, RivaTNT, GEFORCE
};

static void nv3_setpage(int page)
{
__svgalib_outcrtc(0x1d,page << 1);
__svgalib_outcrtc(0x1e,page << 1);
}

static int __svgalib_nv3_inlinearmode(void)
{
return nv3_is_linear;
}

/* Fill in chipset specific mode information */

static void nv3_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{
   
   if(modeinfo->colors==16)return;
   
   modeinfo->maxpixels = nv3_memory*1024/modeinfo->bytesperpixel;
   modeinfo->maxlogicalwidth = 4088;
   modeinfo->startaddressrange = nv3_memory * 1024 - 1;
   modeinfo->haveblit = 0;
   modeinfo->flags &= ~HAVE_RWPAGE;

   if (modeinfo->bytesperpixel >= 1) {
	modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_nv3_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
   }
}

/* Read and save chipset-specific registers */

static int nv3_saveregs(unsigned char regs[])
{ unsigned long k;
    int i;
    nv3_unlock();		/* May be locked again by other programs (e.g. X) */

    regs[NV3REG_SAVE(0)] = PCRTC_Read(REPAINT0);
    regs[NV3REG_SAVE(1)] = PCRTC_Read(REPAINT1);
    regs[NV3REG_SAVE(2)] = PCRTC_Read(EXTRA);
    regs[NV3REG_SAVE(3)] = PCRTC_Read(PIXEL);
    regs[NV3REG_SAVE(4)] = PCRTC_Read(HORIZ_EXTRA);
    regs[NV3REG_SAVE(5)] = PCRTC_Read(FIFO_CONTROL);
    regs[NV3REG_SAVE(6)] = PCRTC_Read(FIFO);
    regs[NV3REG_SAVE(7)] = 0;

   k = PFB_Read(CONFIG_0) ;
   regs[NV3REG_SAVE(8)] = k&0xff;
   regs[NV3REG_SAVE(9)] = (k >> 8)&0xff;
   regs[NV3REG_SAVE(10)] = (k >> 16) & 0xff ;
   regs[NV3REG_SAVE(11)] = ( k >> 24 ) & 0xff ;
   k =  PRAMDAC_Read(VPLL_COEFF);
   regs[NV3REG_SAVE(12)] = k&0xff;
   regs[NV3REG_SAVE(13)] = (k >> 8)&0xff;
   regs[NV3REG_SAVE(14)] = (k >> 16) & 0xff ;
   regs[NV3REG_SAVE(15)] = ( k >> 24 ) & 0xff ;
   k =  PRAMDAC_Read(PLL_COEFF_SELECT);
   regs[NV3REG_SAVE(16)] = k&0xff;
   regs[NV3REG_SAVE(17)] = (k >> 8)&0xff;
   regs[NV3REG_SAVE(18)] = (k >> 16) & 0xff ;
   regs[NV3REG_SAVE(19)] = ( k >> 24 ) & 0xff ;
   k =  PRAMDAC_Read(GENERAL_CONTROL);
   regs[NV3REG_SAVE(20)] = k&0xff;
   regs[NV3REG_SAVE(21)] = (k >> 8)&0xff;
   regs[NV3REG_SAVE(22)] = (k >> 16) & 0xff ;
   regs[NV3REG_SAVE(23)] = ( k >> 24 ) & 0xff ;
   
   for(i=0x18;i<0x38;i++)regs[84+i-0x18]=__svgalib_incrtc(i);
   
   return NV3_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void nv3_setregs(const unsigned char regs[], int mode)
{   unsigned long k ; 
    nv3_unlock();		/* May be locked again by other programs (eg. X) */

   PCRTC_Write(REPAINT0,regs[NV3REG_SAVE(0)]);
   PCRTC_Write(REPAINT1,regs[NV3REG_SAVE(1)]);
   PCRTC_Write(EXTRA,regs[NV3REG_SAVE(2)]);
   PCRTC_Write(PIXEL,regs[NV3REG_SAVE(3)]);
   PCRTC_Write(HORIZ_EXTRA,regs[NV3REG_SAVE(4)]); 
   PCRTC_Write(FIFO_CONTROL,regs[NV3REG_SAVE(5)]); 
/*  PCRTC_Write(FIFO,regs[NV3REG_SAVE(6)]); */

   __svgalib_outcrtc(0x1c,regs[88]); /* this enables banking at 0xa0000 */
   __svgalib_outcrtc(0x1d,regs[89]);
   __svgalib_outcrtc(0x1e,regs[90]);
   __svgalib_outcrtc(0x31,0); /* disable cursor */

   k=regs[NV3REG_SAVE(8)]+(regs[NV3REG_SAVE(9)] << 8)+(regs[NV3REG_SAVE(10)] << 16)+(regs[NV3REG_SAVE(11)] << 24); 
          PFB_Write(CONFIG_0,k);

   k=regs[NV3REG_SAVE(12)]+(regs[NV3REG_SAVE(13)] << 8)+(regs[NV3REG_SAVE(14)] << 16)+(regs[NV3REG_SAVE(15)] << 24);
          PRAMDAC_Write(VPLL_COEFF,k);
   k=regs[NV3REG_SAVE(16)]+(regs[NV3REG_SAVE(17)] << 8)+(regs[NV3REG_SAVE(18)] << 16)+(regs[NV3REG_SAVE(19)] << 24);
          PRAMDAC_Write(PLL_COEFF_SELECT,k);
   k=regs[NV3REG_SAVE(20)]+(regs[NV3REG_SAVE(21)] << 8)+(regs[NV3REG_SAVE(22)] << 16)+(regs[NV3REG_SAVE(23)] << 24);
          PRAMDAC_Write(GENERAL_CONTROL,k);
}


/* Return nonzero if mode is available */

static int nv3_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;
    
    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (nv3_memory * 1024 < info->ydim * info->xbytes)
	return 0;
    
    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);
   
    if(modeinfo->bitsPerPixel==24) {
	free(modeinfo);
        return 0;
    }
    
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


static int NV3ClockSelect(float clockIn,float *clockOut,int *mOut,
                                        int *nOut,int *pOut);

static int CalculateCRTC(ModeTiming *mode, ModeInfo *modeinfo, unsigned char *moderegs)
{
  int bpp=modeinfo->bitsPerPixel/8,
      horizDisplay = (mode->CrtcHDisplay/8) - 1,
      horizStart = (mode->CrtcHSyncStart/8) - 1,
      horizEnd = (mode->CrtcHSyncEnd/8) - 1,
      horizTotal = (mode->CrtcHTotal/8)	- 1,
      vertDisplay = mode->CrtcVDisplay - 1,
      vertStart = mode->CrtcVSyncStart	- 1,
      vertEnd = mode->CrtcVSyncEnd - 1,
      vertTotal = mode->CrtcVTotal - 2;
        
  /* Calculate correct value for offset register */
  moderegs[0x13]=((modeinfo->width/8)*bpp)&0xff;
  /* Extra bits for CRTC offset register */
  moderegs[NV3REG_SAVE(0)]=
    SetBitField((modeinfo->width/8)*bpp,10:8,7:5);

  /* The NV3 manuals states that for native modes, there should be no 
   * borders. This code should also be tidied up to use symbolic names
   */     
  moderegs[0x0]=Set8Bits(horizTotal - 4);
  moderegs[0x1]=Set8Bits(horizDisplay);
  moderegs[0x2]=Set8Bits(horizDisplay);
  moderegs[0x3]=SetBitField(horizTotal,4:0,4:0) | SetBit(7);
  moderegs[0x4]=Set8Bits(horizStart);
  moderegs[0x5]=SetBitField(horizTotal,5:5,7:7)|
                     SetBitField(horizEnd,4:0,4:0);
  moderegs[0x6]=SetBitField(vertTotal,7:0,7:0);

  moderegs[0x7]=SetBitField(vertTotal,8:8,0:0)|
		     SetBitField(vertDisplay,8:8,1:1)|
		     SetBitField(vertStart,8:8,2:2)|
		     SetBitField(vertDisplay,8:8,3:3)|
		     SetBit(4)|
		     SetBitField(vertTotal,9:9,5:5)|
		     SetBitField(vertDisplay,9:9,6:6)|
		     SetBitField(vertStart,9:9,7:7);

  moderegs[0x9]= SetBitField(vertDisplay,9:9,5:5) | SetBit(6);
  moderegs[0x10]= Set8Bits(vertStart);
  moderegs[0x11]= SetBitField(vertEnd,3:0,3:0) | SetBit(5);
  moderegs[0x12]= Set8Bits(vertDisplay);
  moderegs[0x15]= Set8Bits(vertDisplay);
  moderegs[0x16]= Set8Bits(vertTotal + 1);

  moderegs[NV3REG_SAVE(2)]= SetBitField(horizTotal,6:6,4:4) |
                             SetBitField(vertDisplay,10:10,3:3) |
                             SetBitField(vertStart,10:10,2:2) |
                             SetBitField(vertDisplay,10:10,1:1) |
                             SetBitField(vertTotal,10:10,0:0);

  if(mode->flags & DOUBLESCAN) moderegs[0x9]|=0x80;
 
  /* I think this should be SetBitField(horizTotal,8:8,0:0), but this
   * doesn't work apparently. Why 260 ? 256 would make sense.
   */
  moderegs[NV3REG_SAVE(4)]= (horizTotal < 260 ? 0 : 1);
  return 1;
}

/* Set a mode */

/* Local, called by nv3_setmode(). */

static void nv3_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ long k;
  int m,n,p;
  float clockIn=(float)modetiming->pixelClock;
  float clockOut;
  int pixelDepth;
  
   nv3_saveregs(moderegs);
   __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);
   if(!NV3ClockSelect(clockIn,&clockOut,&m,&n,&p)) {return ;}
   k=PRAMDAC_Val(VPLL_COEFF_NDIV,n) | PRAMDAC_Val(VPLL_COEFF_MDIV,m) | PRAMDAC_Val(VPLL_COEFF_PDIV,p);
   moderegs[NV3REG_SAVE(12)] = k&0xff;
   moderegs[NV3REG_SAVE(13)] = (k >> 8)&0xff;
   moderegs[NV3REG_SAVE(14)] = (k >> 16) & 0xff ;
   moderegs[NV3REG_SAVE(15)] = ( k >> 24 ) & 0xff ;

  CalculateCRTC(modetiming,modeinfo,moderegs);

  moderegs[NV3REG_SAVE(1)]=
    PCRTC_Val(REPAINT1_LARGE_SCREEN,modetiming->CrtcHDisplay<1280) |
    PCRTC_Def(REPAINT1_PALETTE_WIDTH,6BITS);

/* The new xfree driver (from nVidia) calculates those in some
   twisted way, but I leave it for now */
  moderegs[NV3REG_SAVE(5)]=0x83;
  moderegs[NV3REG_SAVE(6)]=0x22;

  /* PixelFormat controls how many bits per pixel. 
   * There is another register in the 
   * DAC which controls if mode is 5:5:5 or 5:6:5
   */
  pixelDepth=(modeinfo->bitsPerPixel+1)/8;
  if(pixelDepth>3) pixelDepth=3;
  moderegs[NV3REG_SAVE(3)]=pixelDepth;

  k=
     PRAMDAC_Def(GENERAL_CONTROL_IDC_MODE,GAMMA)|
     PRAMDAC_Val(GENERAL_CONTROL_565_MODE,modeinfo->greenWeight==6)|
     PRAMDAC_Def(GENERAL_CONTROL_TERMINATION,37OHM)|
     PRAMDAC_Def(GENERAL_CONTROL_BPC,6BITS)|  
     PRAMDAC_Def(GENERAL_CONTROL_VGA_STATE,SEL); /* Not sure about this */

   if(modeinfo->bitsPerPixel>8)k|=PRAMDAC_Def(GENERAL_CONTROL_BPC,8BITS);

   moderegs[NV3REG_SAVE(20)] = k&0xff;
   moderegs[NV3REG_SAVE(21)] = (k >> 8)&0xff;
   moderegs[NV3REG_SAVE(22)] = (k >> 16) & 0xff ;
   moderegs[NV3REG_SAVE(23)] = ( k >> 24 ) & 0xff ;

   switch(nv3_chiptype){
      case Riva128:
          k=PRAMDAC_Def(PLL_COEFF_SELECT_MPLL_SOURCE,PROG)|
                            PRAMDAC_Def(PLL_COEFF_SELECT_VPLL_SOURCE,PROG)|
                            PRAMDAC_Def(PLL_COEFF_SELECT_VCLK_RATIO,DB2);
          break;
      case RivaTNT:
      case GEFORCE:
          k=0x10000700;
          break;
   };
   
   moderegs[NV3REG_SAVE(16)] = k&0xff;
   moderegs[NV3REG_SAVE(17)] = (k >> 8)&0xff;
   moderegs[NV3REG_SAVE(18)] = (k >> 16) & 0xff ;
   moderegs[NV3REG_SAVE(19)] = ( k >> 24 ) & 0xff ;

  /* Disable Tetris tiling for now. This looks completely mad but could 
   * give some significant performance gains. Will perhaps experiment 
   * later on with this stuff!  
   */
   if(nv3_chiptype<GEFORCE) {
       k=
           PFB_Val(CONFIG_0_RESOLUTION,((modeinfo->lineWidth+31)/32))|
           PFB_Val(CONFIG_0_PIXEL_DEPTH,pixelDepth)|
           PFB_Def(CONFIG_0_TILING,ENABLED); 
       moderegs[NV3REG_SAVE(8)] = k&0xff;
       moderegs[NV3REG_SAVE(9)] = (k >> 8)&0xff;
       moderegs[NV3REG_SAVE(10)] = (k >> 16) & 0xff ;
       moderegs[NV3REG_SAVE(11)] = ( k >> 24 ) & 0xff ;
   }
  moderegs[88]=28;

  if(modeinfo->greenWeight==5){moderegs[NV3REG_SAVE(21)]&=0xef;}; 

  nv3_is_linear=0;

return ;
}


static int nv3_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;
    int i;
    if ((mode < G640x480x256 /*&& mode != G320x200x256*/)
	|| mode == G720x348x2) {

	unsigned int k;

        if(nv3_chiptype==Riva128)
            PRAMDAC_Write(PLL_COEFF_SELECT,0x00000100);
            else PRAMDAC_Write(PLL_COEFF_SELECT,0x00000500);

        __svgalib_outcrtc(NV_PCRTC_REPAINT0,0);
        __svgalib_outcrtc(NV_PCRTC_REPAINT1,0x3d);
        __svgalib_outcrtc(NV_PCRTC_EXTRA,0);
        __svgalib_outcrtc(NV_PCRTC_PIXEL,0);
        __svgalib_outcrtc(NV_PCRTC_HORIZ_EXTRA,0);
        __svgalib_outcrtc(NV_PCRTC_FIFO_CONTROL,0x83);
        __svgalib_outcrtc(0x1c,0x18);
        __svgalib_outcrtc(0x1d,0);
        __svgalib_outcrtc(0x1e,0);
        __svgalib_outcrtc(0x30,0);
        __svgalib_outcrtc(0x31,0);
        k =  PRAMDAC_Read(GENERAL_CONTROL);
        k &= ~0x00100000;
        PRAMDAC_Write(GENERAL_CONTROL,k);

	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }

    if (!nv3_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(NV3_TOTAL_REGS);

    nv3_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    nv3_setregs(moderegs, mode);	/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);

    for(i=0;i<256;i++)vga_setpalette(i,i,i,i);
    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void nv3_unlock(void)
{
    __svgalib_outcrtc(0x11,__svgalib_incrtc(0x11)&0x7f);
    __svgalib_outseq(LOCK_EXT_INDEX,UNLOCK_EXT_MAGIC);    
}

static void nv4_unlock(void)
{
    __svgalib_outcrtc(0x11,__svgalib_incrtc(0x11)&0x7f);
    __svgalib_outcrtc(0x1f, UNLOCK_EXT_MAGIC);
}


/* Relock chipset-specific registers */
/* (currently not used) */

static void nv3_lock(void)
{
__svgalib_outseq(LOCK_EXT_INDEX,UNLOCK_EXT_MAGIC+1);    

}

/* Indentify chipset, initialize and return non-zero if detected */

static int nv3_test(void)
{  unsigned long buf[64];
   int found=0;
   int _ioperm=0;

   if (getenv("IOPERM") == NULL) {
     _ioperm=1;
     if (iopl(3) < 0) {
       printf("svgalib: nv3: cannot get I/O permissions\n");
       exit(1);
     }
   }
   found=__svgalib_pci_find_vendor_vga(0x12d2,buf,0);
   if (found) {
      found=__svgalib_pci_find_vendor_vga(0x10de,buf,0);
      if (_ioperm) iopl(0);
      if(found)return 0;
   };
   if (_ioperm) iopl(0);
   switch(buf[0]>>16){
      case 0x18: nv3_chiptype=Riva128; break;
      case 0x20:
      case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
      case 0xA0:
      case 0x28: nv3_chiptype=RivaTNT; break;
      case 0x150: case 0x151: case 0x152: case 0x153:
      case 0x110: case 0x111: case 0x113:
      case 0x101: case 0x103:
      case 0x100: nv3_chiptype=GEFORCE; break;
      default: nv3_chiptype=-1;
   };

   if(nv3_chiptype==-1)return 0;
   MMIOBASE=0;
   LINEARBASE=0;
   nv3_init(0,0,0);
return 1;
}


/* No r/w paging - I guess it's possible, but is it useful? */
static void nv3_setrdpage(int page)
{
}
static void nv3_setwrpage(int page)
{
}


/* Set display start address (not for 16 color modes) */

static void nv3_setdisplaystart(int address)
{  unsigned char byte;
  address=address >> 2;
  __svgalib_outcrtc(0x0d,address&0xff);
  __svgalib_outcrtc(0x0c,(address>>8)&0xff);
  byte=PCRTC_Read(REPAINT0) & 0xe0;
  PCRTC_Write(REPAINT0,((address>>16)&0x1f)|byte);
  
}


/* Set logical scanline length (usually multiple of 8) */

static void nv3_setlogicalwidth(int width)
{  int byte ;

  __svgalib_outcrtc(0x13,(width >> 3)&0xff);
  byte=PCRTC_Read(REPAINT0) & 0x1f;
  PCRTC_Write(REPAINT0,SetBitField(width,13:11,7:5)|byte);

}

static int nv3_linear(int op, int param)
{
if (op==LINEAR_ENABLE || op==LINEAR_DISABLE){ nv3_is_linear=1-nv3_is_linear; return 0;}
if (op==LINEAR_QUERY_BASE) { return LINEARBASE ;}
if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
    else return -1;		/* Unknown function. */
}

static int nv3_match_programmable_clock(int clock)
{
return clock ;
}
static int nv3_map_clock(int bpp, int clock)
{
return clock ;
}
static int nv3_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}
/* Function table (exported) */

DriverSpecs __svgalib_nv3_driverspecs =
{
    nv3_saveregs,
    nv3_setregs,
    nv3_unlock,
    nv3_lock,
    nv3_test,
    nv3_init,
    nv3_setpage,
    nv3_setrdpage,
    nv3_setwrpage,
    nv3_setmode,
    nv3_modeavailable,
    nv3_setdisplaystart,
    nv3_setlogicalwidth,
    nv3_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    nv3_linear,
    0				/* accelspecs, filled in during init. */
};

#define MapDevice(device,base) \
  nv##device##Port=(unsigned*)(mmap(0, \
     	DEVICE_SIZE(device),PROT_WRITE,MAP_SHARED,__svgalib_mem_fd,\
        (MMIOBASE)+DEVICE_BASE(device)))

/* Initialize chipset (called after detection) */

static int nv3_init(int force, int par1, int par2)
{   
    char *architectures[3]={"nv3, Riva128/Riva128ZX",
                            "nv4, RivaTNT/RivaTNT2", 
                            "nv10, GeForce"};
    
    nv3_unlock();
    
    if(MMIOBASE==0) {
       unsigned long buf[64];
       int _ioperm=0,found;
       
       if (getenv("IOPERM") == NULL) {
         _ioperm=1;
         if (iopl(3) < 0) {
           printf("svgalib: nv3: cannot get I/O permissions\n");
           exit(1);
         }
       }
       
       found=__svgalib_pci_find_vendor_vga(0x12d2,buf,0);
       if (found) {
          found=__svgalib_pci_find_vendor_vga(0x10de,buf,0);
          if (_ioperm) iopl(0);
          if(found)return 0;
       } else if (_ioperm) iopl(0);
       switch(buf[0]>>16){
          case 0x18: nv3_chiptype=Riva128; break;
          case 0x20:
          case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
          case 0xA0:
          case 0x28: nv3_chiptype=RivaTNT; break;
          case 0x150: case 0x151: case 0x152: case 0x153:
          case 0x110: case 0x111: case 0x113:
          case 0x101: case 0x103:
          case 0x100: nv3_chiptype=GEFORCE; break;
          default: nv3_chiptype=Riva128;
       };
       MMIOBASE=buf[4]&0xffffff00;
       LINEARBASE=buf[5]&0xffffff00;
    };
    if (force) {
	nv3_memory = par1;
	nv3_chiptype = par2;
    };
    MapDevice(PRAMDAC,regBase);
    MapDevice(PFB,regBase);
    MapDevice(PEXTDEV,regBase);
     
    if(!force){
       int boot0;
       
       boot0=PFB_Read(BOOT_0);
       switch(nv3_chiptype){
          case Riva128:
                 if(boot0&0x20)nv3_memory=8192; else nv3_memory=1024<<(boot0&3); 
                 if(nv3_memory==1024)nv3_memory=8192;
                 break;
          case RivaTNT: 
                 nv3_memory=2048<<(boot0&3); 
                 if(nv3_memory==2048)nv3_memory=32768;
                 break;
          case GEFORCE:
             	 nv3_memory=16384; /* do this later */
                 break;
       };
    };

    {
       int temp;
       
       temp=PEXTDEV_Read(0);
       switch(nv3_chiptype){
          case Riva128:
             PLL_INPUT_FREQ= (temp&0x20) ? 14318 : 13500;
             break;
          case RivaTNT:
          case GEFORCE:
             PLL_INPUT_FREQ= (temp&0x40) ? 14318 : 13500;
             break;
       };
       
    };

    if (__svgalib_driver_report) {
	printf("Using RIVA driver, %iKB, Type:%s.\n",nv3_memory,architectures[nv3_chiptype]);
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = nv3_memory;
    cardspecs->maxPixelClock4bpp = 75000;
    cardspecs->maxPixelClock8bpp = 230000;
    cardspecs->maxPixelClock16bpp = 230000;
    cardspecs->maxPixelClock24bpp = 0;
    cardspecs->maxPixelClock32bpp = 230000;
    cardspecs->flags = CLOCK_PROGRAMMABLE ;
    cardspecs->maxHorizontalCrtc = 4088;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->clocks = NULL;
    cardspecs->mapClock = nv3_map_clock;
    cardspecs->mapHorizontalCrtc = nv3_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=nv3_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_nv3_driverspecs;
    
    if((nv3_chiptype==RivaTNT)||(nv3_chiptype==GEFORCE))
      __svgalib_driverspecs->unlock=nv4_unlock;
    
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=LINEARBASE;
    __svgalib_linear_mem_size=nv3_memory*0x400;
    return 0;
}

static int NV3ClockSelect(float clockIn,float *clockOut,int *mOut,
                                        int *nOut,int *pOut)
{ 
  int m,n,p;
  float bestDiff=1e10;
  float target=0.0;
  float best=0.0;
  float diff;
  int nMax,nMin;
  
  *clockOut=0.0;
  for(p=P_MIN;p<=P_MAX;p++) {
    for(m=M_MIN;m<=M_MAX;m++) {
      float fm=(float)m;
      /* Now calculate maximum and minimum values for n */
      nMax=(int) (((256000/PLL_INPUT_FREQ)*fm)-0.5);
      nMin=(int) (((128000/PLL_INPUT_FREQ)*fm)+0.5);
      n=(int)(((clockIn*((float)(1<<p)))/PLL_INPUT_FREQ)*fm);
      if(n>=nMin && n<=nMax) {  
        float fn=(float)n;
        target=(PLL_INPUT_FREQ*(fn/fm))/((float)(1<<p));
        diff=fabs(target-clockIn);
        if(diff<bestDiff) {
          bestDiff=diff;
          best=target;
          *mOut=m;*nOut=n;*pOut=p;
          *clockOut=best;
	}
      }
    }
  }
  return (best!=0.0);    
}


