// SPDX-License-Identifier:	GPL-2.0+
/*
 * D1/R528/T113 DRAM controller register and constant defines
 *
 * (C) Copyright 2022 Arm Ltd.
 * (C) Copyright 2024 Iscle <albertiscle9@gmail.com>
 * 
 * Based on H6 and H616 header, which are:
 * (C) Copyright 2017  Icenowy Zheng <icenowy@aosc.io>
 * (C) Copyright 2020  Jernej Skrabec <jernej.skrabec@siol.net>
 *
 */

#ifndef _SUNXI_DRAM_SUN20I_D1_H
#define _SUNXI_DRAM_SUN20I_D1_H

enum sunxi_dram_type {
	SUNXI_DRAM_TYPE_DDR2 = 2,
	SUNXI_DRAM_TYPE_DDR3 = 3,
	SUNXI_DRAM_TYPE_LPDDR2 = 6,
	SUNXI_DRAM_TYPE_LPDDR3 = 7,
};

struct sunxi_sidc_reg {
	u8 reserved_0x00[0x21c];	/* 0x000 */
	u32 ldob_sid;				/* 0x21c */
	u8 reserved_0x220[0x8];		/* 0x220 */
	u32 efuse;					/* 0x228 */
};
check_member(sunxi_sidc_reg, ldob_sid, 0x21c);
check_member(sunxi_sidc_reg, efuse, 0x228);

struct sunxi_sramc_reg {
	u8 reserved_0x00[0x150];	/* 0x000 */
	u32 ldo_ctal;				/* 0x150 */
	u8 reserved_0x154[0xc];		/* 0x154 */
	u32 zq_cal_ctrl;			/* 0x160 */
	u8 reserved_0x164[0x4];		/* 0x164 */
	u32 zq_res_ctrl;			/* 0x168 */
	u32 zq_res_status;			/* 0x16c */
};
check_member(sunxi_sramc_reg, ldo_ctal, 0x150);
check_member(sunxi_sramc_reg, zq_cal_ctrl, 0x160);
check_member(sunxi_sramc_reg, zq_res_ctrl, 0x168);

struct sunxi_mctl_com_reg {
	u32 work_mode0;				/* 0x000 */
	u32 work_mode1; 			/* 0x004 */
	u32 dbgcr;					/* 0x008 */
	u32 tmr;					/* 0x00c */
	u8 reserved_0x10[0x4];		/* 0x010 */
	u32 cccr;					/* 0x014 */
	u8 reserved_0x18[0x8];		/* 0x018 */
	u32 maer0;					/* 0x020 */
	u32 maer1;					/* 0x024 */
	u32 maer2;					/* 0x028 */
	u8 reserved_0x2c[0x4d4];	/* 0x02c */
	u32 remap0;					/* 0x500 */
	u32 remap1;					/* 0x504 */
	u32 remap2;					/* 0x508 */
	u32 remap3;					/* 0x50c */
};
check_member(sunxi_mctl_com_reg, cccr, 0x014);
check_member(sunxi_mctl_com_reg, maer0, 0x020);
check_member(sunxi_mctl_com_reg, remap0, 0x500);

struct sunxi_mctl_phy_reg {
	u32 pir;					/* 0x000 */
	u32 pwrctl;					/* 0x004 */
	u32 mrctrl0;				/* 0x008 */
	u32 clken;					/* 0x00c */
	u32 pgsr0;					/* 0x010 */
	u32 pgsr1;					/* 0x014 */
	u32 statr;					/* 0x018 */
	u8 reserved_0x1c[0x10];		/* 0x01c */
	u32 lp3mr11;				/* 0x02c */
	u32 dram_mr0;				/* 0x030 */
	u32 dram_mr1;				/* 0x034 */
	u32 dram_mr2;				/* 0x038 */
	u32 dram_mr3;				/* 0x03c */
	u8 reserved_0x40[0x4];		/* 0x040 */
	u32 ptr0;					/* 0x044 */
	u8 reserved_0x48[0x4];		/* 0x048 */
	u32 ptr2;					/* 0x04c */
	u32 ptr3;					/* 0x050 */
	u32 ptr4;					/* 0x054 */
	u32 dramtmg0;				/* 0x058 */
	u32 dramtmg1;				/* 0x05c */
	u32 dramtmg2;				/* 0x060 */
	u32 dramtmg3;				/* 0x064 */
	u32 dramtmg4;				/* 0x068 */
	u32 dramtmg5;				/* 0x06c */
	u32 dramtmg6;				/* 0x070 */
	u32 dramtmg7;				/* 0x074 */
	u32 dramtmg8;				/* 0x078 */
	u32 odtcfg;					/* 0x07c */
	u32 pitmg0;					/* 0x080 */
	u32 pitmg1;					/* 0x084 */
	u32 lptpr;					/* 0x088 */
	u32 rfshctl0;				/* 0x08c */
	u32 rfshtmg;				/* 0x090 */
	u32 rfshctl1;				/* 0x094 */
	u32 pwrtmg;					/* 0x098 */
	u32 asrc;					/* 0x09c */
	u32 asrtc;					/* 0x0a0 */
	u8 reserved_0xa4[0x14];		/* 0x0a4 */
	u32 vtfcr;					/* 0x0b8 */
	u32 dqsgmr;					/* 0x0bc */
	u32 dtcr;					/* 0x0c0 */
	u32 dtar0;					/* 0x0c4 */
	u8 reserved_0xc8[0x38];		/* 0x0c8 */
	u32 pgcr0;					/* 0x100 */
	u32 pgcr1;					/* 0x104 */
	u32 pgcr2;					/* 0x108 */
	u32 pgcr3;					/* 0x10c */
	u32 iovcr0;					/* 0x110 */
	u32 iovcr1;					/* 0x114 */
	u8 reserved_0x118[0x4];		/* 0x118 */
	u32 dxccr;					/* 0x11c */
	u32 odtmap;					/* 0x120 */
	u32 zqctl0;					/* 0x124 */
	u32 zqctl1;					/* 0x128 */
	u8 reserved_0x12c[0x14];	/* 0x12c */
	u32 zqcr;					/* 0x140 */
	u32 zqsr;					/* 0x144 */
	u32 zqdr0;					/* 0x148 */
	u32 zqdr1;					/* 0x14c */
	u32 zqdr2;					/* 0x150 */
	u8 reserved_0x154[0x6c];	/* 0x154 */
	u32 sched;					/* 0x1c0 */
	u32 perfhpr0;				/* 0x1c4 */
	u32 perfhpr1;				/* 0x1c8 */
	u32 perflpr0;				/* 0x1cc */
	u32 perflpr1;				/* 0x1d0 */
	u32 perfwr0;				/* 0x1d4 */
	u32 perfwr1;				/* 0x1d8 */
	u8 reserved_0x1dc[0x24];	/* 0x1dc */
	u32 acmdlr;					/* 0x200 */
	u32 acldlr;					/* 0x204 */
	u32 aciocr0;				/* 0x208 */
};
check_member(sunxi_mctl_phy_reg, lp3mr11, 0x02c);
check_member(sunxi_mctl_phy_reg, ptr0, 0x044);
check_member(sunxi_mctl_phy_reg, ptr2, 0x04c);
check_member(sunxi_mctl_phy_reg, vtfcr, 0x0b8);
check_member(sunxi_mctl_phy_reg, pgcr0, 0x100);
check_member(sunxi_mctl_phy_reg, dxccr, 0x11c);
check_member(sunxi_mctl_phy_reg, zqcr, 0x140);
check_member(sunxi_mctl_phy_reg, sched, 0x1c0);
check_member(sunxi_mctl_phy_reg, acmdlr, 0x200);

#define ACIOCR1(x)		(SUNXI_DRAM_PHY_BASE + 0x210 + 0x4 * x)
#define DXnMDLR(x)		(SUNXI_DRAM_PHY_BASE + 0x300 + 0x80 * x)
#define DXnLDLR0(x)		(SUNXI_DRAM_PHY_BASE + 0x304 + 0x80 * x)
#define DXnLDLR1(x)		(SUNXI_DRAM_PHY_BASE + 0x308 + 0x80 * x)
#define DXnLDLR2(x)		(SUNXI_DRAM_PHY_BASE + 0x30c + 0x80 * x)
#define DXIOCR			(SUNXI_DRAM_PHY_BASE + 0x310)
#define DATX0IOCR(x)	(SUNXI_DRAM_PHY_BASE + 0x310 + 0x4 * x)
#define DATX1IOCR(x)	(SUNXI_DRAM_PHY_BASE + 0x390 + 0x4 * x)
#define DATX2IOCR(x)	(SUNXI_DRAM_PHY_BASE + 0x410 + 0x4 * x)
#define DATX3IOCR(x)	(SUNXI_DRAM_PHY_BASE + 0x490 + 0x4 * x)
#define DXnSDLR6(x)		(SUNXI_DRAM_PHY_BASE + 0x33c + 0x80 * x)
#define DXnGTR(x)		(SUNXI_DRAM_PHY_BASE + 0x340 + 0x80 * x)
#define DXnGCR0(x)		(SUNXI_DRAM_PHY_BASE + 0x344 + 0x80 * x)
#define DXnGSR0(x)		(SUNXI_DRAM_PHY_BASE + 0x348 + 0x80 * x)

/*
 * This structure contains a mixture of fixed configuration settings,
 * variables that are used at runtime to communicate settings between
 * different stages and functions, and unused values.
 * This is copied from Allwinner's boot0 data structure, which can be
 * found at offset 0x38 in any boot0 binary. To allow matching up some
 * board specific settings, this struct is kept compatible, even though
 * we don't need all members in our code.
 */
struct dram_para {
	/* normal configuration */
	const u32 clk;
	const u32 type;
	const u32 zq;
	const u32 odt_en;

	/* control configuration */
	u32 para1;
	u32 para2;

	/* timing configuration */
	const u32 mr0;
	const u32 mr1;
	const u32 mr2;
	const u32 mr3;
	const u32 tpr0;	/* DRAMTMG0 */
	const u32 tpr1;	/* DRAMTMG1 */
	const u32 tpr2;	/* DRAMTMG2 */
	const u32 tpr3;	/* DRAMTMG3 */
	const u32 tpr4;	/* DRAMTMG4 */
	const u32 tpr5;	/* DRAMTMG5 */
	const u32 tpr6;	/* DRAMTMG8 */
	const u32 tpr7;
	const u32 tpr8;
	const u32 tpr9;
	const u32 tpr10;
	const u32 tpr11;
	const u32 tpr12;
	/* contains a bitfield of DRAM setup settings */
	u32 tpr13;
};

#endif /* _SUNXI_DRAM_SUN20I_D1_H */
