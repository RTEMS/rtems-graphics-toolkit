/*
    NeoMagic chipset driver 
	Written by Shigehiro Nomura <s.nomura@mba.nifty.ne.jp>

Does not support external screen.

Remarks:
Problem: The modes whose width is longer than the width of LCD panel
         are also reported by vgatest, but not displaying properly.
 --> Please do not select such modes :-)

Note:
  When use Toshiba Libretto100,110, please define "LIBRETTO100" at
  line 19 in src/neo.c
  And add the following lines to libvga.config
  -------------------------------------------------------------------
   HorizSync 31.5 70
   VertRefresh 50 100
   Modeline "800x480"   50  800  856  976 1024   480  483  490  504 +hsync
+vsync
   newmode  800 480 256       800 1
   newmode  800 480 32768    1600 2
   newmode  800 480 65536    1600 2
   newmode  800 480 16777216 2400 3
  -------------------------------------------------------------------

*/
#include <stdlib.h>
#include <stdio.h>		
#include <string.h>
#include <unistd.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"
#include "timing.h"
#include "vgaregs.h"
#include "interface.h"
#include "accel.h"
#include "vgapci.h"

#undef DEBUG
#undef NEO_PCI_BURST
#undef LIBRETTO100  /* Define if Toshiba Libretto100/110 */

#define TRUE (1)
#define FALSE (0)

#define VENDOR_ID 0x10c8   /* NeoMagic */
#define PCI_CHIP_NM2070		0x0001
#define PCI_CHIP_NM2090		0x0002
#define PCI_CHIP_NM2093		0x0003
#define PCI_CHIP_NM2097		0x0083
#define PCI_CHIP_NM2160		0x0004
#define PCI_CHIP_NM2200		0x0005

#define GRAX	  0x3ce

#define NEO_EXT_CR_MAX 0x85
#define NEO_EXT_GR_MAX 0xC7

#define NEOREG_SAVE(i) (VGA_TOTAL_REGS+i)
#define  GeneralLockReg       NEOREG_SAVE(0)   /* GRAX(0x0a) */
#define  ExtCRTDispAddr       NEOREG_SAVE(1)   /* GRAX(0x0e) */
#define  ExtCRTOffset         NEOREG_SAVE(2)   /* GRAX(0x0f) */
#define  SysIfaceCntl1        NEOREG_SAVE(3)   /* GRAX(0x10) */
#define  SysIfaceCntl2        NEOREG_SAVE(4)   /* GRAX(0x11) */
#define  SingleAddrPage       NEOREG_SAVE(5)   /* GRAX(0x15) */
#define  DualAddrPage         NEOREG_SAVE(6)   /* GRAX(0x16) */
#define  PanelDispCntlReg1    NEOREG_SAVE(7)   /* GRAX(0x20) */
#define  PanelDispCntlReg2    NEOREG_SAVE(8)   /* GRAX(0x25) */
#define  PanelDispCntlReg3    NEOREG_SAVE(9)   /* GRAX(0x30) */
#define  PanelVertCenterReg1  NEOREG_SAVE(10)  /* GRAX(0x28) */
#define  PanelVertCenterReg2  NEOREG_SAVE(11)  /* GRAX(0x29) */
#define  PanelVertCenterReg3  NEOREG_SAVE(12)  /* GRAX(0x2a) */
#define  PanelVertCenterReg4  NEOREG_SAVE(13)  /* GRAX(0x32) */
#define  PanelVertCenterReg5  NEOREG_SAVE(14)  /* GRAX(0x37) */
#define  PanelHorizCenterReg1 NEOREG_SAVE(15)  /* GRAX(0x33) */
#define  PanelHorizCenterReg2 NEOREG_SAVE(16)  /* GRAX(0x34) */
#define  PanelHorizCenterReg3 NEOREG_SAVE(17)  /* GRAX(0x35) */
#define  PanelHorizCenterReg4 NEOREG_SAVE(18)  /* GRAX(0x36) */
#define  PanelHorizCenterReg5 NEOREG_SAVE(19)  /* GRAX(0x38) */
#define  ExtColorModeSelect   NEOREG_SAVE(20)  /* GRAX(0x90) */
#define  VCLK3NumeratorLow    NEOREG_SAVE(21)  /* GRAX(0x9b) */
#define  VCLK3NumeratorHigh   NEOREG_SAVE(22)  /* GRAX(0x8f) */
#define  VCLK3Denominator     NEOREG_SAVE(23)  /* GRAX(0x9f) */
#define  VerticalExt          NEOREG_SAVE(24)
#define  EXT_SAVED            NEOREG_SAVE(25)  /* EXT regs. saved ? */
#define   EXTCR               NEOREG_SAVE(26)              /* CR(0x00..) */
#define   EXTGR               (EXTCR + NEO_EXT_CR_MAX + 1) /* GR(0x00..) */
#define  DAC                  (EXTGR + NEO_EXT_GR_MAX + 1) /* DAC */
#define NEO_TOTAL_REGS  (DAC + 768)

#define DACDelay \
	{ \
		unsigned char temp = inb(vgaIOBase + 0x0A); \
		temp = inb(vgaIOBase + 0x0A); \
	}


static int neo_init(int, int, int);
static void neo_unlock(void);
static void neo_lock(void);

static int neo_memory;
static int NeoChipset;
static int NeoPanelWidth, NeoPanelHeight;
static int neo_is_linear, neo_linear_base;
static int vgaIOBase;

static CardSpecs *cardspecs;

static void neo_setpage(int page)
{
#ifdef DEBUG
  fprintf(stderr, "neo_setpage: %d\n", page);
#endif
  outb(GRAX, 0x11);
  outw(GRAX, ((inb(GRAX+1) & 0xfc) << 8) | 0x11);
  outw(GRAX, (page << 10) | 0x15); /* set read/write bank */
}

static void neo_setrdpage(int page)
{
#ifdef DEBUG
  fprintf(stderr, "neo_setrdpage: %d\n", page);
#endif
  outb(GRAX, 0x11);
  outw(GRAX, (((inb(GRAX+1) & 0xfc) | 0x01) << 8) | 0x11);
  outw(GRAX, (page << 10) | 0x15); /* set read bank */
}

static void neo_setwrpage(int page)
{
#ifdef DEBUG
  fprintf(stderr, "neo_setwrpage: %d\n", page);
#endif
  outb(GRAX, 0x11);
  outw(GRAX, (((inb(GRAX+1) & 0xfc) | 0x01) << 8) | 0x11);
  outw(GRAX, (page << 10) | 0x16); /* set write bank */
}

static int __svgalib_neo_inlinearmode(void)
{
#ifdef DEBUG
  fprintf(stderr, "neo_inlinearmode\n");
#endif
return neo_is_linear;
}

/* Fill in chipset specific mode information */

static void neo_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{
#ifdef DEBUG
    fprintf(stderr, "neo_getmodeinfo: %d\n", mode);
#endif

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = neo_memory*1024/modeinfo->bytesperpixel;
    if (NeoChipset == PCI_CHIP_NM2200)
      modeinfo->maxlogicalwidth = 1280;
    else
      modeinfo->maxlogicalwidth = 1024;
    modeinfo->startaddressrange = neo_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags |= HAVE_RWPAGE;

#if 1
    if (modeinfo->bytesperpixel >= 1) {
	if(neo_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_neo_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
#endif
}

/* Read and save chipset-specific registers */

static int neo_saveregs(unsigned char regs[])
{ 
  int i;

#ifdef DEBUG
  fprintf(stderr, "neo_saveregs\n");
#endif
  neo_unlock();
  outw(GRAX, 0x0015);   /* bank#0 */

  outb(GRAX, 0x0a); regs[GeneralLockReg] = inb(GRAX + 1);
  outb(GRAX, 0x0e); regs[ExtCRTDispAddr] = inb(GRAX + 1);
  outb(GRAX, 0x0f); regs[ExtCRTOffset] = inb(GRAX + 1);
  outb(GRAX, 0x10); regs[SysIfaceCntl1] = inb(GRAX + 1);
  outb(GRAX, 0x11); regs[SysIfaceCntl2] = inb(GRAX + 1);
  outb(GRAX, 0x15); regs[SingleAddrPage] = inb(GRAX + 1);
  outb(GRAX, 0x16); regs[DualAddrPage] = inb(GRAX+1);
  outb(GRAX, 0x20); regs[PanelDispCntlReg1] = inb(GRAX+1);
  outb(GRAX, 0x25); regs[PanelDispCntlReg2] = inb(GRAX+1);
  outb(GRAX, 0x30); regs[PanelDispCntlReg3] = inb(GRAX+1);
  outb(GRAX, 0x28); regs[PanelVertCenterReg1] = inb(GRAX+1);
  outb(GRAX, 0x29); regs[PanelVertCenterReg2] = inb(GRAX+1);
  outb(GRAX, 0x2a); regs[PanelVertCenterReg3] = inb(GRAX+1);
  if (NeoChipset != PCI_CHIP_NM2070){
    outb(GRAX, 0x32); regs[PanelVertCenterReg4] = inb(GRAX+1);
    outb(GRAX, 0x33); regs[PanelHorizCenterReg1] = inb(GRAX+1);
    outb(GRAX, 0x34); regs[PanelHorizCenterReg2] = inb(GRAX+1);
    outb(GRAX, 0x35); regs[PanelHorizCenterReg3] = inb(GRAX+1);
  }
  if (NeoChipset == PCI_CHIP_NM2160){
    outb(GRAX, 0x36); regs[PanelHorizCenterReg4] = inb(GRAX+1);
  }
  if (NeoChipset == PCI_CHIP_NM2200){
    outb(GRAX, 0x36); regs[PanelHorizCenterReg4] = inb(GRAX+1);
    outb(GRAX, 0x37); regs[PanelVertCenterReg5]  = inb(GRAX+1);
    outb(GRAX, 0x38); regs[PanelHorizCenterReg5] = inb(GRAX+1);
  }
  outb(GRAX, 0x90); regs[ExtColorModeSelect] = inb(GRAX+1);
  outb(GRAX, 0x9B); regs[VCLK3NumeratorLow]  = inb(GRAX+1);
  if (NeoChipset == PCI_CHIP_NM2200){
    outb(GRAX, 0x8F); regs[VCLK3NumeratorHigh] = inb(GRAX+1);
  }
  outb(GRAX, 0x9F); regs[VCLK3Denominator] = inb(GRAX+1);

  regs[EXT_SAVED] = TRUE;
  outb(vgaIOBase + 4, 0x25); regs[EXTCR + 0x25] = inb(vgaIOBase + 5);
  outb(vgaIOBase + 4, 0x2F); regs[EXTCR + 0x2F] = inb(vgaIOBase + 5);
  for (i = 0x40; i <= 0x59; i++){
    outb(vgaIOBase + 4, i); regs[EXTCR + i] = inb(vgaIOBase + 5);
  }
  for (i = 0x60; i <= 0x69; i++){
    outb(vgaIOBase + 4, i); regs[EXTCR + i] = inb(vgaIOBase + 5);
  }
  for (i = 0x70; i <= NEO_EXT_CR_MAX; i++){
    outb(vgaIOBase + 4, i); regs[EXTCR + i] = inb(vgaIOBase + 5);
  }

  for (i = 0x0A; i <= NEO_EXT_GR_MAX; i++){
    outb(GRAX, i); regs[EXTGR + i] = inb(GRAX+1);
  }

  /* DAC */
  outb(0x3C6,0xFF); /* mask */
  outb(0x3C7,0x00); /* read address */
  for (i = 0; i < 768; i++){
    regs[DAC + i] = inb(0x3C9);
    DACDelay;
  }

  return NEO_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void neo_setregs(const unsigned char regs[], int mode)
{
  int i;
  unsigned char temp;

#ifdef DEBUG
  fprintf(stderr, "neo_setregs\n");
#endif
  neo_unlock();		
  outw(GRAX, 0x0015);   /* bank#0 */

  outb(GRAX, 0x0a); outb(GRAX+1, regs[GeneralLockReg]);

  /* set color mode first */
  outb(GRAX, 0x90); temp = inb(GRAX+1);
  switch (NeoChipset){
  case PCI_CHIP_NM2070:
    temp &= 0xF0; /* Save bits 7:4 */
    temp |= (regs[ExtColorModeSelect] & ~0xF0);
    break;
  case PCI_CHIP_NM2090: case PCI_CHIP_NM2093: case PCI_CHIP_NM2097:
  case PCI_CHIP_NM2160: case PCI_CHIP_NM2200:
    temp &= 0x70; /* Save bits 6:4 */
    temp |= (regs[ExtColorModeSelect] & ~0x70);
    break;
  }
  outb(GRAX, 0x90); outb(GRAX+1, temp);

  /* Disable horizontal and vertical graphics and text expansions */
  outb(GRAX, 0x25);
  temp = inb(GRAX+1);
  outb(GRAX, 0x25);
  temp &= 0x39;
  outb(GRAX+1, temp);

  usleep(200000);

  /* DAC */
  outb(0x3C6,0xFF); /* mask */
  outb(0x3C8,0x00); /* write address */
  for (i = 0; i < 768; i++){
    outb(0x3C9, regs[DAC + i]);
    DACDelay;
  }

  outb(GRAX, 0x0E); outb(GRAX+1, regs[ExtCRTDispAddr]);
  outb(GRAX, 0x0F); outb(GRAX+1, regs[ExtCRTOffset]);
  outb(GRAX, 0x10); temp = inb(GRAX+1);
  temp &= 0x0F; /* Save bits 3:0 */
  temp |= (regs[SysIfaceCntl1] & ~0x0F);
  outb(GRAX, 0x10); outb(GRAX+1, temp);

  outb(GRAX, 0x11); outb(GRAX+1, regs[SysIfaceCntl2]);
  outb(GRAX, 0x15); outb(GRAX+1, regs[SingleAddrPage]);
  outb(GRAX, 0x16); outb(GRAX+1, regs[DualAddrPage]);
  outb(GRAX, 0x20); temp = inb(GRAX+1);
  switch (NeoChipset){
  case PCI_CHIP_NM2070:
    temp &= 0xFC; /* Save bits 7:2 */
    temp |= (regs[PanelDispCntlReg1] & ~0xFC);
    break;
  case PCI_CHIP_NM2090:  case PCI_CHIP_NM2093: case PCI_CHIP_NM2097:
  case PCI_CHIP_NM2160:
    temp &= 0xDC; /* Save bits 7:6,4:2 */
    temp |= (regs[PanelDispCntlReg1] & ~0xDC);
    break;
  case PCI_CHIP_NM2200:
    temp &= 0x98; /* Save bits 7,4:3 */
    temp |= (regs[PanelDispCntlReg1] & ~0x98);
    break;
  }
  outb(GRAX, 0x20); outb(GRAX+1, temp);
  outb(GRAX, 0x25); temp = inb(GRAX+1);
  temp &= 0x38; /* Save bits 5:3 */
  temp |= (regs[PanelDispCntlReg2] & ~0x38);
  outb(GRAX, 0x25); outb(GRAX+1, temp);

  if (NeoChipset != PCI_CHIP_NM2070){
    outb(GRAX, 0x30); temp = inb(GRAX+1);
    temp &= 0xEF; /* Save bits 7:5 and bits 3:0 */
    temp |= (regs[PanelDispCntlReg3] & ~0xEF);
    outb(GRAX, 0x30); outb(GRAX+1, temp);
  }

  outb(GRAX, 0x28); outb(GRAX+1, regs[PanelVertCenterReg1]);
  outb(GRAX, 0x29); outb(GRAX+1, regs[PanelVertCenterReg2]);
  outb(GRAX, 0x2a); outb(GRAX+1, regs[PanelVertCenterReg3]);

  if (NeoChipset != PCI_CHIP_NM2070){
    outb(GRAX, 0x32); outb(GRAX+1, regs[PanelVertCenterReg4]);
    outb(GRAX, 0x33); outb(GRAX+1, regs[PanelHorizCenterReg1]);
    outb(GRAX, 0x34); outb(GRAX+1, regs[PanelHorizCenterReg2]);
    outb(GRAX, 0x35); outb(GRAX+1, regs[PanelHorizCenterReg3]);
  }

  if (NeoChipset == PCI_CHIP_NM2160){
    outb(GRAX, 0x36); outb(GRAX+1, regs[PanelHorizCenterReg4]);
  }

  if (NeoChipset == PCI_CHIP_NM2200){
    outb(GRAX, 0x36); outb(GRAX+1, regs[PanelHorizCenterReg4]);
    outb(GRAX, 0x37); outb(GRAX+1, regs[PanelVertCenterReg5]);
    outb(GRAX, 0x38); outb(GRAX+1, regs[PanelHorizCenterReg5]);
  }

#if 0
  outb(GRAX, 0x9B); outb(GRAX+1, regs[VCLK3NumeratorLow]);
  if (NeoChipset == PCI_CHIP_NM2200){
    outb(GRAX, 0x8F); temp = inb(GRAX+1);
    temp &= 0x0F; /* Save bits 3:0 */
    temp |= (regs[VCLK3NumeratorHigh] & ~0x0F);
    outb(GRAX, 0x8F); outb(GRAX+1, temp);
  }
  outb(GRAX, 0x9F); outb(GRAX+1, regs[VCLK3Denominator]);
#endif

  if (regs[EXT_SAVED]){
    outb(vgaIOBase + 4, 0x25); outb(vgaIOBase + 5, regs[EXTCR + 0x25]);
    outb(vgaIOBase + 4, 0x2F); outb(vgaIOBase + 5, regs[EXTCR + 0x2F]);
    for (i = 0x40; i <= 0x59; i++){
      outb(vgaIOBase + 4, i); outb(vgaIOBase + 5, regs[EXTCR + i]);
    }
    for (i = 0x60; i <= 0x69; i++){
      outb(vgaIOBase + 4, i); outb(vgaIOBase + 5, regs[EXTCR + i]);
    }
    for (i = 0x70; i <= NEO_EXT_CR_MAX; i++){
      outb(vgaIOBase + 4, i); outb(vgaIOBase + 5, regs[EXTCR + i]);
    }

    for (i = 0x0a; i <= 0x3f; i++){
      outb(GRAX, i); outb(GRAX+1, regs[EXTGR + i]);
    }
    for (i = 0x90; i <= NEO_EXT_GR_MAX; i++){
      outb(GRAX, i); outb(GRAX+1, regs[EXTGR + i]);
    }
  }

  /* Program vertical extension register */
  if (NeoChipset == PCI_CHIP_NM2200){
    outb(vgaIOBase + 4, 0x70); outb(vgaIOBase + 5, regs[VerticalExt]);
  }
}

#if 0
/*
 * NeoCalcVCLK --
 *
 * Determine the closest clock frequency to the one requested.
 */
#define REF_FREQ 14.31818
#define MAX_N 127
#define MAX_D 31
#define MAX_F 1

static void NeoCalcVCLK(long freq, unsigned char *moderegs)
{
    int n, d, f;
    double f_out;
    double f_diff;
    int n_best = 0, d_best = 0, f_best = 0;
    double f_best_diff = 999999.0;
    double f_target = freq/1000.0;

    for (f = 0; f <= MAX_F; f++)
	for (n = 0; n <= MAX_N; n++)
	    for (d = 0; d <= MAX_D; d++) {
		f_out = (n+1.0)/((d+1.0)*(1<<f))*REF_FREQ;
		f_diff = abs(f_out-f_target);
		if (f_diff < f_best_diff) {
		    f_best_diff = f_diff;
		    n_best = n;
		    d_best = d;
		    f_best = f;
		}
	    }

    if (NeoChipset == PCI_CHIP_NM2200){
      /* NOT_DONE:  We are trying the full range of the 2200 clock.
	 We should be able to try n up to 2047 */
      moderegs[VCLK3NumeratorLow]  = n_best;
      moderegs[VCLK3NumeratorHigh] = (f_best << 7);
    } else {
      moderegs[VCLK3NumeratorLow]  = n_best | (f_best << 7);
    }
    moderegs[VCLK3Denominator] = d_best;
}
#endif


/* Return nonzero if mode is available */

static int neo_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

#ifdef DEBUG
    fprintf(stderr, "neo_modeavaailable: %d\n", mode);
#endif

    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (neo_memory * 1024 < info->ydim * info->xbytes)
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

/* Local, called by neo_setmode(). */

static void neo_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{
  int i, hoffset, voffset;

#ifdef DEBUG
  fprintf(stderr, "neo_initializemode: %d\n", mode);
#endif
  neo_saveregs(moderegs);
  __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

  moderegs[EXT_SAVED] = FALSE;
  moderegs[VGA_AR10] = 0x01;  /* Attribute 0x10 */

  moderegs[0x13] = modeinfo->lineWidth >> 3;
  moderegs[ExtCRTOffset] = modeinfo->lineWidth >> 11;
  switch (modeinfo->bitsPerPixel){
  case  8:
    moderegs[ExtColorModeSelect] = 0x11;
    break;
  case 15:
  case 16:
    if (modeinfo->greenWeight == 5){ /* 15bpp */
      for (i = 0; i < 64; i++){
        moderegs[DAC + i*3+0] = i << 1;
        moderegs[DAC + i*3+1] = i << 1;
        moderegs[DAC + i*3+2] = i << 1;
      }
      moderegs[ExtColorModeSelect] = 0x12;
    } else { /* 16bpp */
      for (i = 0; i < 64; i++){
	moderegs[DAC + i*3+0] = i << 1;
	moderegs[DAC + i*3+1] = i;
	moderegs[DAC + i*3+2] = i << 1;
      }
      moderegs[ExtColorModeSelect] = 0x13;
    }
    break;
  case 24:
    for (i = 0; i < 256; i++){
      moderegs[DAC + i*3+0] = i;
      moderegs[DAC + i*3+1] = i;
      moderegs[DAC + i*3+2] = i;
    }
    moderegs[ExtColorModeSelect] = 0x14;
    break;
  }
  moderegs[ExtCRTDispAddr] = 0x10;

#if 0
  /* Vertical Extension */
  moderegs[VerticalExt] = (((modetiming->CrtcVTotal -2) & 0x400) >> 10 )
    | (((modetiming->CrtcVDisplay -1) & 0x400) >> 9 )
    | (((modetiming->CrtcVSyncStart) & 0x400) >> 8 )
    | (((modetiming->CrtcVSyncStart) & 0x400) >> 7 );
#endif

  /* Disable read/write bursts if requested. */
#ifdef NEO_PCI_BURST
  moderegs[SysIfaceCntl1] = 0x30;
#else /* NEO_PCI_BURST */
  moderegs[SysIfaceCntl1] = 0x00;
#endif /* NEO_PCI_BURST */

  /* If they are used, enable linear addressing and/or enable MMIO. */
  moderegs[SysIfaceCntl2] = 0x00;
  moderegs[SysIfaceCntl2] |= 0x80; /* Linear */
#if 0
  moderegs[SysIfaceCntl2] |= 0x40; /* MMIO */
#endif

  moderegs[PanelDispCntlReg1] = 0x00;
  moderegs[PanelDispCntlReg1] |= 0x02; /* Enable internal display */
#if 0
  moderegs[PanelDispCntlReg1] |= 0x01; /* Enable external display */
#endif

  /* If we are using a fixed mode, then tell the chip we are. */
  switch (modetiming->HDisplay) {
  case 1280:
    moderegs[PanelDispCntlReg1] |= 0x60;
    break;
  case 1024:
    moderegs[PanelDispCntlReg1] |= 0x40;
    break;
  case 800:
    moderegs[PanelDispCntlReg1] |= 0x20;
    break;
  case 640:
    break;
  }

  /* Setup shadow register locking. */
  moderegs[GeneralLockReg] = 0x01;

  moderegs[PanelDispCntlReg2] = 0x00;
  moderegs[PanelDispCntlReg3] = 0x00;

  /*
   * If the screen is to be centerd, turn on the centering for the
   * various modes.
   */
  moderegs[PanelVertCenterReg1] = 0x00;
  moderegs[PanelVertCenterReg2] = 0x00;
  moderegs[PanelVertCenterReg3] = 0x00;
  moderegs[PanelVertCenterReg4] = 0x00;
  moderegs[PanelVertCenterReg5] = 0x00;
  moderegs[PanelHorizCenterReg1] = 0x00;
  moderegs[PanelHorizCenterReg2] = 0x00;
  moderegs[PanelHorizCenterReg3] = 0x00;
  moderegs[PanelHorizCenterReg4] = 0x00;
  moderegs[PanelHorizCenterReg5] = 0x00;

  if (moderegs[PanelDispCntlReg1] & 0x02){
    if (modetiming->HDisplay < NeoPanelWidth){
      moderegs[PanelDispCntlReg3] |= 0x10;
      hoffset = ((NeoPanelWidth - modetiming->HDisplay) >> 4) - 1;
      moderegs[PanelHorizCenterReg1] = hoffset;
      switch (modetiming->HDisplay){
      case  320:
	moderegs[PanelHorizCenterReg3] = hoffset;
	break;
      case  400:
	moderegs[PanelHorizCenterReg4] = hoffset;
	break;
      case  640:
	moderegs[PanelHorizCenterReg1] = hoffset;
	break;
      case  800:
	moderegs[PanelHorizCenterReg2] = hoffset;
	break;
      case 1024:
	moderegs[PanelHorizCenterReg5] = hoffset;
	break;
      case 1280:
	/* No centering in these modes. */
	break;
      }
    }
    if (modetiming->VDisplay < NeoPanelHeight){
      moderegs[PanelDispCntlReg2] |= 0x01;
      voffset = ((NeoPanelHeight - modetiming->VDisplay) >> 1) - 2;
      moderegs[PanelVertCenterReg2]  = voffset;
      switch (modetiming->VDisplay){
      case 240:
	moderegs[PanelVertCenterReg2]  = voffset;
	break;
      case 300: case 384:
	moderegs[PanelVertCenterReg1]  = voffset;
	break;
      case 480:
	moderegs[PanelVertCenterReg3]  = voffset;
	break;
      case 600:
	moderegs[PanelVertCenterReg4]  = voffset;
	break;
      case 768:
	moderegs[PanelVertCenterReg5]  = voffset;
	break;
      case 1280:
	/* No centering in these modes. */
	break;
      }
    }
  }

#if 0
  /*
   * Calculate the VCLK that most closely matches the requested dot
   * clock.
   */
  NeoCalcVCLK(modetiming->pixelClock, moderegs);
#endif
  /* Since we program the clocks ourselves, always use VCLK3. */
  moderegs[MIS] |= 0x0C;

  return ;
}


static int neo_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

#ifdef DEBUG
    fprintf(stderr, "neo_setmode: %d\n", mode);
#endif
    if ((mode < G640x480x256)||(mode==G720x348x2)) {

	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!neo_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(NEO_TOTAL_REGS);

    neo_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    neo_setregs(moderegs, mode);		/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);


    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void neo_unlock(void)
{
    int temp;

#ifdef DEBUG
    fprintf(stderr, "neo_unlock\n");
#endif
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);
    outw(GRAX, 0x2609); /* Unlock NeoMagic registers */
}

static void neo_lock(void)
{
    int temp;

#ifdef DEBUG
    fprintf(stderr, "neo_lock\n");
#endif
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp | 0x80);
    outw(GRAX, 0x0009); /* Lock NeoMagic registers */
}


/* Indentify chipset, initialize and return non-zero if detected */

static int neo_test(void)
{
    unsigned long buf[64];
    int found=0;
    int _ioperm=0;
    
    if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: cannot get I/O permissions\n");
        exit(1);
      }
    }
    
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    
    if (_ioperm) iopl(0);
    
    if (found == 0){ /* found */
      switch ((buf[0] >> 16) & 0xffff){
      case PCI_CHIP_NM2070: case PCI_CHIP_NM2090: case PCI_CHIP_NM2093:
      case PCI_CHIP_NM2097: case PCI_CHIP_NM2160: case PCI_CHIP_NM2200:
       neo_init(0,0,0);
       return (TRUE);
      }
    }
    return (FALSE);
}


/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void neo_setdisplaystart(int address)
{ 
  int oldExtCRTDispAddr;

#ifdef DEBUG
  fprintf(stderr, "neo_setdisplaystart: 0x%x\n", address);
#endif
  address=address >> 2;
  outw(vgaIOBase + 4, (address & 0x00FF00) | 0x0C);
  outw(vgaIOBase + 4, ((address & 0x00FF) << 8) | 0x0D);

  outb(GRAX, 0x0E);
  oldExtCRTDispAddr = inb(GRAX+1);
  outw(GRAX,
       ((((address >> 16) & 0x07) | (oldExtCRTDispAddr & 0xf8)) << 8) | 0x0E);
}


/* Set logical scanline length (usually multiple of 8) */
/* Cirrus supports multiples of 8, up to 4088 */

static void neo_setlogicalwidth(int width)
{   
    int offset = width >> 3;
 
#ifdef DEBUG
    fprintf(stderr, "neo_setlogicalwidth: %d\n", width);
#endif
    __svgalib_outCR(0x13,offset&0xff);
    outb(GRAX, 0x0F); 
    outb(GRAX+1, width >> 11);
}

static int neo_linear(int op, int param)
{
#ifdef DEBUG
    fprintf(stderr, "neo_linear: %d\n", op);
#endif
    if (op==LINEAR_ENABLE){neo_is_linear=1; return 0;};
    if (op==LINEAR_DISABLE){neo_is_linear=0; return 0;};
    if (op==LINEAR_QUERY_BASE) return neo_linear_base;
    if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
        else return -1;		/* Unknown function. */
}

static int neo_match_programmable_clock(int clock)
{
return clock ;
}

static int neo_map_clock(int bpp, int clock)
{
return clock ;
}

static int neo_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_neo_driverspecs =
{
    neo_saveregs,
    neo_setregs,
#if 1
    neo_unlock,
    neo_lock,
#else
    0,
    0,
#endif
    neo_test,
    neo_init,
    neo_setpage,
    neo_setrdpage,
    neo_setwrpage,
    neo_setmode,
    neo_modeavailable,
    neo_setdisplaystart,
    neo_setlogicalwidth,
    neo_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    neo_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};


/* Initialize chipset (called after detection) */

static int neo_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    int found=0;
    int _ioperm=0;
    int w;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    neo_unlock();
    if (force) {
	neo_memory = par1;
        NeoChipset = par2;
    } else {
      neo_memory = -1;
      NeoChipset = -1;
    };

    if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: cannot get I/O permissions\n");
        exit(1);
      }
    }
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    if (_ioperm) iopl(0);
    neo_linear_base=0;
    if (!found){ /* found */
      neo_linear_base=buf[4]&0xffffff00;
      if (NeoChipset < 0)
	NeoChipset = (buf[0] >> 16) & 0xffff;
      if (neo_memory < 0){
	switch (NeoChipset){
	case PCI_CHIP_NM2070:
	  neo_memory = 896;
	  break;
	case PCI_CHIP_NM2090: case PCI_CHIP_NM2093: case PCI_CHIP_NM2097:
	  neo_memory = 1152;
	  break;
	case PCI_CHIP_NM2160:
	  neo_memory = 2048;
	  break;
	case PCI_CHIP_NM2200:
	  neo_memory = 2560;
	  break;
	default:
	  neo_memory = 0;
	}
      }
    }

    if (__svgalib_driver_report) {
	printf("Using NeoMagic driver, %iKB. ",neo_memory);
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = neo_memory;
    cardspecs->maxPixelClock4bpp = 75000;
    switch (NeoChipset){
    case PCI_CHIP_NM2070:
      cardspecs->maxPixelClock8bpp = 65000;
      cardspecs->maxPixelClock16bpp = 65000;
      cardspecs->maxPixelClock24bpp = 0;
      cardspecs->maxPixelClock32bpp = 0;
      break;
    case PCI_CHIP_NM2090: case PCI_CHIP_NM2093: case PCI_CHIP_NM2097:
      cardspecs->maxPixelClock8bpp = 80000;
      cardspecs->maxPixelClock16bpp = 80000;
      cardspecs->maxPixelClock24bpp = 80000;
      cardspecs->maxPixelClock32bpp = 0;
      break;
    case PCI_CHIP_NM2160:
      cardspecs->maxPixelClock8bpp = 90000;
      cardspecs->maxPixelClock16bpp = 90000;
      cardspecs->maxPixelClock24bpp = 90000;
      cardspecs->maxPixelClock32bpp = 0;
      break;
    case PCI_CHIP_NM2200:
      cardspecs->maxPixelClock8bpp = 110000;
      cardspecs->maxPixelClock16bpp = 110000;
      cardspecs->maxPixelClock24bpp = 110000;
      cardspecs->maxPixelClock32bpp = 0;
      break;
    }

    /* Determine panel width -- used in NeoValidMode. */
    outb(GRAX, 0x20);
    w = inb(GRAX+1);
    switch ((w & 0x18) >> 3){
    case 0x00:
	NeoPanelWidth  = 640;
	NeoPanelHeight = 480;
	break;
    case 0x01:
	NeoPanelWidth  = 800;
#ifdef LIBRETTO100
	NeoPanelHeight = 480;
#else /* LIBRETTO100 */
	NeoPanelHeight = 600;
#endif /* LIBRETTO100 */
	break;
    case 0x02:
	NeoPanelWidth  = 1024;
	NeoPanelHeight = 768;
	break;
    case 0x03:
	NeoPanelWidth  = 1280;
	NeoPanelHeight = 1024;
	break;
    default :
	NeoPanelWidth  = 640;
	NeoPanelHeight = 480;
    }

    cardspecs->flags = INTERLACE_DIVIDE_VERT | CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 2040;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->mapClock = neo_map_clock;
    cardspecs->mapHorizontalCrtc = neo_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=neo_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_neo_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=neo_linear_base;
    __svgalib_linear_mem_size=neo_memory*0x400;
    neo_is_linear = FALSE;

    return 0;
}
