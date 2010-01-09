/*
Skeleton chipset driver 
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

#define SKREG_SAVE(i) (VGA_TOTAL_REGS+i)
#define SK_TOTAL_REGS (VGA_TOTAL_REGS + 100)

static int sk_init(int, int, int);
static void sk_unlock(void);
static void sk_lock(void);

void __svgalib_skaccel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int sk_memory;
static int sk_is_linear, sk_linear_base;

static CardSpecs *cardspecs;

static void sk_setpage(int page)
{
}

static int __svgalib_sk_inlinearmode(void)
{
return sk_is_linear;
}

/* Fill in chipset specific mode information */

static void sk_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = sk_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 4088;
    modeinfo->startaddressrange = sk_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(sk_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_sk_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

static int sk_saveregs(unsigned char regs[])
{ 
  int i;

    sk_unlock();		
    return SK_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void sk_setregs(const unsigned char regs[], int mode)
{  
    int i;

    sk_unlock();		

}


/* Return nonzero if mode is available */

static int sk_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (sk_memory * 1024 < info->ydim * info->xbytes)
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

/* Local, called by sk_setmode(). */

static void sk_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ /* long k; */
    int tmp, tmptot, tmpss, tmpse, tmpbs, tmpbe, k;
    int offset;
   
    __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

    return ;
}


static int sk_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256)||(mode==G720x348x2)) {

	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!sk_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(SK_TOTAL_REGS);

    sk_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    sk_setregs(moderegs, mode);		/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);


    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void sk_unlock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);
    
}

static void sk_lock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);   
}


#define VENDOR_ID 0xf00
#define CARD_ID 0xba7

/* Indentify chipset, initialize and return non-zero if detected */

static int sk_test(void)
{
    int _ioperm;
    
    if (getenv("IOPERM") == NULL) {
      _ioperm=1;
      if (iopl(3) < 0) {
        printf("svgalib: cannot get I/O permissions\n");
        exit(1);
      }
    }
    
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    
    if (_ioperm) iopl(0);
    
    if(!found&&((buf[0]>>16)&0xffff==CARD_ID)){
       sk_init(0,0,0);
       return 1;
    };
}


/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void sk_setdisplaystart(int address)
{ 
  address=address >> 2;
  outw(CRT_IC, (address & 0x00FF00) | 0x0C);
  outw(CRT_IC, ((address & 0x00FF) << 8) | 0x0D);
}


/* Set logical scanline length (usually multiple of 8) */
/* Cirrus supports multiples of 8, up to 4088 */

static void sk_setlogicalwidth(int width)
{   
    int offset = width >> 3;
 
    __svgalib_outCR(0x13,offset&0xff);
}

static int sk_linear(int op, int param)
{
    if (op==LINEAR_ENABLE){sk_is_linear=1; return 0;};
    if (op==LINEAR_DISABLE){sk_is_linear=0; return 0;};
    if (op==LINEAR_QUERY_BASE) return sk_linear_base;
    if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
        else return -1;		/* Unknown function. */
}

static int sk_match_programmable_clock(int clock)
{
return clock ;
}

static int sk_map_clock(int bpp, int clock)
{
return clock ;
}

static int sk_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_sk_driverspecs =
{
    sk_saveregs,
    sk_setregs,
    sk_unlock,
    sk_lock,
    sk_test,
    sk_init,
    sk_setpage,
    NULL,
    NULL,
    sk_setmode,
    sk_modeavailable,
    sk_setdisplaystart,
    sk_setlogicalwidth,
    sk_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    sk_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int sk_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    int found=0;
    int _ioperm=0;

    sk_unlock();
    if (force) {
	sk_memory = par1;
        sk_chiptype = par2;
    } else {

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
    sk_linear_base=0;
    if (!found){
       sk_linear_base=buf[4]&0xffffff00;
    };

    if (__svgalib_driver_report) {
	printf("Using SK driver, %iKB. ",sk_memory);
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = sk_memory;
    cardspecs->maxPixelClock4bpp = 75000;	
    cardspecs->maxPixelClock8bpp = 160000;	
    cardspecs->maxPixelClock16bpp = 160000;	
    cardspecs->maxPixelClock24bpp = 160000;
    cardspecs->maxPixelClock32bpp = 160000;
    cardspecs->flags = INTERLACE_DIVIDE_VERT | CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 2040;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->mapClock = sk_map_clock;
    cardspecs->mapHorizontalCrtc = sk_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=sk_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_sk_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=sk_linear_base;
    __svgalib_linear_mem_size=sk_memory*0x400;
    return 0;
}
