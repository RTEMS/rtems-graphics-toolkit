/*
Matrox G200/G400/G450 chipset driver 

Based on the XFree86 driver.

Tested only on a G450. 

TODO: SDRAM, reference frequency checking.

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
#define G400_TOTAL_REGS (VGA_TOTAL_REGS + 0x50 + 9 + 12)

static void __svgalib_outpal(int i, int r, int g, int b)
{

    outb(PEL_IW,i);
    outb(PEL_D,r);
    outb(PEL_D,g);
    outb(PEL_D,b);
}

enum { ID_G100 = 0, ID_G200, ID_G400, ID_G450 };

static int g400_init(int, int, int);
static void g400_unlock(void);
static void g400_lock(void);

void __svgalib_g400accel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int g400_memory, id;
static int g400_is_linear, g400_linear_base, g400_mmio_base;

static int HasSDRAM;

static CardSpecs *cardspecs;

static int g400_inExt(int i) {
    *(MMIO_POINTER + 0x1fde) = i;
    return *(MMIO_POINTER + 0x1fdf);
}

static void g400_outExt(int i, int d) {
    *(unsigned short *)(MMIO_POINTER + 0x1fde) = (d<<8) | i;
}

static int g400_inDAC(int i) {
    *(MMIO_POINTER + 0x3c00) = i;
    return *(MMIO_POINTER + 0x3c0a);
}

static void g400_outDAC(int i, int d) {
    *(MMIO_POINTER + 0x3c00) = i;
    *(MMIO_POINTER + 0x3c0a) = d;
}

static void g400_setpage(int page)
{
	g400_outExt(4,page);
}

static int __svgalib_g400_inlinearmode(void)
{
return g400_is_linear;
}

/* Fill in chipset specific mode information */

static void g400_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = g400_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 8184;
    modeinfo->startaddressrange = g400_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(g400_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_g400_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

#define PCI_CONF_ADDR  0xcf8
#define PCI_CONF_DATA  0xcfc
 
static int g400_saveregs(unsigned char regs[])
{ 
  int i;

    g400_unlock();		

    outl (PCI_CONF_ADDR, 0x80010000 + 0x40);
    *(unsigned int *)(regs + VGA_TOTAL_REGS + 0x50 + 9 + 0 )  = inl (PCI_CONF_DATA);
    outl (PCI_CONF_ADDR, 0x80010000 + 0x50);
    *(unsigned int *)(regs + VGA_TOTAL_REGS + 0x50 + 9 + 4 )  = inl (PCI_CONF_DATA);
    outl (PCI_CONF_ADDR, 0x80010000 + 0x54);
    *(unsigned int *)(regs + VGA_TOTAL_REGS + 0x50 + 9 + 8 )  = inl (PCI_CONF_DATA);

    for(i=0;i<0x50;i++) regs[VGA_TOTAL_REGS + i]=g400_inDAC(i);
    for(i=0;i<9;i++) regs[VGA_TOTAL_REGS + 0x50 + i]=g400_inExt(i);

    return G400_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void g400_setregs(const unsigned char regs[], int mode)
{  
    int i;

    g400_unlock();		
    for(i=0;i<0x50;i++) {
#if 0
        if( (i> 0x03) && (i!=0x07) && (i!=0x0b) && (i!=0x0f) &&
            (i< 0x13) && (i> 0x17) && (i!=0x1b) && (i!=0x1c) &&
            (i< 0x1f) && (i> 0x29) && (i< 0x30) && (i> 0x37) &&
            (i!=0x39) && (i!=0x3b) && (i!=0x3f) && (i!=0x41) &&
            (i!=0x43) && (i!=0x47) && (i!=0x4b) 
            			 				)
#endif
            g400_outDAC(i,regs[VGA_TOTAL_REGS + i]);
    }

    outl (PCI_CONF_ADDR, 0x80010000 + 0x40);
    outl(PCI_CONF_DATA, *(unsigned int *)(regs + VGA_TOTAL_REGS + 0x50 + 9 ));
    outl (PCI_CONF_ADDR, 0x80010000 + 0x50);
    outl(PCI_CONF_DATA, *(unsigned int *)(regs + VGA_TOTAL_REGS + 0x50 + 9 + 4 ));
    outl (PCI_CONF_ADDR, 0x80010000 + 0x54);
    outl(PCI_CONF_DATA, *(unsigned int *)(regs + VGA_TOTAL_REGS + 0x50 + 9 + 8 ));

    for(i=0;i<9;i++) g400_outExt(i, regs[VGA_TOTAL_REGS + 0x50 + i]);

}


/* Return nonzero if mode is available */

static int g400_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if (IS_IN_STANDARD_VGA_DRIVER(mode))
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (g400_memory * 1024 < info->ydim * info->xbytes)
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

#define MGA_MIN_VCO_FREQ    120000
#define MGA_MAX_VCO_FREQ    250000
#define MGA_MAX_PCLK_FREQ   250000
#define MGA_MAX_MCLK_FREQ   100000
#define MGA_REF_FREQ        27050.0
#define MGA_ALT_REF_FREQ    14318.0
#define MGA_FEED_DIV_MIN    8
#define MGA_FEED_DIV_MAX    127
#define MGA_IN_DIV_MIN      1
#define MGA_IN_DIV_MAX      30
#define MGA_ALT_IN_DIV_MAX      6
#define MGA_POST_DIV_MIN    0
#define MGA_POST_DIV_MAX    3

static double
MGACalcClock ( long f_out, long f_max, int *m, int *n, int *p, int *s )
{
	int best_m=0, best_n=0;
	double f_pll, f_vco;
	double m_err, calc_f, base_freq;
        int mga_in_div_max;
	static double ref = 0.0;

	switch(id) {
            case ID_G400:
            case ID_G450:
                ref = MGA_REF_FREQ;
                mga_in_div_max = MGA_IN_DIV_MAX;
                mga_in_div_max = MGA_ALT_IN_DIV_MAX; /* 31 should be allowed,
                   					but does not work. */
                break;
            case ID_G200:
                ref = MGA_ALT_REF_FREQ;
                mga_in_div_max = MGA_ALT_IN_DIV_MAX;
                break;
            default:
                ref = MGA_ALT_REF_FREQ;
                mga_in_div_max = MGA_ALT_IN_DIV_MAX;
                break;
        }

	/* Make sure that f_min <= f_out <= f_max */
	if ( f_out < ( MGA_MIN_VCO_FREQ / 8))
		f_out = MGA_MIN_VCO_FREQ / 8;

	if ( f_out > f_max )
		f_out = f_max;

	/*
	 * f_pll = f_vco /  (2^p)
	 * Choose p so that MGA_MIN_VCO_FREQ   <= f_vco <= MGA_MAX_VCO_FREQ  
	 * we don't have to bother checking for this maximum limit.
	 */
	f_vco = ( double ) f_out;
	for ( *p = 0; *p < MGA_POST_DIV_MAX && f_vco < MGA_MIN_VCO_FREQ;
								( *p )++ )
		f_vco *= 2.0;

	/* Initial value of calc_f for the loop */
	calc_f = 0;

	base_freq = ref / ( 1 << *p );

	/* Initial amount of error for frequency maximum */
	m_err = f_out;

	/* Search for the different values of ( *m ) */
	for ( *m = MGA_IN_DIV_MIN ;
		*m < mga_in_div_max ; ( *m )++ )
	{
		/* see values of ( *n ) which we can't use */
		for ( *n = MGA_FEED_DIV_MIN;
			*n <= MGA_FEED_DIV_MAX; ( *n )++ )
		{ 
			calc_f = (base_freq * (*n)) / *m ;

		/*
		 * Pick the closest frequency.
		 */
			if (abs( calc_f - f_out ) < m_err ) {
				m_err = abs(calc_f - f_out);
				best_m = *m;
				best_n = *n;
			}
		}
	}
	
	/* Now all the calculations can be completed */
	f_vco = ref * best_n / best_m;

	/* Adjustments for filtering pll feed back */
        switch(id) {
	    case ID_G450:
		*s=0;
		break;
            case ID_G400: 
	        if ( (50000.0 <= f_vco)
	        && (f_vco < 110000.0) )
		        *s = 0;	
	        if ( (110000.0 <= f_vco)
	        && (f_vco < 170000.0) )
		        *s = 1;	
	        if ( (170000.0 <= f_vco)
	        && (f_vco < 240000.0) )
		        *s = 2;	
	        if ( (240000.0 <= f_vco)
	        && (f_vco < 310000.0) )
		        *s = 3;	
                break;
            case ID_G200:
	        if ( (50000.0 <= f_vco)
	        && (f_vco < 100000.0) )
		        *s = 0;	
	        if ( (100000.0 <= f_vco)
	        && (f_vco < 140000.0) )
		        *s = 1;	
	        if ( (140000.0 <= f_vco)
	        && (f_vco < 180000.0) )
		        *s = 2;	
	        if ( (180000.0 <= f_vco)
	        && (f_vco < 250000.0) )
		        *s = 3;	
                break;
        }

	f_pll = f_vco / ( 1 << *p );

	*m = best_m - 1;
	*n = best_n - 1;
	*p = ( 1 << *p ) - 1 ; 

	return f_pll;
}


/*
 * MGASetPCLK - Set the pixel (PCLK) and loop (LCLK) clocks.
 *
 * PARAMETERS
 *   f_pll			IN	Pixel clock PLL frequencly in kHz.
 */
static void 
MGASetPCLK( long f_out, unsigned char *initDAC )
{
	/* Pixel clock values */
	int m, n, p, s;

	/* The actual frequency output by the clock */
	double f_pll;
	long f_max;

	/* Get the maximum pixel clock frequency from the BIOS, 
         * or from a reasonable default
         */
	if ( 0 )
		f_max = (/*MGABios2.PclkMax+*/100) * 1000; /* [ajv - scale it] */
	else
		f_max = MGA_MAX_PCLK_FREQ;

	/* Do the calculations for m, n, and p */
	f_pll = MGACalcClock( f_out, f_max, &m, &n, &p , &s);

	/* Values for the pixel clock PLL registers */
	if((id == ID_G450) && (p==3))p=2;	

	initDAC[ 0x4c ] = ( m & 0x1f );
	initDAC[ 0x4d ] = ( n & 0x7f );
	initDAC[ 0x4e ] = ( (p & 0x07) | ((s & 0x3) << 3) );
}

static void g400_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ 
	static unsigned char initDAC[] = {
	/* 0x00: */	   0,    0,    0,    0,    0,    0, 0x00,    0,
	/* 0x08: */	   0,    0,    0,    0,    0,    0,    0,    0,
	/* 0x08: */	   0,    0,    0,    0,    0,    0,    0,    0,
	/* 0x18: */	0x03,    0, 0x09, 0xFF, 0xBF, 0x20, 0x1F, 0x20,
	/* 0x20: */	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28: */	0x00, 0x00, 0x00, 0x77, 0x04, 0x2D, 0x19, 0x40,
	/* 0x30: */	0x00, 0xB0, 0x00, 0xC2, 0x34, 0x14, 0x02, 0x83,
	/* 0x38: */	0x00, 0x93, 0x00, 0x77, 0x71, 0xDB, 0x02, 0x3A,
	/* 0x40: */	   0,    0,    0,    0,    0,    0,    0,    0,
	/* 0x48: */	   0,    0,    0,    0,    0,    0,    0,    0
	};

	int hd, hs, he, ht, vd, vs, ve, vt, wd;
	int i;
	int weight555 = 0;
        int MGABppShft = 0;

        __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

	switch(modeinfo->bitsPerPixel)
	{
	case 8:
		initDAC[ 0x19 ] = 0;
                initDAC[ 0x1e ] &= ~8;
                MGABppShft=0;
		break;
	case 16:
		initDAC[ 0x19 ] = 2;
                MGABppShft=1;
		if ( modeinfo->greenWeight==5 ) {
			weight555 = 1;
			initDAC[ 0x19 ] = 1;
		}
		break;
	case 24:
                MGABppShft=0;
		initDAC[ 0x19 ] = 3;
		break;
	case 32:
                MGABppShft=2;
		initDAC[ 0x19 ] = 7;
		break;
	}
		
	/*
	 * Here all of the other fields of 'newVS' get filled in.
	 */
	hd = (modetiming->CrtcHDisplay	>> 3)	- 1;
	hs = (modetiming->CrtcHSyncStart	>> 3)	- 1;
	he = (modetiming->CrtcHSyncEnd	>> 3)	- 1;
	ht = (modetiming->CrtcHTotal		>> 3)	- 1;
	vd = modetiming->CrtcVDisplay			- 1;
	vs = modetiming->CrtcVSyncStart		- 1;
	ve = modetiming->CrtcVSyncEnd			- 1;
	vt = modetiming->CrtcVTotal			- 2;
	
	/* HTOTAL & 0xF equal to 0xE in 8bpp or 0x4 in 24bpp causes strange
	 * vertical stripes
	 */  
	if((ht & 0x0F) == 0x0E || (ht & 0x0F) == 0x04)
		ht++;
		
	if (modeinfo->bitsPerPixel == 24)
		wd = (modeinfo->width * 3) >> (4 - MGABppShft);
	else
		wd = modeinfo->width >> (4 - MGABppShft);

	moderegs[VGA_TOTAL_REGS + 0x50 + 0] = 0;
	moderegs[VGA_TOTAL_REGS + 0x50 + 5] = 0;
	
	if (modetiming->flags & INTERLACED)
	{
		moderegs[VGA_TOTAL_REGS + 0x50 + 0] = 0x80;
		moderegs[VGA_TOTAL_REGS + 0x50 + 5] = (hs + he - ht) >> 1;
		wd <<= 1;
		vt &= 0xFFFE;
	}

	moderegs[VGA_TOTAL_REGS + 0x50 + 0]	|= (wd & 0x300) >> 4;
	moderegs[VGA_TOTAL_REGS + 0x50 + 1]	= (((ht - 4) & 0x100) >> 8) |
				((hd & 0x100) >> 7) |
				((hs & 0x100) >> 6) |
				(ht & 0x40);
	moderegs[VGA_TOTAL_REGS + 0x50 + 2]	= ((vt & 0x400) >> 10) |
				((vt & 0x800) >> 10) |
				((vd & 0x400) >> 8) |
				((vd & 0x400) >> 7) |
				((vd & 0x800) >> 7) |
				((vs & 0x400) >> 5) |
				((vs & 0x800) >> 5);
	if (modeinfo->bitsPerPixel == 24)
		moderegs[VGA_TOTAL_REGS + 0x50 + 3]	= (((1 << MGABppShft) * 3) - 1) | 0x80;
	else
		moderegs[VGA_TOTAL_REGS + 0x50 + 3]	= ((1 << MGABppShft) - 1) | 0x80;

	moderegs[VGA_TOTAL_REGS + 0x50 + 3] &= 0xE7;	/* ajv - bits 4-5 MUST be 0 or bad karma happens */

	moderegs[VGA_TOTAL_REGS + 0x50 + 4]	= 0;
		
	moderegs[0]	= ht - 4;
	moderegs[1]	= hd;
	moderegs[2]	= hd;
	moderegs[3]	= (ht & 0x1F) | 0x80;
	moderegs[4]	= hs;
	moderegs[5]	= ((ht & 0x20) << 2) | (he & 0x1F);
	moderegs[6]	= vt & 0xFF;
	moderegs[7]	= ((vt & 0x100) >> 8 ) |
				((vd & 0x100) >> 7 ) |
				((vs & 0x100) >> 6 ) |
				((vd & 0x100) >> 5 ) |
				0x10 |
				((vt & 0x200) >> 4 ) |
				((vd & 0x200) >> 3 ) |
				((vs & 0x200) >> 2 );
	moderegs[9]	= ((vd & 0x200) >> 4) | 0x40; 
	moderegs[16] = vs & 0xFF;
	moderegs[17] = (ve & 0x0F) | 0x20;
	moderegs[18] = vd & 0xFF;
	moderegs[19] = wd & 0xFF;
	moderegs[21] = vd & 0xFF;
	moderegs[22] = (vt + 1) & 0xFF;

	if (modetiming->flags & DOUBLESCAN)
		moderegs[9] |= 0x80;
    
	moderegs[59] |= 0x0C;

    	switch(id) {
            case ID_G200:
                initDAC[ 0x2c ] = 0x04;
                initDAC[ 0x2d ] = 0x2d;
                initDAC[ 0x2e ] = 0x19;
		if(HasSDRAM)
                     *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 0) = 0x40499121;
                else *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 0) = 0x4049cd21;
                *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 4) = 0x00008000;
                *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 8) = 0x0190a421;
                break;
            case ID_G400:
            case ID_G450:
                initDAC[ 0x2c ] = 0x05;
                initDAC[ 0x2d ] = 0x23;
                initDAC[ 0x2e ] = 0x40;
                *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 0) = 0x40341160;
                *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 4) = 0x01003000;
                *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 8) = 0x0190a421;
		if(!HasSDRAM)
		   *(unsigned int *)(moderegs + VGA_TOTAL_REGS + 0x50 + 9 + 0) |= (1 << 14);
                break;
        }

	MGASetPCLK( modetiming->pixelClock , initDAC);

	for (i = 0; i < sizeof(initDAC); i++)
	{
	    moderegs[VGA_TOTAL_REGS + i] = initDAC[i]; 
	}

    return ;
}


static int g400_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;
    int i;

    if (IS_IN_STANDARD_VGA_DRIVER(mode)) {
	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!g400_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(G400_TOTAL_REGS);

    g400_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */

    g400_setregs(moderegs, mode);		/* Set extended regs. */

    if(mode>=G640x480x256)
	switch(modeinfo->bitsPerPixel) {
            case 16:
                for(i=0;i<256;i++) __svgalib_outpal(i,i<<3,i<<(8-modeinfo->greenWeight),i<<3);
                break;
            case 24:
            case 32:
                for(i=0;i<256;i++) __svgalib_outpal(i,i,i,i);
                break;
        }

    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);

    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void g400_unlock(void)
{
    __svgalib_outcrtc(0x11, __svgalib_incrtc(0x11) &0x7f);
    g400_outExt(3, g400_inExt(3) & 0x7f);
}

static void g400_lock(void)
{
    __svgalib_outcrtc(0x11, __svgalib_incrtc(0x11)&0x7f);
    g400_outExt(3, g400_inExt(3) | 0x80);
}


#define VENDOR_ID 0x102b

/* Indentify chipset, initialize and return non-zero if detected */

static int g400_test(void)
{
    int found, id;
    unsigned long buf[64];
    
    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    
    if(found) return 0;
    
    id=(buf[0]>>16)&0xffff;
    
    if((id==0x520)||(id==0x521)||(id==0x525)||(id==0x1000)||(id==0x1001)){
       g400_init(0,0,0);
       return 1;
    };
    return 0;
}


/* Set display start address (not for 16 color modes) */

static void g400_setdisplaystart(int address)
{ 
  address=address >> 2;
  __svgalib_outcrtc(0x0c, (address & 0xFF00)>>8);
  __svgalib_outcrtc(0x0d,  address & 0x00FF);
  g400_outExt(0, (g400_inExt(0)&0xb0) | ((address&0xf0000)>>16) | ((address&0x100000)>>14));
}


/* Set logical scanline length (usually multiple of 8) */

static void g400_setlogicalwidth(int width)
{   
    int offset = width >> 3;
 
    __svgalib_outcrtc(0x13,offset&0xff);
    g400_outExt(0,(g400_inExt(0)&0xcf) | ((offset&0x300)>>4));
}

static int g400_linear(int op, int param)
{
    if (op==LINEAR_ENABLE){g400_is_linear=1; return 0;};
    if (op==LINEAR_DISABLE){g400_is_linear=0; return 0;};
    if (op==LINEAR_QUERY_BASE) return g400_linear_base;
    if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
        else return -1;		/* Unknown function. */
}

static int g400_match_programmable_clock(int clock)
{
return clock ;
}

static int g400_map_clock(int bpp, int clock)
{
return clock ;
}

static int g400_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_g400_driverspecs =
{
    g400_saveregs,
    g400_setregs,
    g400_unlock,
    g400_lock,
    g400_test,
    g400_init,
    g400_setpage,
    NULL,
    NULL,
    g400_setmode,
    g400_modeavailable,
    g400_setdisplaystart,
    g400_setlogicalwidth,
    g400_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    g400_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int g400_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    int found=0;
    int pci_id;
    char *ids[]={"G100", "G200", "G400", "G450"};

    if (force) {
	g400_memory = par1;
    } else {

    };

    iopl(3); /* Why is it needed? */

    found=__svgalib_pci_find_vendor_vga(VENDOR_ID,buf,0);
    
    if(found) {
        printf("Error: Must use Matrox driver, but no card found\n");
        exit(1);
    }

    pci_id=(buf[0]>>16)&0xffff;
	
    switch(pci_id) {
        case 0x525:
		if((buf[11]&0xffff0000) == 0x07c00000)
			id = ID_G450; else id = ID_G400;
		break;
	case 0x520:
	case 0x521:
		id = ID_G400;
		break;
	default:
		id = ID_G100;
    }

    g400_linear_base = buf[4]&0xffffff00;
    g400_mmio_base = buf[5]&0xffffff00;

/* those need to be fixed */
    g400_memory = 8192;
    HasSDRAM=(buf[0x10]&(1<<14))?0:1;
    
    if (__svgalib_driver_report) {
	printf("Using Matrox %s driver, %iKB S%cRAM.\n",ids[id],
           	g400_memory, HasSDRAM?'D':'G');
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = g400_memory;
    cardspecs->maxPixelClock4bpp = 75000;
    switch(id) {
        default:
            cardspecs->maxPixelClock8bpp = 250000;
            cardspecs->maxPixelClock16bpp = 250000;
            cardspecs->maxPixelClock24bpp = 250000;
            cardspecs->maxPixelClock32bpp = 250000;
            break;
    }
    cardspecs->flags = INTERLACE_DIVIDE_VERT | CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 4095;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->mapClock = g400_map_clock;
    cardspecs->mapHorizontalCrtc = g400_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=g400_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_g400_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=g400_linear_base;
    __svgalib_linear_mem_size=g400_memory*0x400;
    __svgalib_mmio_base=g400_mmio_base;
    __svgalib_mmio_size=16384;
    return 0;
}
