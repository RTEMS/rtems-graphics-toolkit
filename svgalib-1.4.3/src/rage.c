/*
ATI Rage chipset 

By Matan Ziv-Av (zivav@cs.bgu.ac.il)

Alpha release - Please report to me any problem with the driver 

It works on my Xpert98 AGP 8MB SDRAM.

The driver assumes a PCI (or AGP) device, i.e a device that registers on the 
PCI bus.

This driver should work (maybe with small changes) on all Mach64 cards 
that use internal Clock and DAC. (Mach64 CT,ET,VT,GT (Rage),Rage II, 
Rage II+DVD, and Rage Pro according to the XFree86 Documentation.
  
If you have an older Mach64 card, it should be easy to adapt this driver
to work on your card.

The test only tests for a Mach64 card, not for internal DAC.

This driver is based on The XFree86 Mach64 Server and ati driver for the 
SVGA server. That code is :

Copyright 1994 through 1998 by Marc Aurele La France (TSI @ UQV), tsi@ualberta.ca

*/

#include <stdlib.h>
#include <stdio.h>		/* for printf */
#include <string.h>		/* for memset */
#include <unistd.h>
#include <sys/mman.h>
#include "vga.h"
#include "libvga.h"
#include "driver.h"

/* New style driver interface. */
#include "timing.h"
#include "vgaregs.h"
#include "interface.h"
#include "accel.h"
#include "vgapci.h"

/*
#define RAGE_TOTAL_REGS (VGA_TOTAL_REGS + 64)
*/
#define RAGE_TOTAL_REGS (VGA_TOTAL_REGS + 1400)

#define SPARSE_IO 0
#define BLOCK_IO 1

#define _ByteMask(__Byte)	((unsigned char)(-1) << (8 * (__Byte)))
#define GetByte(_Value, _Byte)	GetBits(_Value, _ByteMask(_Byte))

#define GetReg(_Register, _Index)                               \
        (                                                       \
                outb(_Register, _Index),                        \
                inb(_Register + 1)                              \
        )
#define PutReg(_Register, _Index, _Value)                       \
        outw(_Register, ((_Value) << 8) | (_Index))

#define ATIIOPort(_PortTag)							\
        (((ATIIODecoding == SPARSE_IO) ?					\
          (((_PortTag) & SPARSE_IO_SELECT) | ((_PortTag) & IO_BYTE_SELECT)) :	\
          (((_PortTag) & BLOCK_IO_SELECT)  | ((_PortTag) & IO_BYTE_SELECT))) |	\
         ATIIOBase)

#define ATTRX			0x03c0u
#define ATTRD			0x03c1u
#define SEQX			0x03c4u
#define SEQD			0x03c5u
#define GRAX			0x03ceu
#define GRAD			0x03cfu
#define GENS1			0x03dau
#define CRTX			0x03d4u
#define CRTD			0x03d5u

#define SPARSE_IO_BASE		0x03fcu
#define SPARSE_IO_SELECT	0xfc00u
#define BLOCK_IO_BASE		0xff00u
#define BLOCK_IO_SELECT		0x00fcu
#define IO_BYTE_SELECT		0x0003u

#define _UnitOf(___Value)	((((___Value) ^ ((___Value) - 1)) + 1) >> 1)
#define GetBits(__Value, _Mask)	(((__Value) & (_Mask)) / _UnitOf(_Mask))
#define SetBits(__Value, _Mask)	(((__Value) * _UnitOf(_Mask)) & (_Mask))
#define IOPortTag(_SparseIOSelect, _BlockIOSelect)	\
	(SetBits(_SparseIOSelect, SPARSE_IO_SELECT) | \
	 SetBits(_BlockIOSelect, BLOCK_IO_SELECT))
#define SparseIOTag(_IOSelect)	IOPortTag(_IOSelect, (unsigned)(-1))
#define BlockIOTag(_IOSelect)	IOPortTag((unsigned)(-1), _IOSelect)

#define CRTC_H_TOTAL_DISP	IOPortTag(0x00u, 0x00u)
#define CRTC_H_SYNC_STRT_WID	IOPortTag(0x01u, 0x01u)
#define CRTC_V_TOTAL_DISP	IOPortTag(0x02u, 0x02u)
#define CRTC_V_SYNC_STRT_WID	IOPortTag(0x03u, 0x03u)
#define CRTC_VLINE_CRNT_VLINE	IOPortTag(0x04u, 0x04u)
#define CRTC_OFF_PITCH		IOPortTag(0x05u, 0x05u)

#define SCRATCH_REG0		IOPortTag(0x10u, 0x20u)
#define OVR_WID_LEFT_RIGHT	IOPortTag(0x09u, 0x11u)
#define OVR_WID_TOP_BOTTOM	IOPortTag(0x0au, 0x12u)
#define CLOCK_CNTL		IOPortTag(0x12u, 0x24u)
#define BUS_CNTL		IOPortTag(0x13u, 0x28u)
#define MEM_VGA_WP_SEL		IOPortTag(0x15u, 0x2du)
#define MEM_VGA_RP_SEL		IOPortTag(0x16u, 0x2eu)
#define DAC_CNTL		IOPortTag(0x18u, 0x31u)
#define DAC_REGS		IOPortTag(0x17u, 0x30u)	/* 4 separate bytes */
#define CRTC_GEN_CNTL		IOPortTag(0x07u, 0x07u)
#define DSP_CONFIG		BlockIOTag(0x08u)	/* VTB/GTB/LT */
#define DSP_ON_OFF		BlockIOTag(0x09u)	/* VTB/GTB/LT */
#define OVR_CLR			IOPortTag(0x08u, 0x10u)
#define MEM_INFO		IOPortTag(0x14u, 0x2cu) /* Renamed MEM_CNTL */
#define CONFIG_CNTL		IOPortTag(0x1au, 0x37u)
#define GEN_TEST_CNTL		IOPortTag(0x19u, 0x34u)
#define CRTC_INT_CNTL		IOPortTag(0x06u, 0x06u)
#define CONFIG_STATUS64_0	IOPortTag(0x1cu, 0x39u)

#define MEM_BUF_CNTL		BlockIOTag(0x0bu)	/* VTB/GTB/LT */
#define SHARED_CNTL		BlockIOTag(0x0cu)	/* VTB/GTB/LT */
#define SHARED_MEM_CONFIG	BlockIOTag(0x0du)	/* GT3 */

#define CONFIG_CHIP_ID		IOPortTag(0x1bu, 0x38u) /* Read */
#define CFG_CHIP_TYPE0			0x000000fful
#define CFG_CHIP_TYPE1			0x0000ff00ul
#define CFG_CHIP_TYPE			0x0000fffful
#define CFG_CHIP_CLASS			0x00ff0000ul
#define CFG_CHIP_REV			0xff000000ul
#define CFG_CHIP_VERSION		0x07000000ul	/* 264xT */
#define CFG_CHIP_FOUNDRY		0x38000000ul	/* 264xT */
#define CFG_CHIP_REVISION		0xc0000000ul	/* 264xT */
#define CFG_MEM_TYPE_T			0x00000007ul
#define CFG_MEM_TYPE			0x00000038ul

#define DAC_8BIT_EN			0x00000100ul

#define CRTC_OFFSET			0x000ffffful
#define CRTC_H_TOTAL			0x000001fful
#define CRTC_H_DISP			0x01ff0000ul
#define CRTC_H_SYNC_STRT		0x000000fful
#define CRTC_H_SYNC_DLY			0x00000700ul
#define CRTC_H_SYNC_POL			0x00200000ul
#define CRTC_H_SYNC_WID			0x001f0000ul
#define CRTC_H_SYNC_STRT_HI		0x00001000ul
#define CRTC_V_TOTAL			0x000007fful
#define CRTC_V_DISP			0x07ff0000ul
#define CRTC_V_SYNC_STRT		0x000007fful
#define CRTC_V_SYNC_WID			0x001f0000ul
#define CRTC_V_SYNC_POL			0x00200000ul
#define CRTC_PIX_WIDTH			0x00000700ul
#define CRTC_PIX_WIDTH_1BPP			0x00000000ul
#define CRTC_PIX_WIDTH_4BPP			0x00000100ul
#define CRTC_PIX_WIDTH_8BPP			0x00000200ul
#define CRTC_PIX_WIDTH_15BPP			0x00000300ul
#define CRTC_PIX_WIDTH_16BPP			0x00000400ul
#define CRTC_PIX_WIDTH_24BPP			0x00000500ul
#define CRTC_PIX_WIDTH_32BPP			0x00000600ul
#define CRTC_VBLANK			0x00000001ul
#define CRTC_VBLANK_INT_EN		0x00000002ul
#define CRTC_VBLANK_INT			0x00000004ul
#define CRTC_VLINE_INT_EN		0x00000008ul
#define CRTC_VLINE_INT			0x00000010ul
#define CRTC_VLINE_SYNC			0x00000020ul
#define CRTC_FRAME			0x00000040ul
/*	?				0x0000ff80ul */
#define CRTC_SNAPSHOT_INT_EN		0x00000080ul	/* GT3 */
#define CRTC_SNAPSHOT_INT		0x00000100ul	/* GT3 */
#define CRTC_I2C_INT_EN			0x00000200ul	/* GT3 */
#define CRTC_I2C_INT			0x00000400ul	/* GT3 */
#define CRTC_CAPBUF0_INT_EN		0x00010000ul	/* VT/GT */
#define CRTC_CAPBUF0_INT		0x00020000ul	/* VT/GT */
#define CRTC_CAPBUF1_INT_EN		0x00040000ul	/* VT/GT */
#define CRTC_CAPBUF1_INT		0x00080000ul	/* VT/GT */
#define CRTC_OVERLAY_EOF_INT_EN		0x00100000ul	/* VT/GT */
#define CRTC_OVERLAY_EOF_INT		0x00200000ul	/* VT/GT */
#define CRTC_ONESHOT_CAP_INT_EN		0x00400000ul	/* VT/GT */
#define CRTC_ONESHOT_CAP_INT		0x00800000ul	/* VT/GT */
#define CRTC_BUSMASTER_EOL_INT_EN	0x01000000ul	/* VTB/GTB/LT */
#define CRTC_BUSMASTER_EOL_INT		0x02000000ul	/* VTB/GTB/LT */
#define CRTC_GP_INT_EN			0x04000000ul	/* VTB/GTB/LT */
#define CRTC_GP_INT			0x08000000ul	/* VTB/GTB/LT */
/*	?				0xf0000000ul */
#define CRTC_VBLANK_BIT2		0x80000000ul	/* GT3 */
#define CRTC_INT_ENS	/* *** UPDATE ME *** */		\
		(					\
			CRTC_VBLANK_INT_EN |		\
			CRTC_VLINE_INT_EN |		\
			CRTC_SNAPSHOT_INT_EN |		\
			CRTC_I2C_INT_EN |		\
			CRTC_CAPBUF0_INT_EN |		\
			CRTC_CAPBUF1_INT_EN |		\
			CRTC_OVERLAY_EOF_INT_EN |	\
			CRTC_ONESHOT_CAP_INT_EN |	\
			CRTC_BUSMASTER_EOL_INT_EN |	\
			CRTC_GP_INT_EN |		\
			0				\
		)
#define CRTC_INT_ACKS	/* *** UPDATE ME *** */		\
		(					\
			CRTC_VBLANK_INT |		\
			CRTC_VLINE_INT |		\
			CRTC_SNAPSHOT_INT |		\
			CRTC_I2C_INT |			\
			CRTC_CAPBUF0_INT |		\
			CRTC_CAPBUF1_INT |		\
			CRTC_OVERLAY_EOF_INT |		\
			CRTC_ONESHOT_CAP_INT |		\
			CRTC_BUSMASTER_EOL_INT |	\
			CRTC_GP_INT |			\
			0				\
		)
#define CRTC_DBL_SCAN_EN		0x00000001ul
#define CRTC_INTERLACE_EN		0x00000002ul
#define CRTC_HSYNC_DIS			0x00000004ul
#define CRTC_VSYNC_DIS			0x00000008ul
#define CRTC_CSYNC_EN			0x00000010ul
#define CRTC_PIX_BY_2_EN		0x00000020ul
#define CRTC_DISPLAY_DIS		0x00000040ul
#define CRTC_VGA_XOVERSCAN		0x00000080ul
#define CRTC_PIX_WIDTH			0x00000700ul
#define CRTC_PIX_WIDTH_1BPP			0x00000000ul
#define CRTC_PIX_WIDTH_4BPP			0x00000100ul
#define CRTC_PIX_WIDTH_8BPP			0x00000200ul
#define CRTC_PIX_WIDTH_15BPP			0x00000300ul
#define CRTC_PIX_WIDTH_16BPP			0x00000400ul
#define CRTC_PIX_WIDTH_24BPP			0x00000500ul
#define CRTC_PIX_WIDTH_32BPP			0x00000600ul
/*	?					0x00000700ul */
#define CRTC_BYTE_PIX_ORDER		0x00000800ul
#define CRTC_FIFO_OVERFILL		0x0000c000ul	/* VT/GT */
#define CRTC_FIFO_LWM			0x000f0000ul
#define CRTC_VGA_128KAP_PAGING		0x00100000ul	/* VT/GT */
#define CRTC_DISPREQ_ONLY		0x00200000ul	/* VT/GT */
#define CRTC_VFC_SYNC_TRISTATE		0x00200000ul	/* VTB/GTB/LT */
#define CRTC_LOCK_REGS			0x00400000ul	/* VT/GT */
#define CRTC_SYNC_TRISTATE		0x00800000ul	/* VT/GT */
#define CRTC_EXT_DISP_EN		0x01000000ul
#define CRTC_EN				0x02000000ul
#define CRTC_DISP_REQ_EN		0x04000000ul
#define CRTC_VGA_LINEAR			0x08000000ul
#define CRTC_VSYNC_FALL_EDGE		0x10000000ul
#define CRTC_VGA_TEXT_132		0x20000000ul
#define CRTC_CNT_EN			0x40000000ul
#define CRTC_CUR_B_TEST			0x80000000ul
#define DSP_OFF				0x000007fful
#define CRTC_PITCH			0xffc00000ul
#define CTL_MEM_SIZE			0x00000007ul	
#define CTL_MEM_SIZEB			0x0000000ful	/* VTB/GTB/LT */
#define PLL_WR_EN			0x00000200ul	/* For internal PLL */
#define PLL_ADDR			0x00007c00ul	/* For internal PLL */
#define BUS_HOST_ERR_INT_EN		0x00400000ul
#define BUS_HOST_ERR_INT		0x00800000ul
#define BUS_APER_REG_DIS		0x00000010ul	/* VTB/GTB/LT */
#define CLOCK_SELECT			0x0000000ful
#define CLOCK_DIVIDER			0x00000030ul
#define CLOCK_STROBE			0x00000040ul

#define GEN_CUR_EN			0x00000080ul

#define BUS_WS				0x0000000ful
#define BUS_DBL_RESYNC			0x00000001ul	/* VTB/GTB/LT */
#define BUS_MSTR_RESET			0x00000002ul	/* VTB/GTB/LT */
#define BUS_FLUSH_BUF			0x00000004ul	/* VTB/GTB/LT */
#define BUS_STOP_REQ_DIS		0x00000008ul	/* VTB/GTB/LT */
#define BUS_ROM_WS			0x000000f0ul
#define BUS_APER_REG_DIS		0x00000010ul	/* VTB/GTB/LT */
#define BUS_EXTRA_PIPE_DIS		0x00000020ul	/* VTB/GTB/LT */
#define BUS_MASTER_DIS			0x00000040ul	/* VTB/GTB/LT */
#define BUS_ROM_WRT_EN			0x00000080ul	/* GT3 */
#define BUS_ROM_PAGE			0x00000f00ul
#define BUS_ROM_DIS			0x00001000ul
#define BUS_IO_16_EN			0x00002000ul	/* GX */
#define BUS_PCI_READ_RETRY_EN		0x00002000ul	/* VTB/GTB/LT */
#define BUS_DAC_SNOOP_EN		0x00004000ul
#define BUS_PCI_RETRY_EN		0x00008000ul	/* VT/GT */
#define BUS_PCI_WRT_RETRY_EN		0x00008000ul	/* VTB/GTB/LT */
#define BUS_FIFO_WS			0x000f0000ul
#define BUS_RETRY_WS			0x000f0000ul	/* VTB/GTB/LT */
#define BUS_FIFO_ERR_INT_EN		0x00100000ul
#define BUS_MSTR_RD_MULT		0x00100000ul	/* VTB/GTB/LT */
#define BUS_FIFO_ERR_INT		0x00200000ul
#define BUS_MSTR_RD_LINE		0x00200000ul	/* VTB/GTB/LT */
#define BUS_HOST_ERR_INT_EN		0x00400000ul
#define BUS_SUSPEND			0x00400000ul	/* GT3 */
#define BUS_HOST_ERR_INT		0x00800000ul
#define BUS_LAT16X			0x00800000ul	/* GT3 */
#define BUS_PCI_DAC_WS			0x07000000ul
#define BUS_RD_DISCARD_EN		0x01000000ul	/* VTB/GTB/LT */
#define BUS_RD_ABORT_EN			0x02000000ul	/* VTB/GTB/LT */
#define BUS_MSTR_WS			0x04000000ul	/* VTB/GTB/LT */
#define BUS_PCI_DAC_DLY			0x08000000ul
#define BUS_EXT_REG_EN			0x08000000ul	/* VT/GT */
#define BUS_PCI_MEMW_WS			0x10000000ul
#define BUS_MSTR_DISCONNECT_EN		0x10000000ul	/* VTB/GTB/LT */
#define BUS_PCI_BURST_DEC		0x20000000ul	/* GX/CX */
#define BUS_BURST			0x20000000ul	/* 264xT */
#define BUS_WRT_BURST			0x20000000ul	/* VTB/GTB/LT */
#define BUS_RDY_READ_DLY		0xc0000000ul
#define BUS_READ_BURST			0x40000000ul	/* VTB/GTB/LT */
#define BUS_RDY_READ_DLY_B		0x80000000ul	/* VTB/GTB/LT */

#define CFG_MEM_VGA_AP_EN		0x00000004ul
#define DSP_XCLKS_PER_QW		0x00003ffful
#define DSP_FLUSH_WB			0x00008000ul
#define DSP_LOOP_LATENCY		0x000f0000ul
#define DSP_PRECISION			0x00700000ul
#define DSP_OFF				0x000007fful
#define DSP_ON				0x07ff0000ul

#define PLL_VCLK_CNTL		0x05u
#define PLL_VCLK_POST_DIV	0x06u
#define PLL_VCLK0_FB_DIV	0x07u
#define PLL_VCLK0_XDIV		0x10u
#define PLL_XCLK_CNTL		0x0bu	/* VT/GT */
#define PLL_VCLK_RESET		0x04u
#define PLL_XCLK_SRC_SEL	0x07u	/* VTB/GTB/LT */
#define PLL_MCLK_FB_DIV		0x04u
#define PLL_MFB_TIMES_4_2B	0x08u
#define PLL_REF_DIV		0x02u

#define CTL_MEM_TRAS			0x00070000ul	/* VTB/GTB/LT */
#define CTL_MEM_TRCD			0x00000c00ul	/* VTB/GTB/LT */
#define CTL_MEM_TCRD			0x00001000ul	/* VTB/GTB/LT */
#define CTL_MEM_TRP			0x00000300ul	/* VTB/GTB/LT */

#define CTL_MEM_BNDRY			0x00030000ul
#define CTL_MEM_BNDRY_EN		0x00040000ul

#define ATI_CHIP_88800GXC 16    /* Mach64 */
#define ATI_CHIP_88800GXD 17    /* Mach64 */
#define ATI_CHIP_88800GXE 18    /* Mach64 */
#define ATI_CHIP_88800GXF 19    /* Mach64 */
#define ATI_CHIP_88800GX  20    /* Mach64 */
#define ATI_CHIP_88800CX  21    /* Mach64 */
#define ATI_CHIP_264CT    22    /* Mach64 */
#define ATI_CHIP_264ET    23    /* Mach64 */
#define ATI_CHIP_264VT    24    /* Mach64 */
#define ATI_CHIP_264GT    25    /* Mach64 */
#define ATI_CHIP_264VTB   26    /* Mach64 */
#define ATI_CHIP_264GTB   27    /* Mach64 */
#define ATI_CHIP_264VT3   28    /* Mach64 */
#define ATI_CHIP_264GTDVD 29    /* Mach64 */
#define ATI_CHIP_264LT    30    /* Mach64 */
#define ATI_CHIP_264VT4   31    /* Mach64 */
#define ATI_CHIP_264GT2C  32    /* Mach64 */
#define ATI_CHIP_264GTPRO 33    /* Mach64 */
#define ATI_CHIP_264LTPRO 34    /* Mach64 */
#define ATI_CHIP_Mach64   35    /* Mach64 */

#define MEM_264_NONE            0
#define MEM_264_DRAM            1
#define MEM_264_EDO             2
#define MEM_264_PSEUDO_EDO      3
#define MEM_264_SDRAM           4
#define MEM_264_SGRAM           5
#define MEM_264_TYPE_6          6
#define MEM_264_TYPE_7          7

#define ATIGetMach64PLLReg(_Index)                              \
        (                                                       \
                ATIAccessMach64PLLReg(_Index, 0),           \
                inb(ATIIOPortCLOCK_CNTL + 2)                    \
        )
#define ATIPutMach64PLLReg(_Index, _Value)                      \
        (                                                       \
                ATIAccessMach64PLLReg(_Index, 1),            \
                outb(ATIIOPortCLOCK_CNTL + 2, _Value)           \
        )

int           ATIIOPortCRTC_H_TOTAL_DISP, ATIIOPortCRTC_H_SYNC_STRT_WID,
              ATIIOPortCRTC_V_TOTAL_DISP, ATIIOPortCRTC_V_SYNC_STRT_WID,
              ATIIOPortCRTC_OFF_PITCH,ATIIOPortCRTC_INT_CNTL,
              ATIIOPortCRTC_GEN_CNTL, ATIIOPortDSP_CONFIG, ATIIOPortDSP_ON_OFF,
              ATIIOPortOVR_CLR,
              ATIIOPortOVR_WID_LEFT_RIGHT, ATIIOPortOVR_WID_TOP_BOTTOM,
              ATIIOPortCLOCK_CNTL, ATIIOPortBUS_CNTL, ATIIOPortMEM_INFO,
              ATIIOPortMEM_VGA_WP_SEL, ATIIOPortMEM_VGA_RP_SEL,
              ATIIOPortDAC_REGS, ATIIOPortDAC_CNTL, ATIIOPortDAC_DATA,
              ATIIOPortDAC_READ, ATIIOPortDAC_WRITE,ATIIOPortDAC_MASK,
              ATIIOPortGEN_TEST_CNTL, ATIIOPortCONFIG_CNTL,
              ATIIOPortCONFIG_STATUS64_0;

void ATIAccessMach64PLLReg(const int Index, const int Write)
{
    int clock_cntl1 = inb(ATIIOPortCLOCK_CNTL + 1) &
        ~GetByte(PLL_WR_EN | PLL_ADDR, 1);

    /* Set PLL register to be read or written */
    outb(ATIIOPortCLOCK_CNTL + 1, clock_cntl1 |
        GetByte(SetBits(Index, PLL_ADDR) | SetBits(Write, PLL_WR_EN), 1));
}

#define DAC_SIZE 768

typedef struct {
        unsigned int  
               crtc_h_total_disp, crtc_h_sync_strt_wid,
               crtc_v_total_disp, crtc_v_sync_strt_wid,
               crtc_off_pitch, crtc_gen_cntl, 
               crtc_vline_crnt_vline,
               dsp_config, dsp_on_off,
               ovr_clr, ovr_wid_left_right, ovr_wid_top_bottom,
               clock_cntl, bus_cntl, mem_vga_wp_sel, mem_vga_rp_sel,
               dac_cntl, config_cntl, banks, planes,
               dac_read, dac_write, dac_mask,
               mem_info,
               mem_buf_cntl, shared_cntl, shared_mem_config,
               crtc_int_cntl, gen_test_cntl,
               misc_options, mem_bndry, mem_cfg;
        unsigned char PLL[32];
        unsigned char DAC[DAC_SIZE];
        unsigned char extdac[16];
        int ics2595;
       } ATIHWRec, *ATIHWPtr;
extern unsigned char __svgalib_ragedoubleclock;
static int rage_init(int, int, int);
static void rage_unlock(void);
static void rage_lock(void);

void __svgalib_rageaccel_init(AccelSpecs * accelspecs, int bpp, int width_in_pixels);

static int rage_memory,rage_chiptyperev;
static int rage_is_linear=0 , rage_linear_base;
static int ATIIODecoding;
static int ATIIOBase;
static int postdiv[8]={1,2,4,8,3,0,6,12};
static int rage_bpp;
static int M, minN, maxN, Nadj;
static double fref;
static int ATIClockToProgram;
static int ATIChip, ATIMemoryType;
static int rage_dac=0, rage_clock=0;

static CardSpecs *cardspecs;

static void rage_ChipID(void)
{
   int ATIChipType, ATIChipClass, ATIChipRevision, ATIChipVersion,
       ATIChipFoundry;
    unsigned int IO_Value = inl(ATIIOPort(CONFIG_CHIP_ID));
    ATIChipType     = GetBits(IO_Value, 0xFFFFU);
    ATIChipClass    = GetBits(IO_Value, CFG_CHIP_CLASS);
    ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REV);
    ATIChipVersion  = GetBits(IO_Value, CFG_CHIP_VERSION);
    ATIChipFoundry  = GetBits(IO_Value, CFG_CHIP_FOUNDRY);
    switch (ATIChipType)
    {
        case 0x00D7U:
            ATIChipType = 0x4758U;
        case 0x4758U:
            switch (ATIChipRevision)
            {
                case 0x00U:
                    ATIChip = ATI_CHIP_88800GXC;
                    break;

                case 0x01U:
                    ATIChip = ATI_CHIP_88800GXD;
                    break;

                case 0x02U:
                    ATIChip = ATI_CHIP_88800GXE;
                    break;

                case 0x03U:
                    ATIChip = ATI_CHIP_88800GXF;
                    break;

                default:
                    ATIChip = ATI_CHIP_88800GX;
                    break;
            }
            break;

        case 0x0057U:
            ATIChipType = 0x4358U;
        case 0x4358U:
            ATIChip = ATI_CHIP_88800CX;
            break;

        case 0x0053U:
            ATIChipType = 0x4354U;
        case 0x4354U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264CT;
            break;

        case 0x0093U:
            ATIChipType = 0x4554U;
        case 0x4554U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264ET;
            break;

        case 0x02B3U:
            ATIChipType = 0x5654U;
        case 0x5654U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264VT;
            /* Some early GT's are detected as VT's */
/*            if (ExpectedChipType && (ATIChipType != ExpectedChipType))
            {
                if (ExpectedChipType == 0x4754U)
                    ATIChip = ATI_CHIP_264GT;
                else 
                    ErrorF("Mach64 chip type probe discrepancy detected:\n"
                           " PCI=0x%04X;  CHIP_ID=0x%04X.\n",
                           ExpectedChipType, ATIChipType);
            }
            else */if (ATIChipVersion)
                ATIChip = ATI_CHIP_264VTB;
            break;

        case 0x00D3U:
            ATIChipType = 0x4754U;
        case 0x4754U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            if (!ATIChipVersion)
                ATIChip = ATI_CHIP_264GT;
            else
                ATIChip = ATI_CHIP_264GTB;
            break;

        case 0x02B4U:
            ATIChipType = 0x5655U;
        case 0x5655U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264VT3;
            break;

        case 0x00D4U:
            ATIChipType = 0x4755U;
        case 0x4755U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264GTDVD;
            break;

        case 0x0166U:
            ATIChipType = 0x4C47U;
        case 0x4C47U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264LT;
            break;

        case 0x0315U:
            ATIChipType = 0x5656U;
        case 0x5656U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264VT4;
            break;

        case 0x00D5U:
            ATIChipType = 0x4756U;
        case 0x4756U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264GT2C;
            break;

        case 0x00D6U:
        case 0x00D9U:
            ATIChipType = 0x4757U;
        case 0x4757U:
        case 0x475AU:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264GT2C;
            break;

        case 0x00CEU:
        case 0x00CFU:
        case 0x00D0U:
            ATIChipType = 0x4750U;
        case 0x4749U:
        case 0x4750U:
        case 0x4751U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264GTPRO;
            break;

        case 0x00C7U:
        case 0x00C9U:
            ATIChipType = 0x4742U;
        case 0x4742U:
        case 0x4744U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264GTPRO;
            break;

        case 0x0168U:
        case 0x016FU:
            ATIChipType = 0x4C50U;
        case 0x4C49U:
        case 0x4C50U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264LTPRO;
            break;

        case 0x0161U:
        case 0x0163U:
            ATIChipType = 0x4C42U;
        case 0x4C42U:
        case 0x4C44U:
            ATIChipRevision = GetBits(IO_Value, CFG_CHIP_REVISION);
            ATIChip = ATI_CHIP_264LTPRO;
            break;

        default:
            ATIChip = ATI_CHIP_Mach64;
            break;
    }
};

static int rage_probe(void)
{
    unsigned int i=inl(ATIIOPort(SCRATCH_REG0));
    outl(ATIIOPort(SCRATCH_REG0),0x55555555);
    if(inl(ATIIOPort(SCRATCH_REG0))!=0x55555555) {
       outl(ATIIOPort(SCRATCH_REG0),i);
       return 0;
    };
    outl(ATIIOPort(SCRATCH_REG0),0xaaaaaaaa);
    if(inl(ATIIOPort(SCRATCH_REG0))!=0xaaaaaaaa) {
       outl(ATIIOPort(SCRATCH_REG0),i);
       return 0;
    };
    outl(ATIIOPort(SCRATCH_REG0),i);

    return 1;
};

static int
ATIDivide(int Numerator, int Denominator, int Shift, const int RoundingKind)
{
    int Multiplier, Divider;
    int Rounding = 0;                           /* Default to floor */

    /* Deal with right shifts */
    if (Shift < 0)
    {
        Divider = (Numerator - 1) ^ Numerator;
        Multiplier = 1 << (-Shift);
        if (Divider > Multiplier)
            Divider = Multiplier;
        Numerator /= Divider;
        Denominator *= Multiplier / Divider;
        Shift = 0;
    }

    if (!RoundingKind)                          /* Nearest */
        Rounding = Denominator >> 1;
    else if (RoundingKind > 0)                  /* Ceiling */
        Rounding = Denominator - 1;

    return ((Numerator / Denominator) << Shift) +
            ((((Numerator % Denominator) << Shift) + Rounding) / Denominator);
}

static void rage_setpage(int page)
{
    page*=2;
    outl(ATIIOPortMEM_VGA_WP_SEL, page | ((page+1)<<16));
    outl(ATIIOPortMEM_VGA_RP_SEL, page | ((page+1)<<16));
}

static void rage_setrdpage(int page)
{
    page*=2;
    outl(ATIIOPortMEM_VGA_RP_SEL, page | ((page+1)<<16));
}

static void rage_setwrpage(int page)
{
    page*=2;
    outl(ATIIOPortMEM_VGA_WP_SEL, page | ((page+1)<<16));
}

static int __svgalib_rage_inlinearmode(void)
{
return rage_is_linear;
}

/* Fill in chipset specific mode information */

static void rage_getmodeinfo(int mode, vga_modeinfo *modeinfo)
{

    if(modeinfo->colors==16)return;

    modeinfo->maxpixels = rage_memory*1024/modeinfo->bytesperpixel;
    modeinfo->maxlogicalwidth = 4088;
    modeinfo->startaddressrange = rage_memory * 1024 - 1;
    modeinfo->haveblit = 0;
    modeinfo->flags &= ~HAVE_RWPAGE;
    modeinfo->flags |= HAVE_RWPAGE;

    if (modeinfo->bytesperpixel >= 1) {
	if(rage_linear_base)modeinfo->flags |= CAPABLE_LINEAR;
        if (__svgalib_rage_inlinearmode())
	    modeinfo->flags |= IS_LINEAR;
    }
}

/* Read and save chipset-specific registers */

int rage_saveregs(unsigned char regs[])
{ 
  ATIHWPtr save;
  int i;

  save=(ATIHWPtr)(regs+VGA_TOTAL_REGS);

/* This are needed for calling this function, without vga_init first */
/*    
    ATIIODecoding=1;
    ATIIOBase=0xd000;
    ATIIOPortCRTC_H_TOTAL_DISP=ATIIOPort(CRTC_H_TOTAL_DISP);
    ATIIOPortCRTC_H_SYNC_STRT_WID=ATIIOPort(CRTC_H_SYNC_STRT_WID);
    ATIIOPortCRTC_V_TOTAL_DISP=ATIIOPort(CRTC_V_TOTAL_DISP);
    ATIIOPortCRTC_V_SYNC_STRT_WID=ATIIOPort(CRTC_V_SYNC_STRT_WID);
    ATIIOPortCRTC_OFF_PITCH=ATIIOPort(CRTC_OFF_PITCH);
    ATIIOPortCRTC_INT_CNTL=ATIIOPort(CRTC_INT_CNTL);
    ATIIOPortCRTC_GEN_CNTL=ATIIOPort(CRTC_GEN_CNTL);
    ATIIOPortDSP_CONFIG=ATIIOPort(DSP_CONFIG);
    ATIIOPortDSP_ON_OFF=ATIIOPort(DSP_ON_OFF);
    ATIIOPortOVR_CLR=ATIIOPort(OVR_CLR);
    ATIIOPortOVR_WID_LEFT_RIGHT=ATIIOPort(OVR_WID_LEFT_RIGHT);
    ATIIOPortOVR_WID_TOP_BOTTOM=ATIIOPort(OVR_WID_TOP_BOTTOM);
    ATIIOPortCLOCK_CNTL=ATIIOPort(CLOCK_CNTL);
    ATIIOPortBUS_CNTL=ATIIOPort(BUS_CNTL);
    ATIIOPortMEM_INFO=ATIIOPort(MEM_INFO);
    ATIIOPortMEM_VGA_WP_SEL=ATIIOPort(MEM_VGA_WP_SEL);
    ATIIOPortMEM_VGA_RP_SEL=ATIIOPort(MEM_VGA_RP_SEL);
    ATIIOPortDAC_REGS=ATIIOPort(DAC_REGS);
    ATIIOPortDAC_CNTL=ATIIOPort(DAC_CNTL);
    ATIIOPortGEN_TEST_CNTL=ATIIOPort(GEN_TEST_CNTL);
    ATIIOPortCONFIG_CNTL=ATIIOPort(CONFIG_CNTL);
*/
  save->crtc_gen_cntl = inl(ATIIOPortCRTC_GEN_CNTL);

  save->crtc_h_total_disp = inl(ATIIOPortCRTC_H_TOTAL_DISP);
  save->crtc_h_sync_strt_wid = inl(ATIIOPortCRTC_H_SYNC_STRT_WID);
  save->crtc_v_total_disp = inl(ATIIOPortCRTC_V_TOTAL_DISP);
  save->crtc_v_sync_strt_wid = inl(ATIIOPortCRTC_V_SYNC_STRT_WID);
  save->crtc_off_pitch = inl(ATIIOPortCRTC_OFF_PITCH);

  save->ovr_clr = inl(ATIIOPortOVR_CLR);
  save->ovr_wid_left_right = inl(ATIIOPortOVR_WID_LEFT_RIGHT);
  save->ovr_wid_top_bottom = inl(ATIIOPortOVR_WID_TOP_BOTTOM);

  save->clock_cntl = inl(ATIIOPortCLOCK_CNTL);

  save->bus_cntl = inl(ATIIOPortBUS_CNTL);

  save->mem_vga_wp_sel = inl(ATIIOPortMEM_VGA_WP_SEL);
  save->mem_vga_rp_sel = inl(ATIIOPortMEM_VGA_RP_SEL);

  save->dac_cntl = inl(ATIIOPortDAC_CNTL); /* internal DAC */

  save->config_cntl = inl(ATIIOPortCONFIG_CNTL);

  save->mem_info = inl(ATIIOPortMEM_INFO);

  save->crtc_int_cntl=inl(ATIIOPortCRTC_INT_CNTL);
  save->crtc_gen_cntl=inl(ATIIOPortCRTC_GEN_CNTL);
  save->gen_test_cntl=inl(ATIIOPortGEN_TEST_CNTL);  

  switch(rage_clock){
     case 0: /* internal clock */
        for(i=6;i<12;i++)
           save->PLL[i]=ATIGetMach64PLLReg(i); /* internal Clock */
        break;
  };

  if((ATIChip>=ATI_CHIP_264VTB)&&(ATIIODecoding==BLOCK_IO)) {
    save->dsp_on_off=inl(ATIIOPortDSP_ON_OFF);
    save->dsp_config=inl(ATIIOPortDSP_CONFIG);
  };

  ATIIOPortDAC_DATA = ATIIOPortDAC_REGS + 1;
  ATIIOPortDAC_MASK = ATIIOPortDAC_REGS + 2;
  ATIIOPortDAC_READ = ATIIOPortDAC_REGS + 3;
  ATIIOPortDAC_WRITE = ATIIOPortDAC_REGS + 0;
  save->dac_read = inb(ATIIOPortDAC_READ);
  save->dac_write = inb(ATIIOPortDAC_WRITE);
  save->dac_mask = inb(ATIIOPortDAC_MASK);

  outb(ATIIOPortDAC_MASK, 0xFFU);
  outb(ATIIOPortDAC_READ, 0x00U);
  for (i = 0;  i<DAC_SIZE;  i++)
      save->DAC[i] = inb(ATIIOPortDAC_DATA);

  switch(rage_dac){
       case 5:
          i=inl(ATIIOPortDAC_CNTL);
          outl(ATIIOPortDAC_CNTL,(i&0xfffffffc)|2);
          save->extdac[8]=inb(ATIIOPortDAC_WRITE);
          save->extdac[10]=inb(ATIIOPortDAC_MASK);
          save->extdac[11]=inb(ATIIOPortDAC_READ);
          outl(ATIIOPortDAC_CNTL,(i&0xfffffffc)|3);
          save->extdac[12]=inb(ATIIOPortDAC_WRITE);
          outl(ATIIOPortDAC_CNTL,i);
       break;
  };

  return RAGE_TOTAL_REGS - VGA_TOTAL_REGS;
}

/* Set chipset-specific registers */

static void rage_setregs(const unsigned char regs[], int mode)
{  
    ATIHWPtr restore;
    int Index;    
    int i;

    restore=(ATIHWPtr)(regs+VGA_TOTAL_REGS);

    outl(ATIIOPortCRTC_GEN_CNTL, restore->crtc_gen_cntl & ~CRTC_EN );

    /* Load Mach64 CRTC registers */
    outl(ATIIOPortCRTC_H_TOTAL_DISP, restore->crtc_h_total_disp);
    outl(ATIIOPortCRTC_H_SYNC_STRT_WID, restore->crtc_h_sync_strt_wid);
    outl(ATIIOPortCRTC_V_TOTAL_DISP, restore->crtc_v_total_disp);
    outl(ATIIOPortCRTC_V_SYNC_STRT_WID, restore->crtc_v_sync_strt_wid);
    outl(ATIIOPortCRTC_OFF_PITCH, restore->crtc_off_pitch);
    /* Set pixel clock */
    outl(ATIIOPortCLOCK_CNTL, restore->clock_cntl);

    /* Load overscan registers */
    outl(ATIIOPortOVR_CLR, restore->ovr_clr);
    outl(ATIIOPortOVR_WID_LEFT_RIGHT, restore->ovr_wid_left_right);
    outl(ATIIOPortOVR_WID_TOP_BOTTOM, restore->ovr_wid_top_bottom);

    /* Finalize CRTC setup and turn on the screen */
    outl(ATIIOPortCRTC_GEN_CNTL, restore->crtc_gen_cntl);

    /* Aperture setup */
    outl(ATIIOPortBUS_CNTL, restore->bus_cntl);

    outl(ATIIOPortMEM_VGA_WP_SEL, restore->mem_vga_wp_sel);
    outl(ATIIOPortMEM_VGA_RP_SEL, restore->mem_vga_rp_sel);

    outl(ATIIOPortCONFIG_CNTL, restore->config_cntl);

    if((ATIChip>=ATI_CHIP_264VTB)&&(ATIIODecoding==BLOCK_IO)) {
       outl(ATIIOPortDSP_ON_OFF, restore->dsp_on_off);
       outl(ATIIOPortDSP_CONFIG, restore->dsp_config);
    };
    
    outl(ATIIOPortMEM_INFO, restore->mem_info);
    outl(ATIIOPortCRTC_INT_CNTL,restore->crtc_int_cntl);
    outl(ATIIOPortCRTC_GEN_CNTL,restore->crtc_gen_cntl);
    outl(ATIIOPortGEN_TEST_CNTL,restore->gen_test_cntl);

    switch(rage_clock){
       case 0: /* internal clock */
          i=ATIGetMach64PLLReg(PLL_VCLK_CNTL) | PLL_VCLK_RESET;
          ATIPutMach64PLLReg(PLL_VCLK_CNTL,i);
    
          ATIPutMach64PLLReg(PLL_VCLK_POST_DIV, restore->PLL[PLL_VCLK_POST_DIV]);
          ATIPutMach64PLLReg(PLL_XCLK_CNTL, restore->PLL[PLL_XCLK_CNTL]);
          ATIPutMach64PLLReg(PLL_VCLK0_FB_DIV+ATIClockToProgram, restore->PLL[PLL_VCLK0_FB_DIV+ATIClockToProgram]);
    
          i&= ~PLL_VCLK_RESET;
          ATIPutMach64PLLReg(PLL_VCLK_CNTL,i);
       break;
    };

    /* make sure the dac is in 8 bit or 6 bit mode, as needed */
    outl(ATIIOPortDAC_CNTL, restore->dac_cntl);
 
    outb(ATIIOPortDAC_MASK, 0xFFU);
    outb(ATIIOPortDAC_WRITE, 0x00U);
    for (Index = 0;  Index < DAC_SIZE;  Index++)
      outb(ATIIOPortDAC_DATA, restore->DAC[Index]);

    switch(rage_dac){
       case 5:
          i=inl(ATIIOPortDAC_CNTL);
          outl(ATIIOPortDAC_CNTL,(i&0xfffffffc)|2);
          outb(ATIIOPortDAC_WRITE,restore->extdac[8]);
          outb(ATIIOPortDAC_MASK,restore->extdac[10]);
          outb(ATIIOPortDAC_READ,restore->extdac[11]);
          outl(ATIIOPortDAC_CNTL,(i&0xfffffffc)|3);
          outb(ATIIOPortDAC_WRITE,(inb(ATIIOPortDAC_WRITE)&0x80)|restore->extdac[12]);
       break;
    };

    outb(ATIIOPortDAC_READ, restore->dac_read);
    outb(ATIIOPortDAC_WRITE, restore->dac_write);
    outl(ATIIOPortDAC_CNTL, restore->dac_cntl);

};
/* Return nonzero if mode is available */

static int rage_modeavailable(int mode)
{
    struct info *info;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;

    if ((mode < G640x480x256 )
	|| mode == G720x348x2)
	return __svgalib_vga_driverspecs.modeavailable(mode);

    info = &__svgalib_infotable[mode];
    if (rage_memory * 1024 < info->ydim * info->xbytes)
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

static unsigned
comp_lmn(unsigned clock, int *n, int *mp, int *lp);

/* Local, called by rage_setmode(). */
static void rage_initializemode(unsigned char *moderegs,
			    ModeTiming * modetiming, ModeInfo * modeinfo, int mode)
{ 
    int m,n,l,i;
    ATIHWPtr ATINewHWPtr;
    int ATIDisplayFIFODepth = 32 ;
    int ReferenceDivider = M ;
    
    ATINewHWPtr=(ATIHWPtr)(moderegs+VGA_TOTAL_REGS); 

    moderegs[GRA+0]=0;
    moderegs[GRA+1]=0;
    moderegs[GRA+2]=0;
    moderegs[GRA+3]=0;
    moderegs[GRA+4]=0;
    moderegs[GRA+5]=0x10;
    moderegs[GRA+6]=1;
    moderegs[GRA+7]=0;
    moderegs[GRA+8]=0xff;
    moderegs[SEQ+0]=0x3;
    moderegs[SEQ+1]=0x0;
    moderegs[SEQ+2]=0x0F;
    moderegs[SEQ+3]=0x0;
    moderegs[SEQ+4]=0x0A;
    moderegs[ATT+0x10]=0x0c;
    moderegs[ATT+0x11]=0x11;
    moderegs[ATT+0x12]=0xf;
    moderegs[ATT+0x13]=0x13;
    moderegs[ATT+0x14]=0;
    moderegs[VGA_MISCOUTPUT]=0x27;

    ATINewHWPtr->clock_cntl=ATIClockToProgram;

    ATINewHWPtr->crtc_int_cntl=(inl(ATIIOPortCRTC_INT_CNTL) & ~CRTC_INT_ENS) 
       				| CRTC_INT_ACKS /*0x80000074 */;

    ATINewHWPtr->shared_cntl=0;
    ATINewHWPtr->gen_test_cntl=0;
    
    ATINewHWPtr->mem_info=inl(ATIIOPortMEM_INFO);
    
    if(ATIChip<ATI_CHIP_264CT)
      ATINewHWPtr->mem_info &= ~(CTL_MEM_BNDRY | CTL_MEM_BNDRY_EN) ;

    ATINewHWPtr->PLL[PLL_VCLK_POST_DIV]=ATIGetMach64PLLReg(PLL_VCLK_POST_DIV);
    ATINewHWPtr->PLL[PLL_XCLK_CNTL]=ATIGetMach64PLLReg(PLL_XCLK_CNTL);

    for(i=0;i<256;i++)ATINewHWPtr->DAC[i*3]=ATINewHWPtr->DAC[i*3+1]=
                      ATINewHWPtr->DAC[i*3+2]=i;

    ATINewHWPtr->crtc_off_pitch=SetBits(modeinfo->width >> 3, CRTC_PITCH);
  
    ATINewHWPtr->bus_cntl = (inl(ATIIOPortBUS_CNTL) &
        ~BUS_HOST_ERR_INT_EN) | BUS_HOST_ERR_INT;
    if (ATIChip < ATI_CHIP_264VTB)
        ATINewHWPtr->bus_cntl = (ATINewHWPtr->bus_cntl &
            ~(BUS_FIFO_ERR_INT_EN | BUS_ROM_DIS)) |
            (SetBits(15, BUS_FIFO_WS) | BUS_FIFO_ERR_INT);
    else
        ATINewHWPtr->bus_cntl |= BUS_APER_REG_DIS;

    ATINewHWPtr->dac_cntl=inl(ATIIOPortDAC_CNTL);

    if (modeinfo->bitsPerPixel>8)
            ATINewHWPtr->dac_cntl |= DAC_8BIT_EN;
      else
            ATINewHWPtr->dac_cntl &= ~DAC_8BIT_EN;

    ATINewHWPtr->config_cntl= inl(ATIIOPortCONFIG_CNTL) | CFG_MEM_VGA_AP_EN;

    ATINewHWPtr->ovr_clr=0;

    modetiming->CrtcVDisplay--;
    modetiming->CrtcVSyncStart--;
    modetiming->CrtcVSyncEnd--;
    modetiming->CrtcVTotal--;

    modetiming->CrtcHDisplay = (modetiming->CrtcHDisplay >> 3) - 1;
    modetiming->CrtcHTotal = (modetiming->CrtcHTotal >> 3) - 1;
    modetiming->CrtcHSyncStart  = (modetiming->CrtcHSyncStart >> 3) -1;
    modetiming->CrtcHSyncEnd = (modetiming->CrtcHSyncEnd >> 3) -1;

    comp_lmn(modetiming->pixelClock,&n,&m,&l);
    
    switch(rage_clock) {
       case 1:
             ATINewHWPtr->ics2595=n|(l<<9);
         break;
       default:
            ATINewHWPtr->PLL[PLL_VCLK0_FB_DIV+ATIClockToProgram]=n;
            ATINewHWPtr->PLL[PLL_VCLK_POST_DIV]&=0xfc<<(ATIClockToProgram<<1);
            ATINewHWPtr->PLL[PLL_VCLK_POST_DIV]|=(l&3)<<(ATIClockToProgram<<1);
            ATINewHWPtr->PLL[PLL_XCLK_CNTL]&=~(0x10<<ATIClockToProgram);
            ATINewHWPtr->PLL[PLL_XCLK_CNTL]|=((l>>2)<<4)<<ATIClockToProgram;
    };
    ATINewHWPtr->crtc_h_total_disp =
          SetBits(modetiming->CrtcHTotal, CRTC_H_TOTAL) |
          SetBits(modetiming->CrtcHDisplay, CRTC_H_DISP);
    ATINewHWPtr->crtc_h_sync_strt_wid =
          SetBits(modetiming->CrtcHSyncStart, CRTC_H_SYNC_STRT) |
          SetBits(0, CRTC_H_SYNC_DLY) |     
          SetBits(GetBits(modetiming->CrtcHSyncStart, 0x0100U),CRTC_H_SYNC_STRT_HI) |
          SetBits(modetiming->CrtcHSyncEnd - modetiming->CrtcHSyncStart,CRTC_H_SYNC_WID);
    if (modetiming->flags & NHSYNC)
          ATINewHWPtr->crtc_h_sync_strt_wid |= CRTC_H_SYNC_POL;

    ATINewHWPtr->crtc_v_total_disp =
          SetBits(modetiming->CrtcVTotal, CRTC_V_TOTAL) |
          SetBits(modetiming->CrtcVDisplay, CRTC_V_DISP);
    ATINewHWPtr->crtc_v_sync_strt_wid =
          SetBits(modetiming->CrtcVSyncStart, CRTC_V_SYNC_STRT) |
          SetBits(modetiming->CrtcVSyncEnd - modetiming->CrtcVSyncStart,CRTC_V_SYNC_WID);
    if (modetiming->flags & NVSYNC)
          ATINewHWPtr->crtc_v_sync_strt_wid |= CRTC_V_SYNC_POL;

    ATINewHWPtr->crtc_gen_cntl = inl(ATIIOPortCRTC_GEN_CNTL) &
            ~(CRTC_DBL_SCAN_EN | CRTC_INTERLACE_EN |
              CRTC_HSYNC_DIS | CRTC_VSYNC_DIS | CRTC_CSYNC_EN |
              CRTC_PIX_BY_2_EN | CRTC_DISPLAY_DIS | CRTC_VGA_XOVERSCAN |
              CRTC_PIX_WIDTH | CRTC_BYTE_PIX_ORDER | CRTC_FIFO_LWM |
              CRTC_VGA_128KAP_PAGING | CRTC_VFC_SYNC_TRISTATE |
              CRTC_LOCK_REGS |          
              CRTC_SYNC_TRISTATE | CRTC_DISP_REQ_EN |
              CRTC_VGA_TEXT_132 | CRTC_CUR_B_TEST);
    ATINewHWPtr->crtc_gen_cntl |= CRTC_EXT_DISP_EN | CRTC_EN |
          CRTC_VGA_LINEAR | CRTC_CNT_EN | CRTC_BYTE_PIX_ORDER;

    switch (modeinfo->bitsPerPixel)
        {
            case 1:
                ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_1BPP;
                break;
            case 4:
                ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_4BPP;
                break;
            case 8:
                ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_8BPP;
                break;
            case 16:
               	switch(modeinfo->colorBits)
                   {	
                        case 15:
                           ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_15BPP;
                           break;
                        case 16:
                           ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_16BPP;
                           break;
                   };
                break;
            case 24:
                ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_24BPP;
                break;
            case 32:
                ATINewHWPtr->crtc_gen_cntl |= CRTC_PIX_WIDTH_32BPP;
                break;
            default:
                break;
        }

    if (modetiming->flags & DOUBLESCAN)
            ATINewHWPtr->crtc_gen_cntl |= CRTC_DBL_SCAN_EN;
    if (modetiming->flags & INTERLACED)
            ATINewHWPtr->crtc_gen_cntl |= CRTC_INTERLACE_EN;

    switch(rage_dac){
       case 5: /* 68860/68880 */
          ATINewHWPtr->extdac[8]=2;
          ATINewHWPtr->extdac[10]=0x1d;
          switch(modeinfo->bitsPerPixel) {
             case 8: ATINewHWPtr->extdac[11]=0x83; break;
             case 15: ATINewHWPtr->extdac[11]=0xa0; break;
             case 16: ATINewHWPtr->extdac[11]=0xa1; break;
             case 24: ATINewHWPtr->extdac[11]=0xc0; break;
             case 32: ATINewHWPtr->extdac[11]=0xe3; break;
          };
          ATINewHWPtr->extdac[12]=0x60;
          if(modeinfo->bitsPerPixel==8)ATINewHWPtr->extdac[12]=0x61;
          if(rage_memory<1024)ATINewHWPtr->extdac[12]|=0x04;
          if(rage_memory==1024)ATINewHWPtr->extdac[12]|=0x08;
          if(rage_memory>1024)ATINewHWPtr->extdac[12]|=0x0c;
       break;       
    };

    rage_bpp = modeinfo->bytesPerPixel; 

if((ATIChip>=ATI_CHIP_264VTB)&&(ATIIODecoding==BLOCK_IO))
{
    int Multiplier, Divider;
    int dsp_precision, dsp_on, dsp_off, dsp_xclks;
    int tmp, vshift, xshift;
    int ATIXCLKFeedbackDivider,
        ATIXCLKReferenceDivider,
        ATIXCLKPostDivider;

    int ATIXCLKMaxRASDelay,
        ATIXCLKPageFaultDelay,
        ATIDisplayLoopLatency;

    int IO_Value;

    IO_Value = ATIGetMach64PLLReg(PLL_XCLK_CNTL);
    ATIXCLKPostDivider = GetBits(IO_Value, PLL_XCLK_SRC_SEL);
    ATIXCLKReferenceDivider = M ;
    switch (ATIXCLKPostDivider)
    {
        case 0: case 1: case 2: case 3:
            break;

        case 4:
            ATIXCLKReferenceDivider *= 3;
            ATIXCLKPostDivider = 0;
            break;

        default:
            return;
    }

    ATIXCLKPostDivider -= GetBits(IO_Value, PLL_MFB_TIMES_4_2B);
    ATIXCLKFeedbackDivider = ATIGetMach64PLLReg(PLL_MCLK_FB_DIV);

    IO_Value = inl(ATIIOPortMEM_INFO);
    tmp = GetBits(IO_Value, CTL_MEM_TRP);
    ATIXCLKPageFaultDelay = GetBits(IO_Value, CTL_MEM_TRCD) +
        GetBits(IO_Value, CTL_MEM_TCRD) + tmp + 2;
    ATIXCLKMaxRASDelay = GetBits(IO_Value, CTL_MEM_TRAS) + tmp + 2;
    ATIDisplayFIFODepth = 32;

    if ( ATIChip < ATI_CHIP_264VT4 )
    {
        ATIXCLKPageFaultDelay += 2;
        ATIXCLKMaxRASDelay += 3;
        ATIDisplayFIFODepth = 24;
    }
        

    switch (ATIMemoryType)
    {
        case MEM_264_DRAM:
            if (rage_memory <= 1024)
                ATIDisplayLoopLatency = 10;
            else
            {
                ATIDisplayLoopLatency = 8;
                ATIXCLKPageFaultDelay += 2;
            }
            break;

        case MEM_264_EDO:
        case MEM_264_PSEUDO_EDO:
            if (rage_memory <= 1024)
                ATIDisplayLoopLatency = 9;
            else
            {
                ATIDisplayLoopLatency = 8;
                ATIXCLKPageFaultDelay++;
            }
            break;

        case MEM_264_SDRAM:
            if (rage_memory <= 1024)
                ATIDisplayLoopLatency = 11;
            else
            {
                ATIDisplayLoopLatency = 10;
                ATIXCLKPageFaultDelay++;
            }
            break;

        case MEM_264_SGRAM:
            ATIDisplayLoopLatency = 8;
            ATIXCLKPageFaultDelay += 3;
            break;

        default:                
            ATIDisplayLoopLatency = 11;
            ATIXCLKPageFaultDelay += 3;
            break;
    }

    if (ATIXCLKMaxRASDelay <= ATIXCLKPageFaultDelay)
        ATIXCLKMaxRASDelay = ATIXCLKPageFaultDelay + 1;

#   define Maximum_DSP_PRECISION ((int)GetBits(DSP_PRECISION, DSP_PRECISION))

    Multiplier = ReferenceDivider * ATIXCLKFeedbackDivider *
       postdiv[l];
    Divider = n * ATIXCLKReferenceDivider;
/*    if (!ATIUsingPlanarModes) */
        Divider *= modeinfo->bitsPerPixel >> 2;

    vshift = (5 - 2) - ATIXCLKPostDivider;
    vshift++;             

    tmp = ATIDivide(Multiplier * ATIDisplayFIFODepth, Divider, vshift, 1);
    for (dsp_precision = -5;  tmp;  dsp_precision++)
        tmp >>= 1;
    if (dsp_precision < 0)
        dsp_precision = 0;
    else if (dsp_precision > Maximum_DSP_PRECISION)
        dsp_precision = Maximum_DSP_PRECISION;

    xshift = 6 - dsp_precision;
    vshift += xshift;

    dsp_off = ATIDivide(Multiplier * (ATIDisplayFIFODepth - 1), Divider,
        vshift, 1);

    {
        dsp_on = ATIDivide(Multiplier, Divider, vshift, -1);
        tmp = ATIDivide(ATIXCLKMaxRASDelay, 1, xshift, 1);
        if (dsp_on < tmp)
            dsp_on = tmp;
        dsp_on += tmp + ATIDivide(ATIXCLKPageFaultDelay, 1, xshift, 1);
    }

    dsp_xclks = ATIDivide(Multiplier, Divider, vshift + 5, 1);

    ATINewHWPtr->dsp_on_off = SetBits(dsp_on, DSP_ON) |
        SetBits(dsp_off, DSP_OFF);
    ATINewHWPtr->dsp_config = SetBits(dsp_precision, DSP_PRECISION) |
        SetBits(dsp_xclks, DSP_XCLKS_PER_QW) |
        SetBits(ATIDisplayLoopLatency, DSP_LOOP_LATENCY);
}

    if (ATIChip < ATI_CHIP_264VTB)
        ATINewHWPtr->crtc_gen_cntl |= CRTC_FIFO_LWM;
return ;

}


static int rage_setmode(int mode, int prv_mode)
{

    unsigned char *moderegs;
    ModeTiming *modetiming;
    ModeInfo *modeinfo;
    ATIHWPtr ATINewHWPtr;

    if ((mode < G640x480x256 /*&& mode != G320x200x256*/)
	|| mode == G720x348x2) {

	return __svgalib_vga_driverspecs.setmode(mode, prv_mode);
    }
    if (!rage_modeavailable(mode))
	return 1;

    modeinfo = __svgalib_createModeInfoStructureForSvgalibMode(mode);

    modetiming = malloc(sizeof(ModeTiming));
    if (__svgalib_getmodetiming(modetiming, modeinfo, cardspecs)) {
	free(modetiming);
	free(modeinfo);
	return 1;
    }

    moderegs = malloc(MAX_REGS);
    
    ATINewHWPtr=(ATIHWPtr)(moderegs+VGA_TOTAL_REGS); 

    rage_initializemode(moderegs, modetiming, modeinfo, mode);
    free(modetiming);

    __svgalib_setregs(moderegs);
    rage_setregs(moderegs, mode);	
    free(moderegs);

    __svgalib_InitializeAcceleratorInterface(modeinfo);

    free(modeinfo);

    return 0;
}


/* Unlock chipset-specific registers */

static void rage_unlock(void)
{
}

static void rage_lock(void)
{
}


/* Indentify chipset, initialize and return non-zero if detected */

static int rage_test(void)
{
    int i, found;
    unsigned long buf[64];
    
    if (getenv("IOPERM") == NULL) {
      if (iopl(3) < 0) {
        printf("svgalib: rage: cannot get I/O permissions\n");
        exit(1);
      }
    }

   found=__svgalib_pci_find_vendor_vga(0x1002,buf,0);
   if(found)return 0;
   if (!found){
      ATIIOBase=buf[5]&BLOCK_IO_BASE;
   };

   if(ATIIOBase)
      {
         ATIIODecoding=BLOCK_IO;
         i=rage_probe();
      } else 
      {
         ATIIOBase=0x2EC; 	
         ATIIODecoding=SPARSE_IO;
         if(!(i=rage_probe())){
             ATIIOBase=0x1C8; 	
             if(!(i=rage_probe())){
                ATIIOBase=0x1CC;
                i=rage_probe();
             };
         };
      };

    if(!i)return 0;  
   
    rage_init(0,0,0);
    return 1;
}

/* Set display start address (not for 16 color modes) */
/* Cirrus supports any address in video memory (up to 2Mb) */

static void rage_setdisplaystart(int address)
{ 
unsigned int t;

address>>=3;
t=inl(ATIIOPortCRTC_OFF_PITCH);
outl(ATIIOPortCRTC_OFF_PITCH,(t&~CRTC_OFFSET)|address);

}

static void rage_setlogicalwidth(int width)
{   
unsigned int t;

if(rage_bpp>0)width=width/rage_bpp;
t=inl(ATIIOPortCRTC_OFF_PITCH);
outl(ATIIOPortCRTC_OFF_PITCH,(t&~CRTC_PITCH)|((width>>3)<<22));

}

static int rage_linear(int op, int param)
{
if (op==LINEAR_ENABLE || op==LINEAR_DISABLE){ rage_is_linear=1-rage_is_linear; return 0;}
if (op==LINEAR_QUERY_BASE) return rage_linear_base;
if (op == LINEAR_QUERY_RANGE || op == LINEAR_QUERY_GRANULARITY) return 0;		/* No granularity or range. */
    else return -1;		/* Unknown function. */
}

static int rage_match_programmable_clock(int clock)
{
return clock ;
}

static int rage_map_clock(int bpp, int clock)
{
return clock ;
}

static int rage_map_horizontal_crtc(int bpp, int pixelclock, int htiming)
{
return htiming;
}

/* Function table (exported) */

DriverSpecs __svgalib_rage_driverspecs =
{
    rage_saveregs,
    rage_setregs,
    rage_unlock,
    rage_lock,
    rage_test,
    rage_init,
    rage_setpage,
    rage_setrdpage,
    rage_setwrpage,
    rage_setmode,
    rage_modeavailable,
    rage_setdisplaystart,
    rage_setlogicalwidth,
    rage_getmodeinfo,
    0,				/* old blit funcs */
    0,
    0,
    0,
    0,
    0,				/* ext_set */
    0,				/* accel */
    rage_linear,
    0,				/* accelspecs, filled in during init. */
    NULL,                       /* Emulation */
};

/* Initialize chipset (called after detection) */

static int rage_init(int force, int par1, int par2)
{
   unsigned long buf[64];
   int found=0;
   int _ioperm=0;
   int i,j;
   unsigned char *BIOS;

   static int videoRamSizes[] = { 0 , 256 , 512 , 1024, 2*1024 ,
        	4*1024 , 6*1024 , 8*1024 , 12*1024 , 16*1024 , 0 };
   
   rage_unlock();
   if (force) {
	rage_memory = par1;
        rage_chiptyperev = par2;
   } else {

   };

   if (getenv("IOPERM") == NULL) {
     _ioperm=1;
     if (iopl(3) < 0) {
       printf("svgalib: rage: cannot get I/O permissions\n");
       exit(1);
     }
   }

   found=__svgalib_pci_find_vendor_vga(0x1002,buf,0);
   rage_linear_base=0;
   if (!found){
      rage_linear_base=buf[4]&0xffffff00ul;
      ATIIOBase=buf[5]&BLOCK_IO_BASE;
   };

   if(ATIIOBase)
      {
         ATIIODecoding=BLOCK_IO;
         i=rage_probe();
      } else 
      {
         ATIIOBase=0x2EC; 	
         ATIIODecoding=SPARSE_IO;
         if(!(i=rage_probe())){
             ATIIOBase=0x1C8; 	
             if(!(i=rage_probe())){
                ATIIOBase=0x1CC;
                i=rage_probe();
             };
         };
      };

   if(found || !i){
      printf("svgalib: Rage driver must be used, but not found\n");
      exit(1);
   };

   rage_chiptyperev=(buf[0]&0xffff0000) | (buf[2]&0xff);

   rage_ChipID();

   ATIIOPortCRTC_H_TOTAL_DISP=ATIIOPort(CRTC_H_TOTAL_DISP);
   ATIIOPortCRTC_H_SYNC_STRT_WID=ATIIOPort(CRTC_H_SYNC_STRT_WID);
   ATIIOPortCRTC_V_TOTAL_DISP=ATIIOPort(CRTC_V_TOTAL_DISP);
   ATIIOPortCRTC_V_SYNC_STRT_WID=ATIIOPort(CRTC_V_SYNC_STRT_WID);
   ATIIOPortCRTC_OFF_PITCH=ATIIOPort(CRTC_OFF_PITCH);
   ATIIOPortCRTC_INT_CNTL=ATIIOPort(CRTC_INT_CNTL);
   ATIIOPortCRTC_GEN_CNTL=ATIIOPort(CRTC_GEN_CNTL);
   ATIIOPortDSP_CONFIG=ATIIOPort(DSP_CONFIG);
   ATIIOPortDSP_ON_OFF=ATIIOPort(DSP_ON_OFF);
   ATIIOPortOVR_CLR=ATIIOPort(OVR_CLR);
   ATIIOPortOVR_WID_LEFT_RIGHT=ATIIOPort(OVR_WID_LEFT_RIGHT);
   ATIIOPortOVR_WID_TOP_BOTTOM=ATIIOPort(OVR_WID_TOP_BOTTOM);
   ATIIOPortCLOCK_CNTL=ATIIOPort(CLOCK_CNTL);
   ATIIOPortBUS_CNTL=ATIIOPort(BUS_CNTL);
   ATIIOPortMEM_INFO=ATIIOPort(MEM_INFO);
   ATIIOPortMEM_VGA_WP_SEL=ATIIOPort(MEM_VGA_WP_SEL);
   ATIIOPortMEM_VGA_RP_SEL=ATIIOPort(MEM_VGA_RP_SEL);
   ATIIOPortDAC_REGS=ATIIOPort(DAC_REGS);
   ATIIOPortDAC_CNTL=ATIIOPort(DAC_CNTL);
   ATIIOPortGEN_TEST_CNTL=ATIIOPort(GEN_TEST_CNTL);
   ATIIOPortCONFIG_CNTL=ATIIOPort(CONFIG_CNTL);
   ATIIOPortCONFIG_STATUS64_0=ATIIOPort(CONFIG_STATUS64_0);

   i = inl(ATIIOPort(MEM_INFO));
   j = inl(ATIIOPort(CONFIG_STATUS64_0));
   if(ATIChip<ATI_CHIP_264CT) {
     ATIMemoryType=GetBits(j,CFG_MEM_TYPE);
   } else {
     ATIMemoryType=GetBits(j,CFG_MEM_TYPE_T);
   };
   if(ATIChip>=ATI_CHIP_264VTB) {
     i = GetBits(i, CTL_MEM_SIZEB);
     if (i < 8) rage_memory = (i + 1) * 512;
       else if (i < 12) rage_memory = (i - 3) * 1024;
         else rage_memory = (i - 7) * 2048;
   } else {
     i = GetBits(i, CTL_MEM_SIZE);
     rage_memory = videoRamSizes[i+2];
   };

   if(ATIChip>=ATI_CHIP_264CT){
      M=ATIGetMach64PLLReg(PLL_REF_DIV);
      minN=2;
      maxN=255;
      fref=28636;
      Nadj=0;
      if(__svgalib_ragedoubleclock)fref*=2;
   } else {
      rage_dac=(j>>9)&7;
      if(rage_dac==5) {
         rage_clock=1;
         M=46;
         minN=257;
         maxN=512;
         Nadj=257;
         for(i=0;i<4;i++)postdiv[i]=1<<i;
         for(i=4;i<8;i++)postdiv[i]=0;
         fref=14318;
      };
   };

   if (__svgalib_driver_report) {
        printf("Using RAGE driver, %iKB.   ChipID:%i MemType:%i\n",rage_memory,ATIChip,ATIMemoryType);
        if(rage_dac)printf("Using external DAC:%i\n",rage_dac);
   }

   cardspecs = malloc(sizeof(CardSpecs));
   cardspecs->videoMemory = rage_memory;
   switch(ATIChip){
      case ATI_CHIP_264GTPRO:
      case ATI_CHIP_264LTPRO:
         cardspecs->maxPixelClock8bpp = 230000;	
         cardspecs->maxPixelClock16bpp = 230000;	
         cardspecs->maxPixelClock24bpp = 230000;
         cardspecs->maxPixelClock32bpp = 230000;
         break;
      case ATI_CHIP_264GTDVD:
      case ATI_CHIP_264LT:
      case ATI_CHIP_264VT4:
      case ATI_CHIP_264GT2C:
      case ATI_CHIP_264VT3:
         cardspecs->maxPixelClock8bpp = 200000;	
         cardspecs->maxPixelClock16bpp = 200000;	
         cardspecs->maxPixelClock24bpp = 200000;
         cardspecs->maxPixelClock32bpp = 200000;
         break;
      case ATI_CHIP_264VTB:
      case ATI_CHIP_264GTB:
         cardspecs->maxPixelClock8bpp = 170000;	
         cardspecs->maxPixelClock16bpp = 170000;	
         cardspecs->maxPixelClock24bpp = 170000;
         cardspecs->maxPixelClock32bpp = 170000;
         break;
      default:
         cardspecs->maxPixelClock8bpp = 135000;	
         cardspecs->maxPixelClock16bpp = 80000;	
         cardspecs->maxPixelClock24bpp = 40000;
         cardspecs->maxPixelClock32bpp = 40000;
   };
   cardspecs->flags = CLOCK_PROGRAMMABLE;
   cardspecs->maxHorizontalCrtc = 4088;
   cardspecs->nClocks = 0;
   cardspecs->maxPixelClock4bpp = 0;
   cardspecs->mapClock = rage_map_clock;
   cardspecs->mapHorizontalCrtc = rage_map_horizontal_crtc;
   cardspecs->matchProgrammableClock=rage_match_programmable_clock;
   __svgalib_driverspecs = &__svgalib_rage_driverspecs;
   __svgalib_banked_mem_base=0xa0000;
   __svgalib_banked_mem_size=0x10000;
   __svgalib_linear_mem_base=rage_linear_base;
   __svgalib_linear_mem_size=rage_memory*0x400;

#define BIOSWord(x) ((int)BIOS[x]+256*(int)BIOS[x+1])

   BIOS=mmap(0,32*1024,PROT_READ|PROT_WRITE,MAP_SHARED,__svgalib_mem_fd,0xc0000);

   i=BIOSWord(0x48);
   j=BIOSWord(i+0x10);

   ATIClockToProgram=BIOS[j+6];

   if(ATIChip>=ATI_CHIP_264CT){
      M=ATIGetMach64PLLReg(PLL_REF_DIV);
      minN=2;
      maxN=255;
      switch (BIOSWord(j+0x08)/10) {
         case 143:
            fref=14318; 
            break;
         case 286:
            fref=28636;
            break;
         default:
            fref=BIOSWord(j+0x08)*10;
            break;
      }
      Nadj=0;
      fref*=2; /* X says double for all chips */
#if 0
      if(__svgalib_ragedoubleclock)fref/=2; /* just in case */
#endif
      if (__svgalib_driver_report) {
          printf("Rage: BIOS reports base frequency=%.3fMHz  Denominator=%3i\n",fref/1000,M);
      }
   } else {
      rage_dac=(j>>9)&7;
      if(rage_dac==5) {
         rage_clock=1;
         M=46;
         minN=257;
         maxN=512;
         Nadj=257;
         for(i=0;i<4;i++)postdiv[i]=1<<i;
         for(i=4;i<8;i++)postdiv[i]=0;
         fref=14318;
      };
   };
   
   munmap(BIOS,32768);

   return 0;
}

#define WITHIN(v,c1,c2) (((v) >= (c1)) && ((v) <= (c2)))

static unsigned
comp_lmn(unsigned clock, int *np, int *mp, int *lp)
{
  int     n, m, l;
  double  fvco;
  double  fout;
  double  fvco_goal;
  
  for (m = M; m <= M; m++)
  {
    for (l = 0;(l < 8); l++) if(postdiv[l])
    {
      for (n = minN; n <= maxN; n++)
      {
        fout = ((double)(n) * fref)/((double)(m) * (postdiv[l]));
        fvco_goal = (double)clock * (double)(postdiv[l]);
        fvco = fout * (double)(postdiv[l]);
        if (!WITHIN(fvco, 0.995*fvco_goal, 1.005*fvco_goal))
          continue;
        *lp=l;
        *np=n-Nadj;
        *mp=m;
        return 1 ;
      }
    }
  }
printf("Can't do clock=%i\n",clock);
printf("fref=%f, M=%i, N in %i - %i\n",fref,M,minN,maxN);
{int i; for (i=0;i<8;i++)printf("%i ",postdiv[i]); printf("\n");};
  return 0;
}

