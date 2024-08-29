// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner D1/D1s/R528/T113-sx/V851S DRAM initialisation
 *
 * As usual there is no documentation for the memory controller or PHY IP
 * used here. The baseline of this code was lifted from awboot[1], which
 * seems to be based on some form of de-compilation of some original Allwinner
 * code bits (with a GPL2 license tag from the very beginning).
 * This version here is a reworked version, to match the U-Boot coding style
 * and style of the other Allwinner DRAM drivers.
 *
 * [1] https://github.com/szemzoa/awboot.git
 */

#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/cpu.h>
#include <common.h>
#ifdef CONFIG_RAM
  #include <dm.h>
  #include <ram.h>
#endif
#include <linux/delay.h>

#include "dram_sun20i_d1.h"

#define SUNXI_R_CPUCFG_SUP_STAN_FLAG 0x1d4

#define VDD_SYS_PWROFF_GATING_REG 0x250
#define ANALOG_PWROFF_GATING_REG 0x254

#define SP_STDBY_FLAG_REG 0x1f8

static inline int ns_to_t(struct dram_para *para, int nanoseconds)
{
	const unsigned int ctrl_freq = para->clk / 2;

	return DIV_ROUND_UP(ctrl_freq * nanoseconds, 1000);
}

static void sid_read_ldoB_cal(struct dram_para *para)
{
	struct sunxi_sidc_reg * const sidc =
			(struct sunxi_sidc_reg *)SUNXI_SIDC_BASE;
	struct sunxi_sramc_reg * const sramc =
			(struct sunxi_sramc_reg *)SUNXI_SRAMC_BASE;
	
	uint32_t reg;

	reg = (readl(&sidc->ldob_sid) & 0xff00) >> 8;

	if (reg == 0)
		return;

	switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR2:
			break;
		case SUNXI_DRAM_TYPE_DDR3:
			if (reg > 0x20)
				reg -= 0x16;
			break;
		default:
			reg = 0;
			break;
	}

	clrsetbits_le32(&sramc->ldo_ctal, 0xff00, reg << 8);
}

static void dram_voltage_set(struct dram_para *para)
{
	struct sunxi_sramc_reg * const sramc =
			(struct sunxi_sramc_reg *)SUNXI_SRAMC_BASE;
	
	int vol;

	switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR2:
			vol = 47;
			break;
		case SUNXI_DRAM_TYPE_DDR3:
			vol = 25;
			break;
		default:
			vol = 0;
			break;
	}

	clrsetbits_le32(&sramc->ldo_ctal, 0x20ff00, vol << 8);

	udelay(1);

	sid_read_ldoB_cal(para);
}

static void dram_enable_all_master(void)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	
	writel(~0, &mctl_com->maer0);
	writel(0xff, &mctl_com->maer1);
	writel(0xffff, &mctl_com->maer2);
	udelay(10);
}

static void dram_disable_all_master(void)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	
	writel(1, &mctl_com->maer0);
	writel(0, &mctl_com->maer1);
	writel(0, &mctl_com->maer2);
	udelay(10);
}

static void eye_delay_compensation(struct dram_para *para)
{
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	uint32_t delay, i = 0;

	// DATn0IOCR, n =  0...7
	delay = (para->tpr11 & 0xf) << 9;
	delay |= (para->tpr12 & 0xf) << 1;
	for (i = 0; i < 9; i++)
		setbits_le32(DATX0IOCR(i), delay);

	// DATn1IOCR, n =  0...7
	delay = (para->tpr11 & 0xf0) << 5;
	delay |= (para->tpr12 & 0xf0) >> 3;
	for (i = 0; i < 9; i++)
		setbits_le32(DATX1IOCR(i), delay);

	// PGCR0: assert AC loopback FIFO reset
	clrbits_le32(&mctl_phy->pgcr0, 0x04000000);

	// DQS0 read and write delay
	delay = (para->tpr11 & 0xf0000) >> 7;
	delay |= (para->tpr12 & 0xf0000) >> 15;
	setbits_le32(DATX0IOCR(9), delay); // DQS0 P
	setbits_le32(DATX0IOCR(10), delay); // DQS0 N

	// DQS1 read and write delay
	delay = (para->tpr11 & 0xf00000) >> 11;
	delay |= (para->tpr12 & 0xf00000) >> 19;
	setbits_le32(DATX1IOCR(9), delay); // DQS1 P
	setbits_le32(DATX1IOCR(10), delay); // DQS1 N

	// DQS0 enable bit delay
	setbits_le32(DXnSDLR6(0), (para->tpr11 & 0xf0000) << 9);

	// DQS1 enable bit delay
	setbits_le32(DXnSDLR6(1), (para->tpr11 & 0xf00000) << 5);

	// PGCR0: release AC loopback FIFO reset
	setbits_le32(&mctl_phy->pgcr0, BIT(26));

	udelay(1);

	// Set RAS CAS and CA delay
	delay = (para->tpr10 & 0xf0) << 4;
	for (i = 6; i < 27; i++)
		setbits_le32(ACIOCR1(i), delay);

	// Set CK CS delay
	setbits_le32(ACIOCR1(2), (para->tpr10 & 0x0f) << 8);
	setbits_le32(ACIOCR1(3), (para->tpr10 & 0x0f) << 8);
	setbits_le32(ACIOCR1(28), (para->tpr10 & 0xf00) >> 4);
}

/*
 * Main purpose of the auto_set_timing routine seems to be to calculate all
 * timing settings for the specific type of sdram used. Read together with
 * an sdram datasheet for context on the various variables.
 */
static void mctl_set_timing_params(struct dram_para *para)
{
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	/* DRAM_TPR0 */
	u8 tccd = 2;
	u8 tfaw;
	u8 trrd;
	u8 trcd;
	u8 trc;

	/* DRAM_TPR1 */
	u8 txp;
	u8 twtr;
	u8 trtp = 4;
	u8 twr;
	u8 trp;
	u8 tras;

	/* DRAM_TPR2 */
	u16 trefi;
	u16 trfc;

	u8 tcksrx;
	u8 tckesr;
	u8 trd2wr;
	u8 twr2rd;
	u8 trasmax;
	u8 twtp;
	u8 tcke;
	u8 tmod;
	u8 tmrd;
	u8 tmrw;

	u8 tcl;
	u8 tcwl;
	u8 t_rdata_en;
	u8 wr_latency;

	u32 mr0;
	u32 mr1;
	u32 mr2;
	u32 mr3;

	u32 tdinit0;
	u32 tdinit1;
	u32 tdinit2;
	u32 tdinit3;

	switch (para->type) {
		case SUNXI_DRAM_TYPE_DDR2:
			/* DRAM_TPR0 */
			tfaw = ns_to_t(para, 50);
			trrd = ns_to_t(para, 10);
			trcd = ns_to_t(para, 20);
			trc	 = ns_to_t(para, 65);

			/* DRAM_TPR1 */
			txp	 = 2;
			twtr = ns_to_t(para, 8);
			twr	 = ns_to_t(para, 15);
			trp	 = ns_to_t(para, 15);
			tras = ns_to_t(para, 45);

			/* DRAM_TRP2 */
			trfc  = ns_to_t(para, 328);
			trefi = ns_to_t(para, 7800) / 32;

			trasmax = para->clk / 30;
			if (para->clk < 409) {
				t_rdata_en = 1;
				tcl		   = 3;
				mr0		   = 0x06a3;
			} else {
				t_rdata_en = 2;
				tcl		   = 4;
				mr0		   = 0x0e73;
			}
			tmrd	   = 2;
			twtp	   = twr + 5;
			tcksrx	   = 5;
			tckesr	   = 4;
			trd2wr	   = 4;
			tcke	   = 3;
			tmod	   = 12;
			wr_latency = 1;
			tmrw	   = 0;
			twr2rd	   = twtr + 5;
			tcwl	   = 0;

			mr1 = para->mr1;
			mr2 = 0;
			mr3 = 0;

			tdinit0 = 200 * para->clk + 1;
			tdinit1 = 100 * para->clk / 1000 + 1;
			tdinit2 = 200 * para->clk + 1;
			tdinit3 = 1 * para->clk + 1;

			break;
		case SUNXI_DRAM_TYPE_DDR3:
			trfc  = ns_to_t(para, 350);
			trefi = ns_to_t(para, 7800) / 32 + 1; // XXX

			twtr = ns_to_t(para, 8) + 2; // + 2 ? XXX
			/* Only used by trd2wr calculation, which gets discard below */
//			twr		= max(ns_to_t(para, 15), 2);
			trrd = max(ns_to_t(para, 10), 2);
			txp	 = max(ns_to_t(para, 10), 2);

			if (para->clk <= 800) {
				tfaw = ns_to_t(para, 50);
				trcd = ns_to_t(para, 15);
				trp	 = ns_to_t(para, 15);
				trc	 = ns_to_t(para, 53);
				tras = ns_to_t(para, 38);

				mr0		   = 0x1c70;
				mr2		   = 0x18;
				tcl		   = 6;
				wr_latency = 2;
				tcwl	   = 4;
				t_rdata_en = 4;
			} else {
				tfaw = ns_to_t(para, 35);
				trcd = ns_to_t(para, 14);
				trp	 = ns_to_t(para, 14);
				trc	 = ns_to_t(para, 48);
				tras = ns_to_t(para, 34);

				mr0		   = 0x1e14;
				mr2		   = 0x20;
				tcl		   = 7;
				wr_latency = 3;
				tcwl	   = 5;
				t_rdata_en = 5;
			}

			trasmax = para->clk / 30;
			twtp	= tcwl + 2 + twtr; // WL+BL/2+tWTR
			/* Gets overwritten below */
			//		trd2wr		= tcwl + 2 + twr;		// WL+BL/2+tWR
			twr2rd = tcwl + twtr; // WL+tWTR

			tdinit0 = 500 * para->clk + 1; // 500 us
			tdinit1 = 360 * para->clk / 1000 + 1; // 360 ns
			tdinit2 = 200 * para->clk + 1; // 200 us
			tdinit3 = 1 * para->clk + 1; //   1 us

			mr1	   = para->mr1;
			mr3	   = 0;
			tcke   = 3;
			tcksrx = 5;
			tckesr = 4;
			if (((para->tpr13 & 0xc) == 0x04) || para->clk < 912)
				trd2wr = 5;
			else
				trd2wr = 6;

			tmod = 12;
			tmrd = 4;
			tmrw = 0;

			break;
		case SUNXI_DRAM_TYPE_LPDDR2:
			tfaw = max(ns_to_t(para, 50), 4);
			trrd = max(ns_to_t(para, 10), 1);
			trcd = max(ns_to_t(para, 24), 2);
			trc	 = ns_to_t(para, 70);
			txp	 = ns_to_t(para, 8);
			if (txp < 2) {
				txp++;
				twtr = 2;
			} else {
				twtr = txp;
			}
			twr	  = max(ns_to_t(para, 15), 2);
			trp	  = ns_to_t(para, 17);
			tras  = ns_to_t(para, 42);
			trefi = ns_to_t(para, 3900) / 32;
			trfc  = ns_to_t(para, 210);

			trasmax	   = para->clk / 60;
			mr3		   = para->mr3;
			twtp	   = twr + 5;
			mr2		   = 6;
			mr1		   = 5;
			tcksrx	   = 5;
			tckesr	   = 5;
			trd2wr	   = 10;
			tcke	   = 2;
			tmod	   = 5;
			tmrd	   = 5;
			tmrw	   = 3;
			tcl		   = 4;
			wr_latency = 1;
			t_rdata_en = 1;

			tdinit0 = 200 * para->clk + 1;
			tdinit1 = 100 * para->clk / 1000 + 1;
			tdinit2 = 11 * para->clk + 1;
			tdinit3 = 1 * para->clk + 1;
			twr2rd	= twtr + 5;
			tcwl	= 2;
			mr1		= 195;
			mr0		= 0;

			break;
		case SUNXI_DRAM_TYPE_LPDDR3:
			tfaw  = max(ns_to_t(para, 50), 4);
			trrd  = max(ns_to_t(para, 10), 1);
			trcd  = max(ns_to_t(para, 24), 2);
			trc	  = ns_to_t(para, 70);
			twtr  = max(ns_to_t(para, 8), 2);
			twr	  = max(ns_to_t(para, 15), 2);
			trp	  = ns_to_t(para, 17);
			tras  = ns_to_t(para, 42);
			trefi = ns_to_t(para, 3900) / 32;
			trfc  = ns_to_t(para, 210);
			txp	  = twtr;

			trasmax = para->clk / 60;
			if (para->clk < 800) {
				tcwl	   = 4;
				wr_latency = 3;
				t_rdata_en = 6;
				mr2		   = 12;
			} else {
				tcwl	   = 3;
				tcke	   = 6;
				wr_latency = 2;
				t_rdata_en = 5;
				mr2		   = 10;
			}
			twtp	= tcwl + 5;
			tcl		= 7;
			mr3		= para->mr3;
			tcksrx	= 5;
			tckesr	= 5;
			trd2wr	= 13;
			tcke	= 3;
			tmod	= 12;
			tdinit0 = 400 * para->clk + 1;
			tdinit1 = 500 * para->clk / 1000 + 1;
			tdinit2 = 11 * para->clk + 1;
			tdinit3 = 1 * para->clk + 1;
			tmrd	= 5;
			tmrw	= 5;
			twr2rd	= tcwl + twtr + 5;
			mr1		= 195;
			mr0		= 0;

			break;
		default:
			trfc  = 128;
			trp	  = 6;
			trefi = 98;
			txp	  = 10;
			twr	  = 8;
			twtr  = 3;
			tras  = 14;
			tfaw  = 16;
			trc	  = 20;
			trcd  = 6;
			trrd  = 3;

			twr2rd		= 8;
			tcksrx		= 4;
			tckesr		= 3;
			trd2wr		= 4;
			trasmax		= 27;
			twtp		= 12;
			tcke		= 2;
			tmod		= 6;
			tmrd		= 2;
			tmrw		= 0;
			tcwl		= 3;
			tcl			= 3;
			wr_latency	= 1;
			t_rdata_en	= 1;
			mr3			= 0;
			mr2			= 0;
			mr1			= 0;
			mr0			= 0;
			tdinit3		= 0;
			tdinit2		= 0;
			tdinit1		= 0;
			tdinit0		= 0;

			break;
	}

	/* Set mode registers */
	writel(mr0, &mctl_phy->dram_mr0);
	writel(mr1, &mctl_phy->dram_mr1);
	writel(mr2, &mctl_phy->dram_mr2);
	writel(mr3, &mctl_phy->dram_mr3);
	/* TODO: dram_odt_en is either 0x0 or 0x1, so right shift looks weird */
	writel((para->odt_en >> 4) & 0x3, &mctl_phy->lp3mr11);

	/* Set dram timing DRAMTMG0 - DRAMTMG5 */
	writel((twtp << 24) | (tfaw << 16) | (trasmax << 8) | (tras << 0),
		&mctl_phy->dramtmg0);
	writel((txp << 16) | (trtp << 8) | (trc << 0),
		&mctl_phy->dramtmg1);
	writel((tcwl << 24) | (tcl << 16) | (trd2wr << 8) | (twr2rd << 0),
		&mctl_phy->dramtmg2);
	writel((tmrw << 16) | (tmrd << 12) | (tmod << 0),
		&mctl_phy->dramtmg3);
	writel((trcd << 24) | (tccd << 16) | (trrd << 8) | (trp << 0),
		&mctl_phy->dramtmg4);
	writel((tcksrx << 24) | (tcksrx << 16) | (tckesr << 8) | (tcke << 0),
		&mctl_phy->dramtmg5);

	/* Set dual rank timing */
	clrsetbits_le32(&mctl_phy->dramtmg8, 0xf000ffff,
			(para->clk < 800) ? 0xf0006610 : 0xf0007610);

	/* Set phy interface time PITMG0, PTR3, PTR4 */
	writel((0x2 << 24) | (t_rdata_en << 16) | BIT(8) | (wr_latency << 0),
		&mctl_phy->pitmg0);
	writel(((tdinit0 << 0) | (tdinit1 << 20)), &mctl_phy->ptr3);
	writel(((tdinit2 << 0) | (tdinit3 << 20)), &mctl_phy->ptr4);

	/* Set refresh timing and mode */
	writel((trefi << 16) | (trfc << 0), &mctl_phy->rfshtmg);
	writel((trefi << 15) & 0x0fff0000, &mctl_phy->rfshctl1);
}

// Purpose of this routine seems to be to initialize the PLL driving
// the MBUS and sdram.
//
static int ccu_set_pll_ddr_clk(int index, struct dram_para *para)
{
	struct sunxi_ccm_reg * const ccm =
			(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;

	unsigned int val, clk, n;

	if (para->tpr13 & BIT(6))
		clk = para->tpr9;
	else
		clk = para->clk;

	// set VCO clock divider
	n = (clk * 2) / 24;

	val = readl(&ccm->pll5_cfg);
	val &= ~0x0007ff03;			// clear dividers
	val |= (n - 1) << 8;		// set PLL division
	val |= BIT(31) | BIT(30);	// enable PLL and LDO
	writel(val | BIT(29), &ccm->pll5_cfg);

	// wait for PLL to lock
	while ((readl(&ccm->pll5_cfg) & BIT(28)) == 0) {}

	udelay(20);

	// enable PLL output
	setbits_le32(&ccm->pll1_cfg, BIT(27));

	// turn clock gate on
	val = readl(&ccm->dram_clk_cfg);
	val &= ~0x03000303;		// select DDR clk source, n=1, m=1
	val |= BIT(31);			// turn clock on
	writel(val, &ccm->dram_clk_cfg);

	return n * 24;
}

/* Set up the PLL and clock gates for the DRAM controller and MBUS clocks. */
static void mctl_sys_init(struct dram_para *para)
{
	struct sunxi_ccm_reg * const ccm =
			(struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	// assert MBUS reset
	clrbits_le32(&ccm->mbus_cfg, BIT(30));

	// turn off sdram clock gate, assert sdram reset
	clrbits_le32(&ccm->dram_gate_reset, 0x10001);
	clrsetbits_le32(&ccm->dram_clk_cfg, BIT(31) | BIT(30), BIT(27));
	udelay(10);

	// set ddr pll clock
	ccu_set_pll_ddr_clk(0, para);
	udelay(100);
	dram_disable_all_master();

	// release sdram reset
	setbits_le32(&ccm->dram_gate_reset, BIT(16));

	// release MBUS reset
	setbits_le32(&ccm->mbus_cfg, BIT(30));
	setbits_le32(&ccm->dram_clk_cfg, BIT(30));

	udelay(5);

	// turn on sdram clock gate
	setbits_le32(&ccm->dram_gate_reset, BIT(0));

	// turn dram clock gate on, trigger sdr clock update
	setbits_le32(&ccm->dram_clk_cfg, BIT(31) | BIT(27));
	udelay(5);

	// mCTL clock enable
	writel(0x8000, &mctl_phy->clken);
	udelay(10);
}

// The main purpose of this routine seems to be to copy an address configuration
// from the dram_para1 and dram_para2 fields to the PHY configuration registers
// (MCTL_COM_WORK_MODE0, MCTL_COM_WORK_MODE1).
//
static void mctl_com_init(struct dram_para *para)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	uint32_t val, width;
	u32 *ptr;
	int i;

	// Setting controller wait time
	clrsetbits_le32(&mctl_com->dbgcr, 0x3f00, 0x2000);

	// set SDRAM type and word width
	val = readl(&mctl_com->work_mode0) & ~0x00fff000;
	val |= (para->type & 0x7) << 16;	// DRAM type
	val |= (~para->para2 & 0x1) << 12;	// DQ width
	val |= BIT(22);						// ??
	if (para->type == SUNXI_DRAM_TYPE_LPDDR2 ||
		para->type == SUNXI_DRAM_TYPE_LPDDR3) {
		val |= BIT(19); // type 6 and 7 must use 1T
	} else {
		if (para->tpr13 & BIT(5))
			val |= BIT(19);
	}
	writel(val, &mctl_com->work_mode0);

	// init rank / bank / row for single/dual or two different ranks
	if ((para->para2 & BIT(8)) && ((para->para2 & 0xf000) != 0x1000))
		width = 32;
	else
		width = 16;

	ptr = &mctl_com->work_mode0;
	for (i = 0; i < width; i += 16) {
		val = readl(ptr) & 0xfffff000;

		val |= (para->para2 >> 12) & 0x3; // rank
		val |= ((para->para1 >> (i + 12)) << 2) & 0x4; // bank - 2
		val |= (((para->para1 >> (i + 4)) - 1) << 4) & 0xff; // row - 1

		// convert from page size to column addr width - 3
		switch ((para->para1 >> i) & 0xf) {
		case 8: val |= 0xa00; break;
		case 4: val |= 0x900; break;
		case 2: val |= 0x800; break;
		case 1: val |= 0x700; break;
		default: val |= 0x600; break;
		}
		writel(val, ptr);
		ptr++;
	}

	// set ODTMAP based on number of ranks in use
	val = (readl(&mctl_com->work_mode0) & 0x1) ? 0x303 : 0x201;
	writel(val, &mctl_phy->odtmap);

	// set mctl reg 3c4 to zero when using half DQ
	if (para->para2 & BIT(0))
		writel(0, DXnGCR0(1));

	// set dram address mapping from dram_tpr4 param
	if (para->tpr4) {
		setbits_le32(&mctl_com->work_mode0, (para->tpr4 & 0x3) << 25);
		setbits_le32(&mctl_com->work_mode1, (para->tpr4 & 0x7fc) << 10);
	}
}

static const uint8_t ac_remapping_tables[][22] = {
	[0] = { 0 },
	[1] = {  1,  9,  3,  7,  8, 18,  4, 13,  5,  6, 10,
		 2, 14, 12,  0,  0, 21, 17, 20, 19, 11, 22 }, // Generic DDR3 Type1
	[2] = {  4,  9,  3,  7,  8, 18,  1, 13,  2,  6, 10,
		 5, 14, 12,  0,  0, 21, 17, 20, 19, 11, 22 }, // Generic DDR3 Type C
	[3] = {  1,  7,  8, 12, 10, 18,  4, 13,  5,  6,  3,
		 2,  9,  0,  0,  0, 21, 17, 20, 19, 11, 22 }, // Generic DDR3 Type 8
	[4] = {  4, 12, 10,  7,  8, 18,  1, 13,  2,  6,  3,
		 5,  9,  0,  0,  0, 21, 17, 20, 19, 11, 22 }, // Generic DDR3 Type 9
	[5] = { 13,  2,  7,  9, 12, 19,  5,  1,  6,  3,  4,
		 8, 10,  0,  0,  0, 21, 22, 18, 17, 11, 20 }, // Generic DDR3 Type bf
	[6] = {  3, 10,  7, 13,  9, 11,  1,  2,  4,  6,  8,
		 5, 12,  0,  0,  0, 20,  1,  0, 21, 22, 17 }, // DDR2
	[7] = {  3,  2,  4,  7,  9,  1, 17, 12, 18, 14, 13,
		 8, 15,  6, 10,  5, 19, 22, 16, 21, 20, 11 }, // DDR3 D1-H
};

/*
 * This routine chooses one of several remapping tables for 22 lines.
 * It is unclear which lines are being remapped. It seems to pick
 * table cfg7 for the Nezha board.
 */
static void mctl_phy_ac_remapping(struct dram_para *para)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	
	const uint8_t *cfg;
	uint32_t fuse, val;

	/*
	 * It is unclear whether the LPDDRx types don't need any remapping,
	 * or whether the original code just didn't provide tables.
	 */
	if (para->type != SUNXI_DRAM_TYPE_DDR2 &&
		para->type != SUNXI_DRAM_TYPE_DDR3)
		return;

	fuse = (readl(SUNXI_SID_BASE + 0x28) & 0xf00) >> 8;
	debug("DDR efuse: 0x%x\n", fuse);

	if (para->type == SUNXI_DRAM_TYPE_DDR2) {
		if (fuse == 15)
			return;
		cfg = ac_remapping_tables[6];
	} else {
		if (para->tpr13 & 0xc0000) {
			cfg = ac_remapping_tables[7];
		} else {
			switch (fuse) {
			case 8: cfg = ac_remapping_tables[2]; break;
			case 9: cfg = ac_remapping_tables[3]; break;
			case 10: cfg = ac_remapping_tables[5]; break;
			case 11: cfg = ac_remapping_tables[4]; break;
			default:
			case 12: cfg = ac_remapping_tables[1]; break;
			case 13:
			case 14: cfg = ac_remapping_tables[0]; break;
			}
		}
	}

	val = (cfg[4] << 25) | (cfg[3] << 20) | (cfg[2] << 15) |
		  (cfg[1] << 10) | (cfg[0] << 5);
	writel(val, &mctl_com->remap0);

	val = (cfg[10] << 25) | (cfg[9] << 20) | (cfg[8] << 15) |
		  (cfg[ 7] << 10) | (cfg[6] <<  5) | cfg[5];
	writel(val, &mctl_com->remap1);

	val = (cfg[15] << 20) | (cfg[14] << 15) | (cfg[13] << 10) |
		  (cfg[12] <<  5) | cfg[11];
	writel(val, &mctl_com->remap2);

	val = (cfg[21] << 25) | (cfg[20] << 20) | (cfg[19] << 15) |
		  (cfg[18] << 10) | (cfg[17] <<  5) | cfg[16];
	writel(val, &mctl_com->remap3);
}

// Init the controller channel. The key part is placing commands in the main
// command register (PIR, 0x3103000) and checking command status (PGSR0, 0x3103010).
//
static unsigned int mctl_channel_init(unsigned int ch_index,
					  struct dram_para *para)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	unsigned int val, dqs_gating_mode;

	dqs_gating_mode = (para->tpr13 & 0xc) >> 2;

	// set DDR clock to half of CPU clock
	clrsetbits_le32(&mctl_com->tmr, 0xfff, (para->clk / 2) - 1);

	// MRCTRL0 nibble 3 undocumented
	clrsetbits_le32(&mctl_phy->pgcr2, 0xf00, 0x300);

	if (para->odt_en)
		val = 0;
	else
		val = BIT(5);

	// DX0GCR0
	if (para->clk > 672)
		clrsetbits_le32(DXnGCR0(0), 0xf63e, val);
	else
		clrsetbits_le32(DXnGCR0(0), 0xf03e, val);

	// DX1GCR0
	if (para->clk > 672) {
		setbits_le32(DXnGCR0(0), 0x400);
		clrsetbits_le32(DXnGCR0(1), 0xf63e, val);
	} else {
		clrsetbits_le32(DXnGCR0(1), 0xf03e, val);
	}

	// ACIOCR0 undocumented
	setbits_le32(&mctl_phy->aciocr0, BIT(1));

	eye_delay_compensation(para);

	// set PLL SSCG ?
	val = readl(&mctl_phy->pgcr2);
	if (dqs_gating_mode == 1) {
		clrsetbits_le32(&mctl_phy->pgcr2, 0xc0, 0);
		clrbits_le32(&mctl_phy->dqsgmr, 0x107);
	} else if (dqs_gating_mode == 2) {
		clrsetbits_le32(&mctl_phy->pgcr2, 0xc0, 0x80);

		clrsetbits_le32(&mctl_phy->dqsgmr, 0x107,
				(((para->tpr13 >> 16) & 0x1f) - 2) | 0x100);
		clrsetbits_le32(&mctl_phy->dxccr, BIT(31), BIT(27));
	} else {
		clrbits_le32(&mctl_phy->pgcr2, 0x40);
		udelay(10);
		setbits_le32(&mctl_phy->pgcr2, 0xc0);
	}

	if (para->type == SUNXI_DRAM_TYPE_LPDDR2 ||
		para->type == SUNXI_DRAM_TYPE_LPDDR3) {
		if (dqs_gating_mode == 1)
			clrsetbits_le32(&mctl_phy->dxccr, 0x080000c0, 0x80000000);
		else
			clrsetbits_le32(&mctl_phy->dxccr, 0x77000000, 0x22000000);
	}

	clrsetbits_le32(&mctl_phy->dtcr, 0x0fffffff,
			(para->para2 & BIT(12)) ? 0x03000001 : 0x01000007);

	if (readl((SUNXI_R_CPUCFG_BASE + SUNXI_R_CPUCFG_SUP_STAN_FLAG)) & (1 << 16)) {
		clrbits_le32((SUNXI_PRCM_BASE + VDD_SYS_PWROFF_GATING_REG), 0x2);
		udelay(10);
	}

	// Set ZQ config
	clrsetbits_le32(&mctl_phy->zqcr, 0x3ffffff,
			(para->zq & 0x00ffffff) | BIT(25));

	// Initialise DRAM controller
	if (dqs_gating_mode == 1) {
		//writel(0x52, &mctl_phy->pir); // prep PHY reset + PLL init + z-cal
		writel(0x53, &mctl_phy->pir); // Go

		while ((readl(&mctl_phy->pgsr0) & 0x1) == 0) {} // wait for IDONE
		udelay(10);

		// 0x520 = prep DQS gating + DRAM init + d-cal
		if (para->type == SUNXI_DRAM_TYPE_DDR3)
			writel(0x5a0, &mctl_phy->pir); // + DRAM reset
		else
			writel(0x520, &mctl_phy->pir);
	} else {
		if ((readl((SUNXI_R_CPUCFG_BASE + SUNXI_R_CPUCFG_SUP_STAN_FLAG)) & (1 << 16)) == 0) {
			// prep DRAM init + PHY reset + d-cal + PLL init + z-cal
			if (para->type == SUNXI_DRAM_TYPE_DDR3)
				writel(0x1f2, &mctl_phy->pir); // + DRAM reset
			else
				writel(0x172, &mctl_phy->pir);
		} else {
			// prep PHY reset + d-cal + z-cal
			writel(0x62, &mctl_phy->pir);
		}
	}

	setbits_le32(&mctl_phy->pir, BIT(0)); // GO

	udelay(10);
	while ((readl(&mctl_phy->pgsr0) & BIT(0)) == 0) {} // wait for IDONE

	if (readl((SUNXI_R_CPUCFG_BASE + SUNXI_R_CPUCFG_SUP_STAN_FLAG)) & BIT(16)) {
		clrsetbits_le32(&mctl_phy->pgcr3, 0x06000000, 0x04000000);
		udelay(10);

		setbits_le32(&mctl_phy->pwrctl, BIT(0));

		while ((readl(&mctl_phy->statr) & 0x7) != 0x3) {}

		clrbits_le32((SUNXI_PRCM_BASE + VDD_SYS_PWROFF_GATING_REG), BIT(0));
		udelay(10);

		clrbits_le32(&mctl_phy->pwrctl, BIT(0));

		while ((readl(&mctl_phy->statr) & 0x7) != 0x1) {}

		udelay(15);

		if (dqs_gating_mode == 1) {
			clrbits_le32(&mctl_phy->pgcr2, 0xc0);
			clrsetbits_le32(&mctl_phy->pgcr3, 0x06000000, 0x02000000);
			udelay(1);
			writel(0x401, &mctl_phy->pir);

			while ((readl(&mctl_phy->pgsr0) & BIT(0)) == 0) {}
		}
	}

	// Check for training error
	if (readl(&mctl_phy->pgsr0) & BIT(20)) {
		printf("ZQ calibration error, check external 240 ohm resistor\n");
		return 0;
	}

	// STATR = Zynq STAT? Wait for status 'normal'?
	while ((readl(&mctl_phy->statr) & BIT(0)) == 0) {}

	setbits_le32(&mctl_phy->rfshctl0, BIT(31));
	udelay(10);
	clrbits_le32(&mctl_phy->rfshctl0, BIT(31));
	udelay(10);
	setbits_le32(&mctl_com->cccr, BIT(31));
	udelay(10);

	clrbits_le32(&mctl_phy->pgcr3, 0x06000000);

	if (dqs_gating_mode == 1)
		clrsetbits_le32(&mctl_phy->dxccr, 0xc0, 0x40);

	return 1;
}

static unsigned int calculate_rank_size(uint32_t regval)
{
	unsigned int bits;

	bits = (regval >> 8) & 0xf;		/* page size - 3 */
	bits += (regval >> 4) & 0xf;	/* row width - 1 */
	bits += (regval >> 2) & 0x3;	/* bank count - 2 */
	bits -= 14;						/* 1MB = 20 bits, minus above 6 = 14 */

	return 1U << bits;
}

/*
 * The below routine reads the dram config registers and extracts
 * the number of address bits in each rank available. It then calculates
 * total memory size in MB.
 */
static unsigned int DRAMC_get_dram_size(void)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	
	uint32_t val;
	unsigned int size;

	val = readl(&mctl_com->work_mode0);
	size = calculate_rank_size(val);

	if ((val & 0x3) == 0) /* single rank? */
		return size;

	val = readl(&mctl_com->work_mode1);
	if ((val & 0x3) == 0) /* two identical ranks? */
		return size * 2;

	/* add sizes of both ranks */
	return size + calculate_rank_size(val);
}

/*
 * The below routine reads the command status register to extract
 * DQ width and rank count. This follows the DQS training command in
 * channel_init. If error bit 22 is reset, we have two ranks and full DQ.
 * If there was an error, figure out whether it was half DQ, single rank,
 * or both. Set bit 12 and 0 in dram_para2 with the results.
 */
static int dqs_gate_detect(struct dram_para *para)
{
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	uint32_t dx0 = 0, dx1 = 0;

	if ((readl(&mctl_phy->pgsr0) & BIT(22)) == 0) {
		para->para2 = (para->para2 & ~0xf) | BIT(12);
		debug("dual rank and full DQ\n");

		return 1;
	}

	dx0 = (readl(DXnGSR0(0)) & 0x3000000) >> 24;
	if (dx0 == 0) {
		para->para2 = (para->para2 & ~0xf) | 0x1001;
		debug("dual rank and half DQ\n");

		return 1;
	}

	if (dx0 == 2) {
		dx1 = (readl(DXnGSR0(1)) & 0x3000000) >> 24;
		if (dx1 == 2) {
			para->para2 = para->para2 & ~0xf00f;
			debug("single rank and full DQ\n");
		} else {
			para->para2 = (para->para2 & ~0xf00f) | BIT(0);
			debug("single rank and half DQ\n");
		}

		return 1;
	}

	if ((para->tpr13 & BIT(29)) == 0)
		return 0;

	debug("DX0 state: %d\n", dx0);
	debug("DX1 state: %d\n", dx1);

	return 0;
}

static int dramc_simple_wr_test(unsigned int mem_mb, int len)
{
	unsigned int  offs	= (mem_mb / 2) << 18; // half of memory size
	unsigned int  patt1 = 0x01234567;
	unsigned int  patt2 = 0xfedcba98;
	unsigned int *addr, v1, v2, i;

	addr = (unsigned int *)CFG_SYS_SDRAM_BASE;
	for (i = 0; i != len; i++, addr++) {
		writel(patt1 + i, (unsigned long)addr);
		writel(patt2 + i, (unsigned long)(addr + offs));
	}

	addr = (unsigned int *)CFG_SYS_SDRAM_BASE;
	for (i = 0; i != len; i++) {
		v1 = readl((unsigned long)(addr + i));
		v2 = patt1 + i;
		if (v1 != v2) {
			printf("DRAM: simple test FAIL\n");
			printf("%x != %x at address %p\n", v1, v2, addr + i);
			return 1;
		}
		v1 = readl((unsigned long)(addr + offs + i));
		v2 = patt2 + i;
		if (v1 != v2) {
			printf("DRAM: simple test FAIL\n");
			printf("%x != %x at address %p\n", v1, v2, addr + offs + i);
			return 1;
		}
	}

	debug("DRAM: simple test OK\n");
	return 0;
}

// Set the Vref mode for the controller
//
static void mctl_vrefzq_init(struct dram_para *para)
{
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	if (para->tpr13 & BIT(17))
		return;

	clrsetbits_le32(&mctl_phy->iovcr0, 0x7f7f7f7f, para->tpr5);

	// IOCVR1
	if ((para->tpr13 & BIT(16)) == 0)
		clrsetbits_le32(&mctl_phy->iovcr1, 0x7f, para->tpr6 & 0x7f);
}

// Perform an init of the controller. This is actually done 3 times. The first
// time to establish the number of ranks and DQ width. The second time to
// establish the actual ram size. The third time is final one, with the final
// settings.
//
static int mctl_core_init(struct dram_para *para)
{
	mctl_sys_init(para);

	mctl_vrefzq_init(para);

	mctl_com_init(para);
#if 0
	mctl_phy_ac_remapping(para);
#endif
	mctl_set_timing_params(para);

	return mctl_channel_init(0, para);
}

/*
 * This routine sizes a DRAM device by cycling through address lines and
 * figuring out if they are connected to a real address line, or if the
 * address is a mirror.
 * First the column and bank bit allocations are set to low values (2 and 9
 * address lines). Then a maximum allocation (16 lines) is set for rows and
 * this is tested.
 * Next the BA2 line is checked. This seems to be placed above the column,
 * BA0-1 and row addresses. Finally, the column address is allocated 13 lines
 * and these are tested. The results are placed in dram_para1 and dram_para2.
 */
static int auto_scan_dram_size(struct dram_para *para)
{
	struct sunxi_mctl_com_reg * const mctl_com =
			(struct sunxi_mctl_com_reg *)SUNXI_DRAM_COM_BASE;
	
	uint32_t i = 0, j = 0, current_rank = 0;
	uint32_t rank_count = 1, addr_line = 0;
	uint32_t reg_val = 0, ret = 0, cnt = 0;
	u32 *mc_work_mode;
	uint32_t rank1_addr = CFG_SYS_SDRAM_BASE;

	// init core
	if (mctl_core_init(para) == 0) {
		debug("DRAM initial error : 0!\n");
		return 0;
	}

	// Set rank_count to 2
	if ((((para->para2 >> 12) & 0xf) == 0x1))
		rank_count = 2;

	for (current_rank = 0; current_rank < rank_count; current_rank++) {
		mc_work_mode = (&mctl_com->work_mode0 + 4 * current_rank);

		/* Set 16 Row 4Bank 512BPage for Rank 1 */
		if (current_rank == 1) {
			clrsetbits_le32(&mctl_com->work_mode0, 0xf0c, 0x6f0);
			clrsetbits_le32(&mctl_com->work_mode1, 0xf0c, 0x6f0);
			/* update Rank 1 addr */
			rank1_addr = CFG_SYS_SDRAM_BASE + (0x1 << 27);
		}

		/* write test pattern */
		for (i = 0; i < 64; i++) {
			writel((i % 2) ? (CFG_SYS_SDRAM_BASE + 4 * i) : (~(CFG_SYS_SDRAM_BASE + 4 * i)),
				   CFG_SYS_SDRAM_BASE + 4 * i);
		}
		/* set row mode */
		clrsetbits_le32(mc_work_mode, 0xf0c, 0x6f0);
		udelay(2);

		for (i = 11; i < 17; i++) {
			ret = CFG_SYS_SDRAM_BASE + (1 << (i + 2 + 9)); /* row-bank-column */
			cnt = 0;
			for (j = 0; j < 64; j++) {
				reg_val = (j % 2) ? (rank1_addr + 4 * j) : (~(rank1_addr + 4 * j));
				if (reg_val == readl(ret + j * 4)) {
					cnt++;
				} else
					break;
			}
			if (cnt == 64) {
				break;
			}
		}
		if (i >= 16)
			i = 16;
		addr_line += i;

		debug("rank %d row = %d \n", current_rank, i);

		/* Store rows in para 1 */
		para->para1 &= ~(0xffU << (16 * current_rank + 4));
		para->para1 |= (i << (16 * current_rank + 4));
		debug("para->para1 = 0x%x\n", para->para1);

		/* Set bank mode for current rank */
		if (current_rank == 1) { /* Set bank mode for rank0 */
			clrsetbits_le32(&mctl_com->work_mode0, 0xffc, 0x6a4);
		}

		/* Set bank mode for current rank */
		clrsetbits_le32(mc_work_mode, 0xffc, 0x6a4);
		udelay(1);

		for (i = 0; i < 1; i++) {
			ret = CFG_SYS_SDRAM_BASE + (0x1U << (i + 2 + 9));
			cnt = 0;
			for (j = 0; j < 64; j++) {
				reg_val = (j % 2) ? (rank1_addr + 4 * j) : (~(rank1_addr + 4 * j));
				if (reg_val == readl(ret + j * 4)) {
					cnt++;
				} else
					break;
			}
			if (cnt == 64) {
				break;
			}
		}

		addr_line += i + 2;
		debug("rank %d bank = %d \n", current_rank, (4 + i * 4));

		/* Store bank in para 1 */
		para->para1 &= ~(0xfU << (16 * current_rank + 12));
		para->para1 |= (i << (16 * current_rank + 12));
		debug("para->para1 = 0x%x\n", para->para1);

		/* Set page mode for rank0 */
		if (current_rank == 1) {
			clrsetbits_le32(mc_work_mode, 0xffc, 0xaa0);
		}

		/* Set page mode for current rank */
		clrsetbits_le32(mc_work_mode, 0xffc, 0xaa0);
		udelay(2);

		/* Scan per address line, until address wraps (i.e. see shadow) */
		for (i = 9; i <= 13; i++) {
			ret = CFG_SYS_SDRAM_BASE + (0x1U << i); // column 40000000+（9~13）
			cnt = 0;
			for (j = 0; j < 64; j++) {
				reg_val = (j % 2) ? (CFG_SYS_SDRAM_BASE + 4 * j) : (~(CFG_SYS_SDRAM_BASE + 4 * j));
				if (reg_val == readl(ret + j * 4)) {
					cnt++;
				} else {
					break;
				}
			}
			if (cnt == 64) {
				break;
			}
		}

		if (i >= 13) {
			i = 13;
		}

		/* add page size */
		addr_line += i;

		if (i == 9) {
			i = 0;
		} else {
			i = (0x1U << (i - 10));
		}

		debug("rank %d page size = %d KB \n", current_rank, i);

		/* Store page in para 1 */
		para->para1 &= ~(0xfU << (16 * current_rank));
		para->para1 |= (i << (16 * current_rank));
		debug("para->para1 = 0x%x\n", para->para1);
	}

	/* check dual rank config */
	if (rank_count == 2) {
		para->para2 &= 0xfffff0ff;
		if ((para->para1 & 0xffff) == (para->para1 >> 16)) {
			debug("rank1 config same as rank0\n");
		} else {
			para->para2 |= 0x1 << 8;
			debug("rank1 config different from rank0\n");
		}
	}
	return 1;
}

/*
 * This routine sets up parameters with dqs_gating_mode equal to 1 and two
 * ranks enabled. It then configures the core and tests for 1 or 2 ranks and
 * full or half DQ width. It then resets the parameters to the original values.
 * dram_para2 is updated with the rank and width findings.
 */
static int auto_scan_dram_rank_width(struct dram_para *para)
{
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	unsigned int s1 = para->tpr13;
	unsigned int s2 = para->para1;

	para->para1 = 0x00b000b0;
	para->para2 = (para->para2 & ~0xf) | BIT(12);

	/* set DQS probe mode */
	para->tpr13 = (para->tpr13 & ~0x8) | BIT(2) | BIT(0);

	mctl_core_init(para);

	if (readl(&mctl_phy->pgsr0) & BIT(20))
		return 0;

	if (dqs_gate_detect(para) == 0)
		return 0;

	para->tpr13 = s1;
	para->para1 = s2;

	return 1;
}

/*
 * This routine determines the SDRAM topology. It first establishes the number
 * of ranks and the DQ width. Then it scans the SDRAM address lines to establish
 * the size of each rank. It then updates dram_tpr13 to reflect that the sizes
 * are now known: a re-init will not repeat the autoscan.
 */
static int auto_scan_dram_config(struct dram_para *para)
{
	if (((para->tpr13 & BIT(14)) == 0) &&
		(auto_scan_dram_rank_width(para) == 0)) {
		printf("ERROR: auto scan dram rank & width failed\n");
		return 0;
	}

	if (((para->tpr13 & BIT(0)) == 0) &&
		(auto_scan_dram_size(para) == 0)) {
		printf("ERROR: auto scan dram size failed\n");
		return 0;
	}

	if ((para->tpr13 & BIT(15)) == 0)
		para->tpr13 |= BIT(14) | BIT(13) | BIT(1) | BIT(0);

	return 1;
}

static int init_dram(struct dram_para *para)
{
	struct sunxi_sramc_reg * const sramc =
			(struct sunxi_sramc_reg *)SUNXI_SRAMC_BASE;
	struct sunxi_mctl_phy_reg * const mctl_phy =
			(struct sunxi_mctl_phy_reg *)SUNXI_DRAM_PHY_BASE;
	
	u32 rc, mem_size_mb;

	debug("DRAM BOOT DRIVE INFO: %s\n", "V0.24");
	debug("DRAM CLK = %d MHz\n", para->clk);
	debug("DRAM Type = %d (2:DDR2,3:DDR3)\n", para->type);
	if ((para->odt_en & 0x1) == 0)
		debug("DRAMC read ODT off\n");
	else
		debug("DRAMC ZQ value: 0x%x\n", para->zq);

	/* Test ZQ status */
	if (para->tpr13 & BIT(16)) {
		debug("DRAM only have internal ZQ\n");
		setbits_le32(&sramc->zq_cal_ctrl, BIT(8));
		writel(0, &sramc->zq_res_ctrl);
		udelay(10);
	} else {
		clrbits_le32(&sramc->zq_cal_ctrl, 0x3);
		writel(para->tpr13 & (1 << 16), (SUNXI_PRCM_BASE + ANALOG_PWROFF_GATING_REG));
		udelay(10);
		clrsetbits_le32(&sramc->zq_cal_ctrl, 0x108, BIT(1));
		udelay(10);
		setbits_le32(&sramc->zq_cal_ctrl, BIT(0));
		udelay(20);
		debug("ZQ value = 0x%x\n", readl(&sramc->zq_res_status));
	}

	dram_voltage_set(para);

	/* Set SDRAM controller auto config */
	if ((para->tpr13 & BIT(0)) == 0) {
		if (auto_scan_dram_config(para) == 0) {
			printf("auto_scan_dram_config() failed\n");
			return 0;
		}
	}

	/* report ODT */
	rc = para->mr1;
	if ((rc & 0x44) == 0)
		debug("DRAM ODT off\n");
	else
		debug("DRAM ODT value: 0x%x\n", rc);

	/* Init core, final run */
	if (mctl_core_init(para) == 0) {
		printf("mctl_core_init() failed\n");
		return 0;
	}

	/* Get SDRAM size
	 * You can set para2 to force set the dram size
	 * TODO: who ever puts a negative number in the top half?
	 */
	rc = para->para2;
	if (rc & BIT(31)) {
		rc = (rc >> 16) & ~BIT(15);
	} else {
		rc = DRAMC_get_dram_size();
		debug("DRAM: size = %dMB\n", rc);
		para->para2 = (para->para2 & 0xffffu) | rc << 16;
	}
	mem_size_mb = rc;

	/* Enable hardware auto refresh */
	if (para->tpr13 & BIT(30)) {
		rc = para->tpr8;
		if (rc == 0)
			rc = 0x10000200;
		writel(rc, &mctl_phy->asrtc);
		writel(0x40a, &mctl_phy->asrc);
		setbits_le32(&mctl_phy->pwrctl, BIT(0));
		debug("Enable Auto SR\n");
	} else {
		clrbits_le32(&mctl_phy->asrtc, 0xffff);
		clrbits_le32(&mctl_phy->pwrctl, 0x1);
	}

	/* Set HDR/DDR dynamic */
	if (para->tpr13 & BIT(9)) {
		clrsetbits_le32(&mctl_phy->pgcr0, 0xf000, 0x5000);
	} else {
		if (para->type != SUNXI_DRAM_TYPE_LPDDR2)
			clrbits_le32(&mctl_phy->pgcr0, 0xf000);
	}

	/* Disable ZQ calibration */
	setbits_le32(&mctl_phy->zqcr, BIT(31));

	/* Set VTF feature */
	if (para->tpr13 & BIT(8))
		writel(readl(&mctl_phy->vtfcr) | 0x300, &mctl_phy->vtfcr);

	/* Set PAD Hold */
	if (para->tpr13 & BIT(16))
		clrbits_le32(&mctl_phy->pgcr2, BIT(13));
	else
		setbits_le32(&mctl_phy->pgcr2, BIT(13));

	/* Set LPDDR3 ODT delay */
	if (para->type == SUNXI_DRAM_TYPE_LPDDR3)
		clrsetbits_le32(&mctl_phy->odtcfg, 0xf0000, 0x1000);

	dram_enable_all_master();
	if (para->tpr13 & BIT(28)) {
		if ((readl(SUNXI_RTC_BASE + SP_STDBY_FLAG_REG) & BIT(16)) || dramc_simple_wr_test(mem_size_mb, 4096))
			return 0;
	}

	return mem_size_mb;
}

unsigned long sunxi_dram_init(void)
{
	struct dram_para para = {
		.clk = CONFIG_DRAM_CLK,
		.type = CONFIG_SUNXI_DRAM_TYPE,
		.zq = CONFIG_DRAM_ZQ,
		.odt_en = CONFIG_DRAM_SUNXI_ODT_EN,
		.para1 = CONFIG_DRAM_SUNXI_PARA1,
		.para2 = 0x0,
		.mr1 = CONFIG_DRAM_SUNXI_MR1,
		.mr3 = 0,
		.tpr4 = 0,
		.tpr5 = 0x48484848,
		.tpr6 = 0x00000048,
		.tpr8 = 0,
		.tpr9 = 0, // clock?
		.tpr10 = 0,
		.tpr11 = CONFIG_DRAM_SUNXI_TPR11,
		.tpr12 = CONFIG_DRAM_SUNXI_TPR12,
		.tpr13 = CONFIG_DRAM_SUNXI_TPR13,
	};

	return init_dram(&para) * 1024UL * 1024;
}

#ifdef CONFIG_RAM		/* using the driver model */
struct sunxi_ram_priv {
	size_t size;
};

static int sunxi_ram_probe(struct udevice *dev)
{
	struct sunxi_ram_priv *priv = dev_get_priv(dev);
	unsigned long dram_size;

	debug("%s: %s: probing\n", __func__, dev->name);

	dram_size = sunxi_dram_init();
	if (!dram_size) {
		printf("DRAM init failed\n");
		return -ENODEV;
	}

	priv->size = dram_size;

	return 0;
}

static int sunxi_ram_get_info(struct udevice *dev, struct ram_info *info)
{
	struct sunxi_ram_priv *priv = dev_get_priv(dev);

	debug("%s: %s: getting info\n", __func__, dev->name);

	info->base = CFG_SYS_SDRAM_BASE;
	info->size = priv->size;

	return 0;
}

static struct ram_ops sunxi_ram_ops = {
	.get_info = sunxi_ram_get_info,
};

static const struct udevice_id sunxi_ram_ids[] = {
	{ .compatible = "allwinner,sun20i-d1-mbus" },
	{ }
};

U_BOOT_DRIVER(sunxi_ram) = {
	.name = "sunxi_ram",
	.id = UCLASS_RAM,
	.of_match = sunxi_ram_ids,
	.ops = &sunxi_ram_ops,
	.probe = sunxi_ram_probe,
	.priv_auto = sizeof(struct sunxi_ram_priv),
};
#endif				/* CONFIG_RAM (using driver model) */