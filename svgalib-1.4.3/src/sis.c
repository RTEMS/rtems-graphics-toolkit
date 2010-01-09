/*
SIS chipset driver 

Matan Ziv-Av <matan@svgalib.org>

Fixes for 530 and for vesafb by Marcelo de Paula Bezerra 
<mosca@internetaddress.com>.


This driver is based on the XFree86 sis driver, written by

 Alan Hourihane

and modified by
 Xavier Ducoin,
 Mike Chapman,
 Juanjo Santamarta,
 Mitani Hiroshi,
 David Thomas. 

And on the documentation availbale from sis's web pages (not many
chipset companies still do this).

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

#define SISREG_SAVE(i) (VGA_TOTAL_REGS+i)
#define SIS_TOTAL_REGS (VGA_TOTAL_REGS + 0x48 - 5)

#define XR(i) (VGA_TOTAL_REGS+i-5)

#define PCI_CHIP_SG86C201       0x0001
#define PCI_CHIP_SG86C202       0x0002
#define PCI_CHIP_SG86C205       0x0205
#define PCI_CHIP_SG86C215       0x0215
#define PCI_CHIP_SG86C225       0x0225
#define PCI_CHIP_SIS5598        0x0200
#define PCI_CHIP_SIS5597        0x0200
#define PCI_CHIP_SIS6326        0x6326
#define PCI_CHIP_SIS530        0x6306
#define PCI_CHIP_SIS300         0x0300
#define PCI_CHIP_SIS540         0x5300
#define PCI_CHIP_SIS630         0x6300

#define ClockReg	XR(0x07) 
#define DualBanks	XR(0x0B)
#define BankReg		XR(0x06)
#define CRTCOff		XR(0x0A)
#define DispCRT		XR(0x27)
#define Unknown		XR(0x08)
#define LinearAddr0	XR(0x20)
#define LinearAddr1	XR(0x21)

enum { SIS_86C201=0, SIS_86C202, SIS_86C205, SIS_86C215, SIS_86C225,
       SIS_5597, SIS_5598, SIS_6326, SIS_530 };

#define write_xr(num,val) {outb(SEQ_I, num);outb(SEQ_D, val);}
#define read_xr(num,var) {outb(SEQ_I, num);var=inb(SEQ_D);} 

static int sis_init(int, int, int);
static void sis_unlock(void);
static void sis_lock(void);
static void sisClockLoad(int, unsigned char *);
static void sis_CPUthreshold(int,int,int *,int *);

void __svgalib_sisaccel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int sis_memory;
static int sis_is_linear, sis_linear_base;
static int sis_chiptype;

static int sis_edo_vram = 0, sis_host_bus = 0, sis_fast_vram = 0,
           sis_pci_burst_on = 0, sis_pci_burst_off = 0;

static CardSpecs *cardspecs;

static void sis_setpage(int page)
{
        char tmp;

        read_xr(0x0b,tmp);
        if (tmp&8) {
            outb(0x3cb,page);
            outb(0x3cd,page);
        } else
            outb(0x3cd,page|(page<<4));
}

static int __svgalib_sis_inlinearmode(void)
{
return sis_is_linear;
}

/* Fill in chipset specific mode information */

static void sis_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = sis_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 4088;
    modeinfo->startaddressrange = sis_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(sis_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_sis_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

static int sis_saveregs(unsigned char regs[])
{ 
    int i;

    sis_unlock();
    
    regs[XR(0x40)] = inb(0x3cb);
    regs[XR(0x41)] = inb(0x3cd);	

    if(sis_chiptype==SIS_5597)
       for(i=0x38; i<0x3a;i++)read_xr(i,regs[XR(i)]);

    if(sis_chiptype==SIS_6326)
       for(i=0x38; i<0x3d;i++)read_xr(i,regs[XR(i)]);

    if(sis_chiptype==SIS_530)
       for(i=0x38; i<0x40 ; i++)read_xr(i,regs[XR(i)]);

    /* get all the clocks */
    if (sis_chiptype==SIS_5597 || sis_chiptype==SIS_6326 || sis_chiptype==SIS_530) {
       i = regs[XR(0x38)] & 0xfc;
       write_xr(0x38,i|1);
       read_xr(0x13,regs[XR(0x42)]);
       read_xr(0x2a,regs[XR(0x43)]);
       read_xr(0x2b,regs[XR(0x44)]);
       write_xr(0x38,i|2);
       read_xr(0x13,regs[XR(0x45)]);
       read_xr(0x2a,regs[XR(0x46)]);
       read_xr(0x2b,regs[XR(0x47)]);
       write_xr(0x38,i); /* Make sure that the loop gets the internal VCLK registers */
    }

    for(i=5; i<0x38;i++)read_xr(i,regs[XR(i)]);
    
    if (sis_chiptype==SIS_5597 || sis_chiptype==SIS_6326 || sis_chiptype==SIS_530)
       write_xr(0x38,regs[XR(0x38)]);

    return SIS_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void sis_setregs(const unsigned char regs[], int mode)
{  
    int i;

    sis_unlock();
    
    outb(0x3cb,regs[XR(0x40)]);
    outb(0x3cd,regs[XR(0x41)]);

    /* set all the clocks */
    if (sis_chiptype==SIS_5597 || sis_chiptype==SIS_6326 || sis_chiptype==SIS_530) {
       i = regs[XR(0x38)] & 0xfc;
       write_xr(0x38,i|1);
       write_xr(0x13,regs[XR(0x42)]);
       write_xr(0x2a,regs[XR(0x43)]);
       write_xr(0x2b,regs[XR(0x44)]);
       write_xr(0x38,i|2);
       write_xr(0x13,regs[XR(0x45)]);
       write_xr(0x2a,regs[XR(0x46)]);
       write_xr(0x2b,regs[XR(0x47)]);
       write_xr(0x38,i); /* Make sure that the loop puts the internal VCLK registers */
    }

    for(i=6; i<0x38;i++)write_xr(i,regs[XR(i)]);
    
    if(sis_chiptype==SIS_5597)
       for(i=0x38; i<0x3a;i++)write_xr(i,regs[XR(i)]);
 
    if(sis_chiptype==SIS_6326)
       for(i=0x38; i<0x3d;i++)write_xr(i,regs[XR(i)]);

    if(sis_chiptype==SIS_530)
       for(i=0x38; i<0x40;i++)write_xr(i,regs[XR(i)]);
}


/* Return nonzero if mode is available */

static int sis_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (sis_memory * 1024 < info->ydim * info->xbytes)
	return 0;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);
    
    if(modeinfo->bytesPerPixel==4){
        free(modeinfo);
        return 0;
    };

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

/* Local, called by sis_setmode(). */

static void sis_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ /* long k; */
    int offset;
    
    __svgalib_setup_VGA_registers(moderegs, modetiming, modeinfo);

    sis_saveregs(moderegs);
    
    sisClockLoad(modetiming->pixelClock,moderegs);
    offset = modetiming->CrtcHDisplay >> 
             ((modetiming->flags & INTERLACED) ? 2 : 3);

    moderegs[ATT+16] = 0x01;
    moderegs[20] = 0x40;
    moderegs[23] = 0xA3;
    
    moderegs[ATT+0x10] = 0x01;   /* mode */
    moderegs[ATT+0x11] = 0x00;   /* overscan (border) color */
    moderegs[ATT+0x12] = 0x0F;   /* enable all color planes */
    moderegs[ATT+0x13] = 0x00;   /* horiz pixel panning 0 */

    if ( (modeinfo->bitsPerPixel == 16) || (modeinfo->bitsPerPixel == 24) )
	    moderegs[GRA+0x05] = 0x00;    /* normal read/write mode */

    if (modeinfo->bitsPerPixel == 16) {
	    offset <<= 1;	       /* double the width of the buffer */
    } else if (modeinfo->bitsPerPixel == 24) {
	    offset += offset << 1;
    } 

    moderegs[BankReg] = 0x02;
    moderegs[DualBanks] = 0x00; 

    if ( sis_is_linear ) {
	    moderegs[BankReg] |= 0x80;  	/* enable linear mode addressing */
	    moderegs[LinearAddr0] = (sis_linear_base & 0x07f80000) >> 19 ; 
            moderegs[LinearAddr1] = ((sis_linear_base & 0xf8000000) >> 27) |
		                (0x60) ; /* Enable Linear with max 4 mb*/
    } else moderegs[DualBanks] |= 0x08;

    if (modeinfo->bitsPerPixel == 16) {
	    if (modeinfo->greenWeight == 5)
		moderegs[BankReg] |= 0x04;	/* 16bpp = 5-5-5 */
	    else
		moderegs[BankReg] |= 0x08;	/* 16bpp = 5-6-5 */
    }

    if (modeinfo->bitsPerPixel == 24) {
	    moderegs[BankReg] |= 0x10;
            moderegs[DualBanks] |= 0x80;
    }

    moderegs[0x13] = offset & 0xFF;
    moderegs[CRTCOff] = ((offset & 0xF00) >> 4) | 
	    (((modetiming->CrtcVTotal-2) & 0x400) >> 10 ) |
            (((modetiming->CrtcVDisplay-1) & 0x400) >> 9 ) |
            (((modetiming->CrtcVSyncStart) & 0x400) >> 8 ) |
            (((modetiming->CrtcVSyncStart) & 0x400) >> 7 ) ;
	
    if (modetiming->flags & INTERLACED)
		moderegs[BankReg] |= 0x20;
    
    if ((sis_chiptype == SIS_5597) || 
        (sis_chiptype == SIS_6326) || 
        (sis_chiptype == SIS_530)) {
               moderegs[XR(0x0C)] |= 0x20; /* readahead cache */
               moderegs[XR(0x07)] |= 0x80; /* combine FIFOs */
    }

    if((sis_chiptype == SIS_530) || (sis_chiptype==SIS_5597)) {
	moderegs[XR(0x0c)] |= 0x80; /* 32 bit memory access */
        moderegs[XR(0x26)]&=0xfe; /* zero bit 20 of start address */
    };

    /* makes SR27 d[3:1]=0; If yes, I believe this is the offset for the 
       scroll, or somthing like that... */
    moderegs[XR(0x27)]&=0xf0; 

    {
        int CRT_ENGthreshold,CRT_CPUthresholdLow, CRT_CPUthresholdHigh;
    
        CRT_ENGthreshold = 0x1F; 
        sis_CPUthreshold(modetiming->pixelClock, modeinfo->bitsPerPixel,
			     &CRT_CPUthresholdLow, &CRT_CPUthresholdHigh);
        moderegs[XR(0x08)] = (CRT_ENGthreshold & 0x0F) | 
	    (CRT_CPUthresholdLow & 0x0F)<<4 ;
        moderegs[XR(0x09)] &= 0xF0;  /* Yeou */
        moderegs[XR(0x09)] |= (CRT_CPUthresholdHigh & 0x0F); /* Yeou */
    
        if (sis_chiptype == SIS_530) {
	        /* CRT/CPU/Engine Threshold Bit[4] */
	        moderegs[XR(0x3F)] &= 0xE3;
                moderegs[XR(0x3F)] |= ((CRT_CPUthresholdHigh & 0x10) |
				        ((CRT_ENGthreshold & 0x10) >> 1) |
				        ((CRT_CPUthresholdLow & 0x10) >> 2));
        }
    }
    
    moderegs[59]=0x6f;

    return ;
}


static int sis_setmode(int mode, int prv_mode)
{
    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;
    int tmp, var;

    if ((mode < G640x480x256)||(mode==G720x348x2)) {
        if(sis_chiptype >= SIS_5597) { 
		/* Work Around for vesafb case */
                read_xr(0x06,tmp);
                tmp&=~(0x1E); /* Depth registers */
                write_xr(0x06,tmp);
		/* program the 25 and 28 Mhz clocks */
		if (sis_chiptype==SIS_5597 || sis_chiptype==SIS_6326 || sis_chiptype==SIS_530) {
                   read_xr(0x38,tmp);
                   tmp &= 0xfc;
                   write_xr(0x38,tmp|1);
	           read_xr(0x13,var);
                   write_xr(0x13,var|0x40);
                   write_xr(0x2a,0x1b);
                   write_xr(0x2b,0xe1);
                   write_xr(0x38,tmp|2);
                   read_xr(0x13,var);
                   write_xr(0x13,var|0x40);
                   write_xr(0x2a,0x4e);
                   write_xr(0x2b,0xe4);
                   write_xr(0x38,tmp);
		}
        }
 
	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!sis_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(SIS_TOTAL_REGS);

    sis_initializemode(moderegs, modetiming, modeinfo, mode);


    free(modetiming);

    __svgalib_setregs(moderegs);	/* Set standard regs. */
    sis_setregs(moderegs, mode);		/* Set extended regs. */
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);

    free(modeinfo);
    return 0;
}


/* Unlock chipset-specific registers */

static void sis_unlock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);

    outw(SEQ_I,0x8605);
    
}

static void sis_lock(void)
{
    int vgaIOBase, temp;

    vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
    outb(vgaIOBase + 4, 0x11);
    temp = inb(vgaIOBase + 5);
    outb(vgaIOBase + 5, temp & 0x7F);   

    outw(SEQ_I,0x0005);

}

#define VENDOR_ID 0x1039

/* Indentify chipset, initialize and return non-zero if detected */

static int sis_test(void)
{
    int _ioperm=0, found;
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
    
    if(!found&&(
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SG86C201)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SG86C202)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SG86C205)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SG86C215)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SG86C225)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS5597)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS5598)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS530)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS540)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS630)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS300)||
       (((buf[0]>>16)&0xffff)==PCI_CHIP_SIS6326)
       )){
       sis_init(0,0,0);
       return 1;
    };
    return 0;
}


/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void sis_setdisplaystart(int address)
{ 
  int temp;
  address=address >> 2;
  outw(CRT_IC, (address & 0x00FF00) | 0x0C);
  outw(CRT_IC, ((address & 0x00FF) << 8) | 0x0D);

  read_xr(0x27,temp);
  temp &= 0xf0;
  temp |= (address&0xf0000)>>16;
  write_xr(0x27,temp);
}


/* Set logical scanline length (usually multiple of 8) */
/* Cirrus supports multiples of 8, up to 4088 */

static void sis_setlogicalwidth(int width)
{   
    int offset = width >> 3;
    int temp;
    
    __svgalib_outCR(0x13,offset&0xff);
    read_xr(0x0a,temp);
    temp &= 0xf;
    temp |= (offset&0xf00)>>4;
    write_xr(0x0a,temp);
}

static int sis_linear(int op, int param)
{
    if (op==LINEAR_ENABLE){
        int temp;
        
        sis_is_linear=1; 
        read_xr(6,temp);
        temp |=0x80;
        write_xr(0x20,(sis_linear_base & 0x07f80000) >> 19) ; 
        write_xr(0x21,((sis_linear_base & 0xf8000000) >> 27) |
		                (0x60)) ; /* Enable Linear with max 4 mb*/
        write_xr(6,temp);
        return 0;
    };
    if (op==LINEAR_DISABLE){
        int temp;

        sis_is_linear=0; 
        read_xr(6,temp);
        temp &=0x7f;
        write_xr(0x20,0) ; 
        write_xr(0x21,0) ; 
        write_xr(6,temp);
        return 0;
    };
    if (op==LINEAR_QUERY_BASE) return sis_linear_base;
    if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
        else return -1;		/* Unknown function. */
}

static int sis_match_programmable_clock(int clock)
{
return clock ;
}

static int sis_map_clock(int bpp, int clock)
{
return clock ;
}

static int sis_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_sis_driverspecs =
{
    sis_saveregs,
    sis_setregs,
    sis_unlock,
    sis_lock,
    sis_test,
    sis_init,
    sis_setpage,
    NULL,
    NULL,
    sis_setmode,
    sis_modeavailable,
    sis_setdisplaystart,
    sis_setlogicalwidth,
    sis_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    sis_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int sis_init(int force, int par1, int par2)
{
    unsigned long buf[64];
    int found=0;
    int _ioperm=0;

    sis_unlock();
    if (force) {
	sis_memory = par1;
        sis_chiptype = par2;
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
    sis_linear_base=0;
    sis_chiptype=0;
    if (!found){
       sis_linear_base=buf[4]&0xffffff00;
       switch((buf[0]>>16)&0xffff) {
            case PCI_CHIP_SG86C201: sis_chiptype=SIS_86C201; break;
            case PCI_CHIP_SG86C202: sis_chiptype=SIS_86C202; break;
            case PCI_CHIP_SG86C205: sis_chiptype=SIS_86C205; break;
            case PCI_CHIP_SG86C215: sis_chiptype=SIS_86C215; break;
            case PCI_CHIP_SG86C225: sis_chiptype=SIS_86C225; break;
            case PCI_CHIP_SIS5597: sis_chiptype=SIS_5597; break;
            case PCI_CHIP_SIS300:
            case PCI_CHIP_SIS540:
            case PCI_CHIP_SIS630:
            case PCI_CHIP_SIS530: sis_chiptype=SIS_530; break;
/*            case PCI_CHIP_SIS5598: sis_chiptype=SIS_5598; break;*/
            case PCI_CHIP_SIS6326: sis_chiptype=SIS_6326; break;
       };
    };

    if(sis_memory==0){
        if ((sis_chiptype == SIS_6326)||(sis_chiptype==SIS_530)){
            int temp;
            read_xr(0x0C,temp);
            temp >>= 1;
            sis_memory=1024<<(temp&0x03);
        };		
        if ( sis_chiptype == SIS_5597) {
            int temp,bsiz;
		/* Because 5597 shares main memory, 
		   I test for BIOS CONFIGURED memory.*/

            read_xr(0x0C,temp);
            bsiz = (temp >> 1) & 3;

            read_xr(0x2F,temp);
            temp &= 7;
            temp++;
            if (bsiz > 0) temp = temp << 1;
            sis_memory = 256 * temp;

        };
        if(sis_chiptype<SIS_5597) {
            int temp;
            read_xr(0x0F,temp);

            sis_memory=1024<<(temp&0x03);
        };
    };
    if (__svgalib_driver_report) {
	printf("Using SIS driver, %iKB. Chiptype=%i\n",sis_memory,sis_chiptype);
    };

    cardspecs = malloc(sizeof(CardSpecs));
    cardspecs->videoMemory = sis_memory;
    cardspecs->maxPixelClock4bpp = 75000;	
    cardspecs->maxPixelClock8bpp = 135000;	
    cardspecs->maxPixelClock16bpp = 135000;	
    cardspecs->maxPixelClock24bpp = 135000;
    cardspecs->maxPixelClock32bpp = 0;
    cardspecs->flags = INTERLACE_DIVIDE_VERT | CLOCK_PROGRAMMABLE;
    cardspecs->maxHorizontalCrtc = 2040;
    cardspecs->maxPixelClock4bpp = 0;
    cardspecs->nClocks =0;
    cardspecs->mapClock = sis_map_clock;
    cardspecs->mapHorizontalCrtc = sis_map_horizontal_crtc;
    cardspecs->matchProgrammableClock=sis_match_programmable_clock;
    __svgalib_driverspecs = &__svgalib_sis_driverspecs;
    __svgalib_banked_mem_base=0xa0000;
    __svgalib_banked_mem_size=0x10000;
    __svgalib_linear_mem_base=sis_linear_base;
    __svgalib_linear_mem_size=sis_memory*0x400;
    return 0;
}

static void
sisCalcClock(int Clock, int max_VLD, unsigned int *vclk)
{
    int M, N, P, PSN, VLD, PSNx;

    int bestM=0, bestN=0, bestP=0, bestPSN=0, bestVLD=0;
    double bestError, abest = 42.0, bestFout;
    double target;

    double Fvco, Fout;
    double error, aerror;

    /*
     *	fd = fref*(Numerator/Denumerator)*(Divider/PostScaler)
     *
     *	M 	= Numerator [1:128] 
     *  N 	= DeNumerator [1:32]
     *  VLD	= Divider (Vco Loop Divider) : divide by 1, 2
     *  P	= Post Scaler : divide by 1, 2, 3, 4
     *  PSN     = Pre Scaler (Reference Divisor Select) 
     * 
     * result in vclk[]
     */
#define Midx 	0
#define Nidx 	1
#define VLDidx 	2
#define Pidx 	3
#define PSNidx 	4
#define Fref 14318180
/* stability constraints for internal VCO -- MAX_VCO also determines 
 * the maximum Video pixel clock */
#define MIN_VCO Fref
#define MAX_VCO 135000000
#define MAX_VCO_5597 353000000
#define MAX_PSN 0 /* no pre scaler for this chip */
#define TOLERANCE 0.01	/* search smallest M and N in this tolerance */
  
  int M_min = 2;
  int M_max = 128;
  
/*  abest=10000.0; */
 
  target = Clock * 1000;
 
     if ((sis_chiptype == SIS_5597) || (sis_chiptype == SIS_6326)){
 	int low_N = 2;
 	int high_N = 5;
 	int PSN = 1;
 
 	P = 1;
 	if (target < MAX_VCO_5597 / 2)
 	    P = 2;
 	if (target < MAX_VCO_5597 / 3)
 	    P = 3;
 	if (target < MAX_VCO_5597 / 4)
 	    P = 4;
 	if (target < MAX_VCO_5597 / 6)
 	    P = 6;
 	if (target < MAX_VCO_5597 / 8)
 	    P = 8;
 
 	Fvco = P * target;
 
 	for (N = low_N; N <= high_N; N++){
 	    double M_desired = Fvco / Fref * N;
 	    if (M_desired > M_max * max_VLD)
 		continue;
 
 	    if ( M_desired > M_max ) {
 		M = M_desired / 2 + 0.5;
 		VLD = 2;
 	    } else {
 		M = Fvco / Fref * N + 0.5;
 		VLD = 1;
 	    };
 
 	    Fout = (double)Fref * (M * VLD)/(N * P);
 
 	    error = (target - Fout) / target;
 	    aerror = (error < 0) ? -error : error;
/* 	    if (aerror < abest && abest > TOLERANCE) {*/
 	    if (aerror < abest) {
 	        abest = aerror;
 	        bestError = error;
 	        bestM = M;
 	        bestN = N;
 	        bestP = P;
 	        bestPSN = PSN;
 	        bestVLD = VLD;
 	        bestFout = Fout;
 	    }
 	}
     }
     else {
         for (PSNx = 0; PSNx <= MAX_PSN ; PSNx++) {
 	    int low_N, high_N;
 	    double FrefVLDPSN;
 
 	    PSN = !PSNx ? 1 : 4;
 
 	    low_N = 2;
 	    high_N = 32;
 
 	    for ( VLD = 1 ; VLD <= max_VLD ; VLD++ ) {
 
 	        FrefVLDPSN = (double)Fref * VLD / PSN;
 	        for (N = low_N; N <= high_N; N++) {
 		    double tmp = FrefVLDPSN / N;
 
 		    for (P = 1; P <= 4; P++) {	
 		        double Fvco_desired = target * ( P );
 		        double M_desired = Fvco_desired / tmp;
 
 		        /* Which way will M_desired be rounded?  
 		         *  Do all three just to be safe.  
 		         */
 		        int M_low = M_desired - 1;
 		        int M_hi = M_desired + 1;
 
 		        if (M_hi < M_min || M_low > M_max)
 			    continue;
 
 		        if (M_low < M_min)
 			    M_low = M_min;
 		        if (M_hi > M_max)
 			    M_hi = M_max;
 
 		        for (M = M_low; M <= M_hi; M++) {
 			    Fvco = tmp * M;
 			    if (Fvco <= MIN_VCO)
 			        continue;
 			    if (Fvco > MAX_VCO)
 			        break;
 
 			    Fout = Fvco / ( P );
 
 			    error = (target - Fout) / target;
 			    aerror = (error < 0) ? -error : error;
 			    if (aerror < abest) {
 			        abest = aerror;
 			        bestError = error;
 			        bestM = M;
 			        bestN = N;
 			        bestP = P;
 			        bestPSN = PSN;
 			        bestVLD = VLD;
 			        bestFout = Fout;
 			    }
 		        }
 		    }
 	        }
 	    }
         }
  }
  vclk[Midx]    = bestM;
  vclk[Nidx]    = bestN;
  vclk[VLDidx]  = bestVLD;
  vclk[Pidx]    = bestP;
  vclk[PSNidx]  = bestPSN;
};

static void 
sisClockLoad(int Clock, unsigned char *regs)
 {
    unsigned int 	vclk[5];
    unsigned char 	temp, xr2a, xr2b;

    sisCalcClock(Clock, 2, vclk);

    xr2a = (vclk[Midx] - 1) & 0x7f ;
    xr2a |= ((vclk[VLDidx] == 2 ) ? 1 : 0 ) << 7 ;
    xr2b  = (vclk[Nidx] -1) & 0x1f ;	/* bits [4:0] contain denumerator -MC */
    if (vclk[Pidx] <= 4){
 	    xr2b |= (vclk[Pidx] -1 ) << 5 ; /* postscale 1,2,3,4 */
            temp=regs[XR(0x13)];
 	    temp &= 0xBF;
            regs[XR(0x13)]=temp;
    } else {
	    xr2b |= ((vclk[Pidx] / 2) -1 ) << 5 ;  /* postscale 6,8 */
 
            temp=regs[XR(0x13)];
 	    temp |= 0x40;
            regs[XR(0x13)]=temp;
    };
    xr2b |= 0x80 ;   /* gain for high frequency */
 
    regs[XR(0x2a)]=xr2a;
    regs[XR(0x2b)]=xr2b;

    if (sis_chiptype == SIS_5597 || sis_chiptype == SIS_6326) {
        temp=regs[XR(0x23)];
 	if (sis_edo_vram)
 	    temp |= 0x40;
        regs[XR(0x23)]=temp;
    }

    if (sis_chiptype == SIS_5597 || sis_chiptype == SIS_6326 || sis_chiptype == SIS_530) {
 	/*write_xr(0x3B, 0x08 );*/
        if (sis_chiptype == SIS_5597) {
            temp=regs[XR(0x34)];
 	    if (sis_host_bus)
 	    	temp |= 0x18; 
 	    else temp &= ~0x18;
            regs[XR(0x34)]=temp;

            temp=regs[XR(0x3D)];
 	    if (sis_host_bus)
 	     	  temp &= 0x0F; 
            regs[XR(0x3D)]=temp;
	}
	    
 	/* One-Cycle VRAM */
        temp=regs[XR(0x34)];
 	if (sis_fast_vram)
            regs[XR(0x34)]=temp;
 
 	/* pci burst */
        temp=regs[XR(0x35)];
 	if (sis_pci_burst_on) 
 	    temp |= 0x10;
 	else if (sis_pci_burst_off)
 	    temp &= ~0x10;
        regs[XR(0x35)]=temp;

 	/* pci burst,also */
	if (sis_chiptype != SIS_530) {
                temp=regs[XR(0x26)];
		if (sis_pci_burst_on) 
			temp |= 0x20;
		else if (sis_pci_burst_off)
			temp &= ~0x20;
                regs[XR(0x26)]=temp;
	}
/* Merge FIFOs */	     
   	temp=regs[XR(0x07)];
        temp |= 0x80;
        regs[XR(0x07)]=temp;
    };

}

static int sisMClk(void)
{ int mclk;
  unsigned char xr28, xr29, xr13, xr10;

    if (sis_chiptype == SIS_530) {
	/* The MCLK in SiS530/620 is set by BIOS in SR10 */
	read_xr(0x10, xr10);
	switch(xr10 & 0x0f) {
	case 1:
		mclk = 75000; /* 75 Mhz */
		break;
	case 2:
		mclk = 83000; /* 83 Mhz */
		break;
	case 3:
		mclk = 100000;/* 100 Mhz */
		break;
	case 0:
	default:
		mclk = 66000; /* 66 Mhz */
	}
	return(mclk);
    }
    /* Numerator */
    read_xr(0x28,xr28);
    mclk=14318*((xr28 & 0x7f)+1);

    /* Denumerator */
    read_xr(0x29,xr29);
    mclk=mclk/((xr29 & 0x1f)+1);

  /* Divider. Does not seem to work for mclk for older cards */
  if ( (sis_chiptype==SIS_6326) &&
        ((xr28 & 0x80)!=0 ) ) {
         mclk = mclk*2;
    }
    /* Post-scaler. Values depends on SR13 bit 7  */
    read_xr(0x13,xr13);

    if ( (xr13 & 0x80)==0 ) {
      mclk = mclk / (((xr29 & 0x60) >> 5)+1);
    }
    else {
      /* Values 00 and 01 are reserved */
      if ((xr29 & 0x60) == 0x40) mclk=mclk/6;
      if ((xr29 & 0x60) == 0x60) mclk=mclk/8;
    }

    return(mclk);
}

static void
sis_CPUthreshold(int dotClock,int bpp,int *thresholdLow,int *thresholdHigh)
{
    unsigned char temp;
    int mclk;
    int safetymargin, gap;
    float z, z1, z2; /* Yeou */
    int factor; 
    
    mclk=sisMClk();

    if (sis_chiptype == SIS_530) /* Yeou for 530 thresholod */
    {
        /* z = f *((dotClock * bpp)/(buswidth*mclk);
           thresholdLow  = (z+1)/2 + 4;
           thresholdHigh = 0x1F;
           
           where f = 0x60 when UMA (SR0D D[0] = 1)
                     0x30      LFB (SR0D D[0] = 0)
        */
        read_xr (0x0d, temp); 
	if (temp & 0x01) factor = 0x60;
        else factor = 0x30;
   
        z1 = (float)(dotClock * bpp); 
        z2 = (float)(64.0 * mclk);
 	z = ((float) factor * (z1 / z2));
        *thresholdLow = ((int)z + 1) / 2 + 4 ;   
        *thresholdHigh = 0x1F; 
    }
    else {   /* For sis other product */
        /* Adjust thresholds. Safetymargin is to be adjusted by fifo_XXX 
           options. Try to mantain a fifo margin of gap. At high Vclk*bpp
           this isn't possible, so limit the thresholds. 
       
           The values I guess are :

           FIFO_CONSERVATIVE : safetymargin = 5 ;
           FIFO_MODERATE     : safetymargin = 3 ;
           Default           : safetymargin = 1 ;  (good enough in many cases) 
           FIFO_AGGRESSIVE   : safetymargin = 0 ;
	 
           gap=4 seems to be the best value in either case...
       */
    
       safetymargin=3; 

       gap = 4;
       *thresholdLow = ((bpp*dotClock) / mclk)+safetymargin;
       *thresholdHigh = ((bpp*dotClock) / mclk)+gap+safetymargin;

       /* 24 bpp seems to need lower FIFO limits. 
          At 16bpp is possible to put a thresholdHigh of 0 (0x10) with 
          good results on my system(good performance, and virtually no noise) */
     
        if ( *thresholdLow > (bpp < 24 ? 0xe:0x0d) ) { 
    	     *thresholdLow = (bpp < 24 ? 0xe:0x0d); 
 	}

        if ( *thresholdHigh > (bpp < 24 ? 0x10:0x0f) ) { 
 	     *thresholdHigh = (bpp < 24 ? 0x10:0x0f);
	}
    }; /* sis530 */
}

