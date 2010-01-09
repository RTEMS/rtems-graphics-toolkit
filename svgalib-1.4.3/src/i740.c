/*
 i740 chipset driver
  written by Matan Ziv-Av.
  Only tested on a Hercules Terminator 2x/I
  Interlaced modes don't work yet.
*/
#include <stdlib.h>
#include <stdio.h>		
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"
#include "timing.h"
#include "vgaregs.h"
#include "interface.h"
#include "accel.h"
#include "vgapci.h"
#include "i740_reg.h"

typedef struct {
    unsigned char DisplayControl;
    unsigned char PixelPipeCfg0;
    unsigned char PixelPipeCfg1;
    unsigned char PixelPipeCfg2;
    unsigned char VideoClk2_M;
    unsigned char VideoClk2_N;
    unsigned char VideoClk2_MN_MSBs;
    unsigned char VideoClk2_DivisorSel;
    unsigned char PLLControl;
    unsigned char AddressMapping;
    unsigned char IOControl;
    unsigned char BitBLTControl;
    unsigned char ExtVertTotal;
    unsigned char ExtVertDispEnd;
    unsigned char ExtVertSyncStart;
    unsigned char ExtVertBlankStart;
    unsigned char ExtHorizTotal;
    unsigned char ExtHorizBlank;
    unsigned char ExtOffset;
    unsigned char InterlaceControl;
    unsigned char ExtStartAddr;
    unsigned char ExtStartAddrHi;    
    unsigned int  LMI_FIFO_Watermark;
} vgaI740Rec, *vgaI740Ptr;

#define I740REG_SAVE(i) (VGA_TOTAL_REGS+i)
#define I740_TOTAL_REGS (VGA_TOTAL_REGS + 26)

static int i740_init(int, int, int);
static void i740_unlock(void);
static void i740_lock(void);

void __svgalib_i740accel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int i740_memory, I740HasSGRAM;
static int i740_is_linear, i740_linear_base, i740_mmio_base;


static CardSpecs *cardspecs;

static void i740_setpage(int page)
{
   outb(XRX,14);
   outb(XRX+1,page);
}

static int __svgalib_i740_inlinearmode(void)
{
return i740_is_linear;
}

/* Fill in chipset specific mode information */

static void i740_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = i740_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 4088;
    modeinfo->startaddressrange = i740_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(i740_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_i740_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

static int i740_saveregs(unsigned char regs[])
{ 
    vgaI740Ptr save;

    i740_unlock();		
    save = (vgaI740Ptr)(regs+VGA_TOTAL_REGS);

    /*
     * The port I/O code necessary to read in the extended registers 
     * into the fields of the vgaI740Rec structure goes here.
     */

    outb(XRX, IO_CTNL);           save->IOControl = inb(XRX+1);
    outb(XRX, ADDRESS_MAPPING);   save->AddressMapping = inb(XRX+1);
    outb(XRX, BITBLT_CNTL);       save->BitBLTControl = inb(XRX+1);
    outb(XRX, VCLK2_VCO_M);       save->VideoClk2_M = inb(XRX+1);
    outb(XRX, VCLK2_VCO_N);       save->VideoClk2_N = inb(XRX+1);
    outb(XRX, VCLK2_VCO_MN_MSBS); save->VideoClk2_MN_MSBs = inb(XRX+1);
    outb(XRX, VCLK2_VCO_DIV_SEL); save->VideoClk2_DivisorSel = inb(XRX+1);
    outb(XRX, PLL_CNTL);          save->PLLControl = inb(XRX+1);

    save->ExtVertTotal = __svgalib_inCR(EXT_VERT_TOTAL);
    save->ExtVertDispEnd = __svgalib_inCR(EXT_VERT_DISPLAY);
    save->ExtVertSyncStart = __svgalib_inCR(EXT_VERT_SYNC_START);
    save->ExtVertBlankStart = __svgalib_inCR(EXT_VERT_BLANK_START);
    save->ExtHorizTotal = __svgalib_inCR(EXT_HORIZ_TOTAL);
    save->ExtHorizBlank = __svgalib_inCR(EXT_HORIZ_BLANK);
    save->ExtOffset = __svgalib_inCR(EXT_OFFSET);
    save->InterlaceControl = __svgalib_inCR(INTERLACE_CNTL);
    save->ExtStartAddr = __svgalib_inCR(EXT_START_ADDR);
    save->ExtStartAddrHi = __svgalib_inCR(EXT_START_ADDR_HI);

    outb(XRX, PIXPIPE_CONFIG_0); save->PixelPipeCfg0 = inb(XRX+1);
    outb(XRX, PIXPIPE_CONFIG_1); save->PixelPipeCfg1 = inb(XRX+1);
    outb(XRX, PIXPIPE_CONFIG_2); save->PixelPipeCfg2 = inb(XRX+1);
    outb(XRX, DISPLAY_CNTL);     save->DisplayControl = inb(XRX+1);

    save->LMI_FIFO_Watermark = INREG(FWATER_BLC);

    return I740_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void i740_setregs(const unsigned char regs[], int mode)
{  
    int temp;
    vgaI740Ptr restore;

    i740_unlock();		
    restore = (vgaI740Ptr)(regs+VGA_TOTAL_REGS);


    outb(XRX, DRAM_EXT_CNTL); outb(XRX+1, DRAM_REFRESH_DISABLE);

    usleep(1000); /* Wait 1 ms */

    /* Write the M, N and P values */
    outb(XRX, VCLK2_VCO_M);       outb(XRX+1, restore->VideoClk2_M);
    outb(XRX, VCLK2_VCO_N);       outb(XRX+1, restore->VideoClk2_N);
    outb(XRX, VCLK2_VCO_MN_MSBS); outb(XRX+1, restore->VideoClk2_MN_MSBs);
    outb(XRX, VCLK2_VCO_DIV_SEL); outb(XRX+1, restore->VideoClk2_DivisorSel);

    outb(XRX, PIXPIPE_CONFIG_0); temp = inb(XRX+1);
    temp &= 0x7F; /* Save all but the 8 bit dac mode bit */
    temp |= (restore->PixelPipeCfg0 & DAC_8_BIT);
    outb(XRX, PIXPIPE_CONFIG_0); outb(XRX+1, temp);

    __svgalib_outCR(EXT_VERT_TOTAL, restore->ExtVertTotal);

    __svgalib_outCR(EXT_VERT_DISPLAY, restore->ExtVertDispEnd);

    __svgalib_outCR(EXT_VERT_SYNC_START, restore->ExtVertSyncStart);

    __svgalib_outCR(EXT_VERT_BLANK_START, restore->ExtVertBlankStart);

    __svgalib_outCR(EXT_HORIZ_TOTAL, restore->ExtHorizTotal);

    __svgalib_outCR(EXT_HORIZ_BLANK, restore->ExtHorizBlank);

    __svgalib_outCR(EXT_OFFSET, restore->ExtOffset);

    __svgalib_outCR(EXT_START_ADDR, restore->ExtStartAddr);
    __svgalib_outCR(EXT_START_ADDR_HI, restore->ExtStartAddrHi);

    temp=__svgalib_inCR(INTERLACE_CNTL); 
    temp &= ~INTERLACE_ENABLE;
    temp |= restore->InterlaceControl;
    __svgalib_outCR(INTERLACE_CNTL, temp);

    outb(XRX, ADDRESS_MAPPING); temp = inb(XRX+1);
    temp &= 0xE0; /* Save reserved bits 7:5 */
    temp |= restore->AddressMapping;
    outb(XRX, ADDRESS_MAPPING); outb(XRX+1, temp);

    outb(XRX, IO_CTNL); temp = inb(XRX+1);
    temp &= ~(EXTENDED_ATTR_CNTL|EXTENDED_CRTC_CNTL); 
    temp |= restore->IOControl;
    outb(XRX, IO_CTNL); outb(XRX+1, temp);

    outb(XRX, BITBLT_CNTL); temp = inb(XRX+1);
    temp &= ~COLEXP_MODE;
    temp |= restore->BitBLTControl;
    outb(XRX, BITBLT_CNTL); outb(XRX+1, temp);

    outb(XRX, DISPLAY_CNTL); temp = inb(XRX+1);
    temp &= ~(VGA_WRAP_MODE | GUI_MODE);
    temp |= restore->DisplayControl;
    outb(XRX, DISPLAY_CNTL); outb(XRX+1, temp);

    outb(XRX, PIXPIPE_CONFIG_0); temp = inb(XRX+1);
    temp &= 0x64; /* Save reserved bits 6:5,2 */
    temp |= restore->PixelPipeCfg0;
    outb(XRX, PIXPIPE_CONFIG_0); outb(XRX+1, temp);

    outb(XRX, PIXPIPE_CONFIG_2); temp = inb(XRX+1);
    temp &= 0xF3; /* Save reserved bits 7:4,1:0 */
    temp |= restore->PixelPipeCfg2;
    outb(XRX, PIXPIPE_CONFIG_2); outb(XRX+1, temp);

    outb(XRX, PLL_CNTL); temp = inb(XRX+1);
    temp &= ~PLL_MEMCLK_SEL;
#if 1
    temp = restore->PLLControl; /* To fix the 2.3X BIOS problem */
#else
    temp |= restore->PLLControl;
#endif
    outb(XRX, PLL_CNTL); outb(XRX+1, temp);

    outb(XRX, PIXPIPE_CONFIG_1); temp = inb(XRX+1);
    temp &= ~DISPLAY_COLOR_MODE;
    temp |= restore->PixelPipeCfg1;
    outb(XRX, PIXPIPE_CONFIG_1); outb(XRX+1, temp);

    temp = INREG(FWATER_BLC);
    temp &= ~(LMI_BURST_LENGTH | LMI_FIFO_WATERMARK);
    temp |= restore->LMI_FIFO_Watermark;
    OUTREG(FWATER_BLC, temp);

    /* Turn on DRAM Refresh */
    outb(XRX, DRAM_EXT_CNTL); outb(XRX+1, DRAM_REFRESH_60HZ);

    usleep(50000);

}

/* Return nonzero if mode is available */

static int i740_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (i740_memory * 1024 < info->ydim * info->xbytes)
	return 0;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

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

static unsigned int I740CalcFIFO(double freq, int bpp)
{
    /*
     * Would like to calculate these values automatically, but a generic
     * algorithm does not seem possible.  Note: These FIFO water mark
     * values were tested on several cards and seem to eliminate the
     * all of the snow and vertical banding, but fine adjustments will
     * probably be required for other cards.
     */

    unsigned int wm = 0x18120000;

    switch (bpp) {
    case 8:
	if (I740HasSGRAM) {
	    if      (freq > 200) wm = 0x18120000;
	    else if (freq > 175) wm = 0x16110000;
	    else if (freq > 135) wm = 0x120E0000;
	    else                 wm = 0x100D0000;
	} else {
	    if      (freq > 200) wm = 0x18120000;
	    else if (freq > 175) wm = 0x16110000;
	    else if (freq > 135) wm = 0x120E0000;
	    else                 wm = 0x100D0000;
	}
	break;
    case 16:
	if (I740HasSGRAM) {
	    if      (freq > 140) wm = 0x2C1D0000;
	    else if (freq > 120) wm = 0x2C180000;
	    else if (freq > 100) wm = 0x24160000;
	    else if (freq >  90) wm = 0x18120000;
	    else if (freq >  50) wm = 0x16110000;
	    else if (freq >  32) wm = 0x13100000;
	    else                 wm = 0x120E0000;
	} else {
	    if      (freq > 160) wm = 0x28200000;
	    else if (freq > 140) wm = 0x2A1E0000;
	    else if (freq > 130) wm = 0x2B1A0000;
	    else if (freq > 120) wm = 0x2C180000;
	    else if (freq > 100) wm = 0x24180000;
	    else if (freq >  90) wm = 0x18120000;
	    else if (freq >  50) wm = 0x16110000;
	    else if (freq >  32) wm = 0x13100000;
	    else                 wm = 0x120E0000;
	}
	break;
    case 24:
	if (I740HasSGRAM) {
	    if      (freq > 130) wm = 0x31200000;
	    else if (freq > 120) wm = 0x2E200000;
	    else if (freq > 100) wm = 0x2C1D0000;
	    else if (freq >  80) wm = 0x25180000;
	    else if (freq >  64) wm = 0x24160000;
	    else if (freq >  49) wm = 0x18120000;
	    else if (freq >  32) wm = 0x16110000;
	    else                 wm = 0x13100000;
	} else {
	    if      (freq > 120) wm = 0x311F0000;
	    else if (freq > 100) wm = 0x2C1D0000;
	    else if (freq >  80) wm = 0x25180000;
	    else if (freq >  64) wm = 0x24160000;
	    else if (freq >  49) wm = 0x18120000;
	    else if (freq >  32) wm = 0x16110000;
	    else                 wm = 0x13100000;
	}
	break;
    case 32:
	if (I740HasSGRAM) {
	    if      (freq >  80) wm = 0x2A200000;
	    else if (freq >  60) wm = 0x281A0000;
	    else if (freq >  49) wm = 0x25180000;
	    else if (freq >  32) wm = 0x18120000;
	    else                 wm = 0x16110000;
	} else {
	    if      (freq >  80) wm = 0x29200000;
	    else if (freq >  60) wm = 0x281A0000;
	    else if (freq >  49) wm = 0x25180000;
	    else if (freq >  32) wm = 0x18120000;
	    else                 wm = 0x16110000;
	}
	break;
    }

    return wm;
}

#define MAX_VCO_FREQ 450.0
#define TARGET_MAX_N 30
#define REF_FREQ 66.66666666667

#define CALC_VCLK(m,n,p,d) \
    (double)m / ((double)n * (1 << p)) * (4 << (d << 1)) * REF_FREQ

static void
I740CalcVCLK(double freq ,int *M, int *N, int *MN, int *DIVSEL )
{
    int m, n, p, d;
    double f_out;
    double f_err;
    double f_vco;
    int m_best = 0, n_best = 0, p_best = 0, d_best = 0;
    double f_target = freq;
    double err_max = 0.005;
    double err_target = 0.001;
    double err_best = 999999.0;

    p_best = p = log(MAX_VCO_FREQ/f_target)/log((double)2);
    d_best = d = 0;

    f_vco = f_target * (1 << p);

    n = 2;
    do {
	n++;
	m = f_vco / (REF_FREQ / (double)n) / (double)4.0 + 0.5;
	if (m < 3) m = 3;
	f_out = CALC_VCLK(m,n,p,d);
	f_err = 1.0 - (f_target/f_out);
	if (fabs(f_err) < err_max) {
	    m_best = m;
	    n_best = n;
	    err_best = f_err;
	}
    } while ((fabs(f_err) >= err_target) &&
	     ((n <= TARGET_MAX_N) || (fabs(err_best) > err_max)));

    if (fabs(f_err) < err_target) {
	m_best = m;
        n_best = n;
    }

    *M     = (m_best-2) & 0xFF;
    *N     = (n_best-2) & 0xFF;
    *MN    = ((((n_best-2) >> 4) & VCO_N_MSBS) |
       	      (((m_best-2) >> 8) & VCO_M_MSBS));
    *DIVSEL= ((p_best << 4) | (d_best ? 4 : 0) | REF_DIV_1);

#if 0
    printf("Setting dot clock to %.6lf MHz "
	   "[ %02X %02X %02X ] "
	   "[ %d %d %d %d ]\n",
	   CALC_VCLK(m_best,n_best,p_best,d_best),
	   new->VideoClk2_M,
	   new->VideoClk2_N,
	   new->VideoClk2_DivisorSel,
	   m_best, n_best, p_best, d_best);
#endif
}

/* Local, called by i740_setmode(). */
static void i740_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{    
    vgaI740Ptr new;
    int n, m, mn, divsel;
    
    __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

    new = (vgaI740Ptr)(moderegs+VGA_TOTAL_REGS);

    moderegs[0x13] = modeinfo->lineWidth >> 3;
    new->ExtOffset      = modeinfo->lineWidth >> 11;

    switch (modeinfo->bitsPerPixel) {
        case 8:
            new->PixelPipeCfg1 = DISPLAY_8BPP_MODE;
	break;
        case 15:
        case 16:
	    if (modeinfo->greenWeight == 5) {
	        new->PixelPipeCfg1 = DISPLAY_15BPP_MODE;
	    } else {
	        new->PixelPipeCfg1 = DISPLAY_16BPP_MODE;
	    }
	    break;
        case 24:
	    new->PixelPipeCfg1 = DISPLAY_24BPP_MODE;
	    break;
        case 32:
	    new->PixelPipeCfg1 = DISPLAY_32BPP_MODE;
	    break;
        default:
	    break;
    }

    if (modeinfo->bitsPerPixel>8)
	new->PixelPipeCfg0 = DAC_8_BIT;
    else
	new->PixelPipeCfg0 = DAC_6_BIT;

    new->PixelPipeCfg2 = 0;

    /* Turn on Extended VGA Interpretation */
    new->IOControl = EXTENDED_CRTC_CNTL;

    /* Turn on linear and page mapping */
    new->AddressMapping = LINEAR_MODE_ENABLE | PAGE_MAPPING_ENABLE;

    /* Turn on GUI mode */
    new->DisplayControl = HIRES_MODE;

    /* Set the MCLK freq */
    if (0)
	new->PLLControl = PLL_MEMCLK__66667KHZ; /*  66 MHz */
    else
	new->PLLControl = PLL_MEMCLK_100000KHZ; /* 100 MHz -- use as default */

    /* Calculate the extended CRTC regs */
    new->ExtVertTotal = (modetiming->CrtcVTotal - 2) >> 8;
    new->ExtVertDispEnd = (modetiming->CrtcVDisplay - 1) >> 8;
    new->ExtVertSyncStart = modetiming->CrtcVSyncStart >> 8;
    new->ExtVertBlankStart = modetiming->CrtcVSyncStart >> 8;
    new->ExtHorizTotal = ((modetiming->CrtcHTotal >> 3) - 5) >> 8;
    new->ExtHorizBlank = ((modetiming->CrtcHSyncEnd >> 3) & 0x40) >> 6;

    /* Turn on interlaced mode if necessary */
    if (modetiming->flags & INTERLACED)
	new->InterlaceControl = INTERLACE_ENABLE;
    else
	new->InterlaceControl = INTERLACE_DISABLE;

    moderegs[0x11] = 0;

    I740CalcVCLK(modetiming->pixelClock/1000, &m, &n, &mn, &divsel);
    
    new->VideoClk2_M = m;
    new->VideoClk2_N = n; 
    new->VideoClk2_MN_MSBs = mn;
    new->VideoClk2_DivisorSel = divsel;

    /* Since we program the clocks ourselves, always use VCLK2. */
    moderegs[59] |= 0x0C;

    new->ExtStartAddr=0;
    new->ExtStartAddrHi=0;

    /* Calculate the FIFO Watermark and Burst Length. */
    new->LMI_FIFO_Watermark = I740CalcFIFO(modetiming->pixelClock/1000,modeinfo->bitsPerPixel);

    return ;
}


static int i740_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256)||(mode==G720x348x2)) {

	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!i740_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(I740_TOTAL_REGS);

    i740_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    i740_setregs(moderegs, mode);		/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);

    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void i740_unlock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);
    
}

static void i740_lock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);   
}


#define VENDOR_ID 0x8086
#define I740_ID_PCI 0x00d1
#define I740_ID_AGP 0x7800

/* Indentify chipset, initialize and return non-zero if detected */

static int i740_test(void)
{
    int _ioperm=0,found;
    long buf[64];
    
    if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: cannot get I/O permissions\n");
        exit(1);
      }
    }
    
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    
    if (_ioperm) iopl(0);
    
    if(!found&&
       ((((buf[0]>>16)&0xffff)==I740_ID_PCI)||
        (((buf[0]>>16)&0xffff)==I740_ID_AGP))){
          i740_init(0,0,0);
          return 1;
    };
    
    return 0;
}


/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void i740_setdisplaystart(int address)
{ 
  address=address >> 2;
  outw(CRT_IC, (address & 0x00FF00) | 0x0C);
  outw(CRT_IC, ((address & 0x00FF) << 8) | 0x0D);
  __svgalib_outCR(EXT_START_ADDR_HI,(address&0x3fc00000)>>22);
  __svgalib_outCR(EXT_START_ADDR,((address&0x3f0000)>>16)|EXT_START_ADDR_ENABLE);
}


/* Set logical scanline length (usually multiple of 8) */
/* Cirrus supports multiples of 8, up to 4088 */

static void i740_setlogicalwidth(int width)
{   
    int offset = width >> 3;
 
    __svgalib_outCR(0x13,offset&0xff);
}

static int i740_linear(int op, int param)
{
    if (op==LINEAR_ENABLE){i740_is_linear=1; return 0;};
    if (op==LINEAR_DISABLE){i740_is_linear=0; return 0;};
    if (op==LINEAR_QUERY_BASE) return i740_linear_base;
    if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
        else return -1;		/* Unknown function. */
}

static int i740_match_programmable_clock(int clock)
{
return clock ;
}

static int i740_map_clock(int bpp, int clock)
{
return clock ;
}

static int i740_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_i740_driverspecs =
{
    i740_saveregs,
    i740_setregs,
    i740_unlock,
    i740_lock,
    i740_test,
    i740_init,
    i740_setpage,
    NULL,
    NULL,
    i740_setmode,
    i740_modeavailable,
    i740_setdisplaystart,
    i740_setlogicalwidth,
    i740_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    i740_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int i740_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    int found=0;
    int _ioperm=0;
    int temp;
    
    i740_unlock();
    if (force) {
	i740_memory = par1;
/*        i740_chiptype = par2;*/
    } else {
	outb(XRX, DRAM_ROW_TYPE);
	if ((inb(XRX+1) & DRAM_ROW_1) == DRAM_ROW_1_SDRAM)
	    outb(XRX, DRAM_ROW_BNDRY_1);
	else
	    outb(XRX, DRAM_ROW_BNDRY_0);
        i740_memory = inb(XRX+1)*1024;
    };

    outb(XRX, DRAM_ROW_CNTL_LO); temp = inb(XRX+1);
    I740HasSGRAM = !((temp & DRAM_RAS_TIMING) || (temp & DRAM_RAS_PRECHARGE));

    if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: cannot get I/O permissions\n");
        exit(1);
      }
    }
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    if (_ioperm) iopl(0);
    i740_linear_base=0;
    if(!found&&((((buf[0]>>16)&0xffff)==I740_ID_PCI)||(((buf[0]>>16)&0xffff)==I740_ID_AGP))){
       i740_linear_base=buf[4]&0xffffff00;
       i740_mmio_base=buf[5]&0xffffff00;
    };

    if (__svgalib_driver_report) {
	printf("Using I740 driver, %iKB.\n",i740_memory);
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = i740_memory;
    cardspecs->maxPixelClock4bpp = 75000;	
    cardspecs->maxPixelClock8bpp = 203000;	
    cardspecs->maxPixelClock16bpp = 163000;	
    cardspecs->maxPixelClock24bpp = 128000;
    cardspecs->maxPixelClock32bpp = 86000;
    cardspecs->flags = CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 4088;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->mapClock = i740_map_clock;
    cardspecs->mapHorizontalCrtc = i740_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=i740_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_i740_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=i740_linear_base;
    __svgalib_linear_mem_size=i740_memory*0x400;
    __svgalib_mmio_base=i740_mmio_base;
    __svgalib_mmio_size=512*0x400;
    return 0;
}
