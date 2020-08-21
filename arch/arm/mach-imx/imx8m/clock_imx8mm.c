// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 */

#include <common.h>
#include <asm/arch/clock.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/sys_proto.h>
#include <asm/io.h>
#include <div64.h>
#include <errno.h>
#include <linux/iopoll.h>

DECLARE_GLOBAL_DATA_PTR;

static struct anamix_pll *ana_pll = (struct anamix_pll *)ANATOP_BASE_ADDR;

#ifdef CONFIG_SECURE_BOOT
void hab_caam_clock_enable(unsigned char enable)
{
	/* The CAAM clock is always on for iMX8M */
}
#endif

#ifdef CONFIG_MXC_OCOTP
void enable_ocotp_clk(unsigned char enable)
{
	clock_enable(CCGR_OCOTP, !!enable);
}
#endif

int enable_i2c_clk(unsigned char enable, unsigned i2c_num)
{
	/* 0 - 3 is valid i2c num */
	if (i2c_num > 3)
		return -EINVAL;

	clock_enable(CCGR_I2C1 + i2c_num, !!enable);

	return 0;
}

static u32 decode_intpll(enum clk_root_src intpll)
{
	struct ana_grp2 *pll;
	u32 gnrl_ctl, div_ctl, pll_clke_mask;
	u32 main_div, pre_div, post_div, div;
	u64 freq;

	switch (intpll) {
	case ARM_PLL_CLK:
		pll = &ana_pll->arm_pll;
		break;
	case GPU_PLL_CLK:
		pll = &ana_pll->gpu_pll;
		break;
	case VPU_PLL_CLK:
		pll = &ana_pll->vpu_pll;
		break;
	case SYSTEM_PLL1_800M_CLK:
	case SYSTEM_PLL1_400M_CLK:
	case SYSTEM_PLL1_266M_CLK:
	case SYSTEM_PLL1_200M_CLK:
	case SYSTEM_PLL1_160M_CLK:
	case SYSTEM_PLL1_133M_CLK:
	case SYSTEM_PLL1_100M_CLK:
	case SYSTEM_PLL1_80M_CLK:
	case SYSTEM_PLL1_40M_CLK:
		pll = &ana_pll->sys_pll1;
		break;
	case SYSTEM_PLL2_1000M_CLK:
	case SYSTEM_PLL2_500M_CLK:
	case SYSTEM_PLL2_333M_CLK:
	case SYSTEM_PLL2_250M_CLK:
	case SYSTEM_PLL2_200M_CLK:
	case SYSTEM_PLL2_166M_CLK:
	case SYSTEM_PLL2_125M_CLK:
	case SYSTEM_PLL2_100M_CLK:
	case SYSTEM_PLL2_50M_CLK:
		pll = &ana_pll->sys_pll2;
		break;
	case SYSTEM_PLL3_CLK:
		pll = &ana_pll->sys_pll3;
		break;
	default:
		printf("int PLL %d not supporte\n", intpll);
		return -EINVAL;
	}
	gnrl_ctl = readl(&pll->gnrl_ctl);
	div_ctl = readl(&pll->div_ctl);

	/* Only support SYS_XTAL 24M, PAD_CLK not take into consideration */
	if ((gnrl_ctl & INTPLL_REF_CLK_SEL_MASK) != 0)
		return 0;

	if ((gnrl_ctl & INTPLL_RST_MASK) == 0)
		return 0;

	/*
	 * When BYPASS is equal to 1, PLL enters the bypass mode
	 * regardless of the values of RESETB
	 */
	if (gnrl_ctl & INTPLL_BYPASS_MASK)
		return 24000000u;

	if (!(gnrl_ctl & INTPLL_LOCK_MASK)) {
		puts("pll not locked\n");
		return -EINVAL;
	}

	switch (intpll) {
	case ARM_PLL_CLK:
	case GPU_PLL_CLK:
	case VPU_PLL_CLK:
	case SYSTEM_PLL3_CLK:
	case SYSTEM_PLL1_800M_CLK:
	case SYSTEM_PLL2_1000M_CLK:
		pll_clke_mask = INTPLL_CLKE_MASK;
		div = 1;
		break;

	case SYSTEM_PLL1_400M_CLK:
	case SYSTEM_PLL2_500M_CLK:
		pll_clke_mask = INTPLL_DIV2_CLKE_MASK;
		div = 2;
		break;

	case SYSTEM_PLL1_266M_CLK:
	case SYSTEM_PLL2_333M_CLK:
		pll_clke_mask = INTPLL_DIV3_CLKE_MASK;
		div = 3;
		break;

	case SYSTEM_PLL1_200M_CLK:
	case SYSTEM_PLL2_250M_CLK:
		pll_clke_mask = INTPLL_DIV4_CLKE_MASK;
		div = 4;
		break;

	case SYSTEM_PLL1_160M_CLK:
	case SYSTEM_PLL2_200M_CLK:
		pll_clke_mask = INTPLL_DIV5_CLKE_MASK;
		div = 5;
		break;

	case SYSTEM_PLL1_133M_CLK:
	case SYSTEM_PLL2_166M_CLK:
		pll_clke_mask = INTPLL_DIV6_CLKE_MASK;
		div = 6;
		break;

	case SYSTEM_PLL1_100M_CLK:
	case SYSTEM_PLL2_125M_CLK:
		pll_clke_mask = INTPLL_DIV8_CLKE_MASK;
		div = 8;
		break;

	case SYSTEM_PLL1_80M_CLK:
	case SYSTEM_PLL2_100M_CLK:
		pll_clke_mask = INTPLL_DIV10_CLKE_MASK;
		div = 10;
		break;

	case SYSTEM_PLL1_40M_CLK:
	case SYSTEM_PLL2_50M_CLK:
		pll_clke_mask = INTPLL_DIV20_CLKE_MASK;
		div = 20;
		break;
	default:
		printf("int pll %d not supported\n", intpll);
		return -EINVAL;
	}

	if ((gnrl_ctl & pll_clke_mask) == 0)
		return 0;

	main_div = (div_ctl & INTPLL_MAIN_DIV_MASK) >>
		INTPLL_MAIN_DIV_SHIFT;
	pre_div = (div_ctl & INTPLL_PRE_DIV_MASK) >>
		INTPLL_PRE_DIV_SHIFT;
	post_div = (div_ctl & INTPLL_POST_DIV_MASK) >>
		INTPLL_POST_DIV_SHIFT;

	/* FFVCO = (m * FFIN) / p, FFOUT = (m * FFIN) / (p * 2^s) */
	freq = 24000000ULL * main_div;
	return lldiv(freq, pre_div * (1 << post_div) * div);
}

static u32 decode_fracpll(enum clk_root_src frac_pll)
{
	struct ana_grp *pll;
	u32 gnrl_ctl, fdiv_ctl0, fdiv_ctl1;
	u32 main_div, pre_div, post_div, k;

	switch (frac_pll) {
	case DRAM_PLL1_CLK:
		pll = &ana_pll->dram_pll;
		break;
	case AUDIO_PLL1_CLK:
		pll = &ana_pll->audio_pll1;
		break;
	case AUDIO_PLL2_CLK:
		pll = &ana_pll->audio_pll2;
		break;
	case VIDEO_PLL_CLK:
		pll = &ana_pll->video_pll1;
		break;
	default:
		printf("Not supported\n");
		return 0;
	}
	gnrl_ctl = readl(&pll->gnrl_ctl);
	fdiv_ctl0 = readl(&pll->fdiv_ctl0);
	fdiv_ctl1 = readl(&pll->fdiv_ctl1);

	/* Only support SYS_XTAL 24M, PAD_CLK not take into consideration */
	if ((gnrl_ctl & INTPLL_REF_CLK_SEL_MASK) != 0)
		return 0;

	if ((gnrl_ctl & INTPLL_RST_MASK) == 0)
		return 0;
	/*
	 * When BYPASS is equal to 1, PLL enters the bypass mode
	 * regardless of the values of RESETB
	 */
	if (gnrl_ctl & INTPLL_BYPASS_MASK)
		return 24000000u;

	if (!(gnrl_ctl & INTPLL_LOCK_MASK)) {
		puts("pll not locked\n");
		return 0;
	}

	if (!(gnrl_ctl & INTPLL_CLKE_MASK))
		return 0;

	main_div = (fdiv_ctl0 & INTPLL_MAIN_DIV_MASK) >>
		INTPLL_MAIN_DIV_SHIFT;
	pre_div = (fdiv_ctl0 & INTPLL_PRE_DIV_MASK) >>
		INTPLL_PRE_DIV_SHIFT;
	post_div = (fdiv_ctl0 & INTPLL_POST_DIV_MASK) >>
		INTPLL_POST_DIV_SHIFT;

	k = fdiv_ctl1 & GENMASK(15, 0);

	/* FFOUT = ((m + k / 65536) * FFIN) / (p * 2^s), 1 ≤ p ≤ 63, 64 ≤ m ≤ 1023, 0 ≤ s ≤ 6 */
	return lldiv((main_div * 65536 + k) * 24000000ULL, 65536 * pre_div * (1 << post_div));
}

enum intpll_out_freq {
	INTPLL_OUT_600M,
	INTPLL_OUT_750M,
	INTPLL_OUT_800M,
	INTPLL_OUT_1200M,
	INTPLL_OUT_1000M,
	INTPLL_OUT_2000M,
};

#define PLL_1443X_RATE(_rate, _m, _p, _s, _k)			\
	{							\
		.rate	=	(_rate),			\
		.mdiv	=	(_m),				\
		.pdiv	=	(_p),				\
		.sdiv	=	(_s),				\
		.kdiv	=	(_k),				\
	}

#define LOCK_STATUS 	BIT(31)
#define LOCK_SEL_MASK	BIT(29)
#define CLKE_MASK	BIT(11)
#define RST_MASK	BIT(9)
#define BYPASS_MASK	BIT(4)
#define	MDIV_SHIFT	12
#define	MDIV_MASK	GENMASK(21, 12)
#define PDIV_SHIFT	4
#define PDIV_MASK	GENMASK(9, 4)
#define SDIV_SHIFT	0
#define SDIV_MASK	GENMASK(2, 0)
#define KDIV_SHIFT	0
#define KDIV_MASK	GENMASK(15, 0)

struct imx_int_pll_rate_table {
	u32 rate;
	int mdiv;
	int pdiv;
	int sdiv;
	int kdiv;
};

#define DRAM_BYPASS_ROOT_CONFIG(_rate, _m, _p, _s, _k)			\
	{							\
		.clk	=	(_rate),			\
		.alt_root_sel	=	(_m),				\
		.alt_pre_div	=	(_p),				\
		.apb_root_sel	=	(_s),				\
		.apb_pre_div	=	(_k),				\
	}

struct dram_bypass_clk_setting {
	ulong clk;
	int alt_root_sel;
	enum root_pre_div alt_pre_div;
	int apb_root_sel;
	enum root_pre_div apb_pre_div;
};

#define VIDEO_PLL_RATE 594000000U

#ifdef CONFIG_SPL_BUILD
static struct imx_int_pll_rate_table imx8mm_fracpll_tbl[] = {
	PLL_1443X_RATE(800000000U, 300, 9, 0, 0),
	PLL_1443X_RATE(750000000U, 250, 8, 0, 0),
	PLL_1443X_RATE(650000000U, 325, 3, 2, 0),
	PLL_1443X_RATE(600000000U, 300, 3, 2, 0),
	PLL_1443X_RATE(594000000U, 99, 1, 2, 0),
	PLL_1443X_RATE(400000000U, 300, 9, 1, 0),
	PLL_1443X_RATE(100000000U, 300, 9, 3, 0),
};

static struct dram_bypass_clk_setting imx8mm_dram_bypass_tbl[] = {
	DRAM_BYPASS_ROOT_CONFIG(MHZ(100), 2, CLK_ROOT_PRE_DIV1, 2, CLK_ROOT_PRE_DIV2),
	DRAM_BYPASS_ROOT_CONFIG(MHZ(250), 3, CLK_ROOT_PRE_DIV2, 2, CLK_ROOT_PRE_DIV2),
	DRAM_BYPASS_ROOT_CONFIG(MHZ(400), 1, CLK_ROOT_PRE_DIV2, 3, CLK_ROOT_PRE_DIV2),
};

int fracpll_configure(enum pll_clocks clock, u32 freq)
{
	int i;
	u32 tmp, div_val;
	struct ana_grp* pll;
	struct imx_int_pll_rate_table *rate;
	u32 val;
	int ret;

	for (i = 0; i < ARRAY_SIZE(imx8mm_fracpll_tbl); i++) {
		if (freq == imx8mm_fracpll_tbl[i].rate)
			break;
	}

	if (i == ARRAY_SIZE(imx8mm_fracpll_tbl)) {
		printf("No matched freq table %u\n", freq);
		return -EINVAL;
	}

	rate = &imx8mm_fracpll_tbl[i];

	switch (clock) {
	case ANATOP_DRAM_PLL:
#define SRC_DDR1_ENABLE_MASK	0x8F000000UL
		setbits_le32(GPC_BASE_ADDR + 0xEC, 1 << 7);
		setbits_le32(GPC_BASE_ADDR + 0xF8, 1 << 5);
		writel(SRC_DDR1_ENABLE_MASK, SRC_BASE_ADDR + 0x1004);

		pll = &ana_pll->dram_pll;
		break;
	case ANATOP_VIDEO_PLL:
		pll = &ana_pll->video_pll1;
		break;
	default:
		return -EINVAL;
	}
	/* Bypass clock and set lock to pll output lock */
	tmp = readl(&pll->gnrl_ctl);
	tmp |= BYPASS_MASK;
	writel(tmp, &pll->gnrl_ctl);

	/* Enable RST */
	tmp &= ~RST_MASK;
	writel(tmp, &pll->gnrl_ctl);

	div_val = (rate->mdiv << MDIV_SHIFT) | (rate->pdiv << PDIV_SHIFT) |
		(rate->sdiv << SDIV_SHIFT);
	writel(div_val, &pll->fdiv_ctl0);
	writel(rate->kdiv << KDIV_SHIFT, &pll->fdiv_ctl1);

	__udelay(100);

	/* Disable RST */
	tmp |= RST_MASK;
	writel(tmp, &pll->gnrl_ctl);

	/* Wait Lock*/
	ret = readl_poll_timeout(&pll->gnrl_ctl, val, val & LOCK_STATUS, 600);
	if (ret)
		printf("%s timeout\n", __func__);

	/* Bypass */
	tmp &= ~BYPASS_MASK;
	writel(tmp, &pll->gnrl_ctl);

	return ret;
}

void dram_pll_init(ulong pll_val)
{
	fracpll_configure(ANATOP_DRAM_PLL, pll_val);
}

void dram_enable_bypass(ulong clk_val)
{
	int i;
	struct dram_bypass_clk_setting *config;

	for (i = 0; i < ARRAY_SIZE(imx8mm_dram_bypass_tbl); i++) {
		if (clk_val == imx8mm_dram_bypass_tbl[i].clk)
			break;
	}

	if (i == ARRAY_SIZE(imx8mm_dram_bypass_tbl)) {
		printf("No matched freq table %lu\n", clk_val);
		return;
	}

	config = &imx8mm_dram_bypass_tbl[i];

	clock_set_target_val(DRAM_ALT_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(config->alt_root_sel) | CLK_ROOT_PRE_DIV(config->alt_pre_div));
	clock_set_target_val(DRAM_APB_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(config->apb_root_sel) | CLK_ROOT_PRE_DIV(config->apb_pre_div));
	clock_set_target_val(DRAM_SEL_CFG, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
}

void dram_disable_bypass(void)
{
	clock_set_target_val(DRAM_SEL_CFG, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_set_target_val(DRAM_APB_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(4) | CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV5));
}

int intpll_configure(enum pll_clocks clock, enum intpll_out_freq freq)
{
	struct ana_grp2 *pll;
	u32 div_ctl_val, pll_clke_masks;
	u32 val;
	int ret;

	switch (clock) {
	case ANATOP_SYSTEM_PLL1:
		pll = &ana_pll->sys_pll1;
		pll_clke_masks = INTPLL_DIV20_CLKE_MASK |
			INTPLL_DIV10_CLKE_MASK | INTPLL_DIV8_CLKE_MASK |
			INTPLL_DIV6_CLKE_MASK | INTPLL_DIV5_CLKE_MASK |
			INTPLL_DIV4_CLKE_MASK | INTPLL_DIV3_CLKE_MASK |
			INTPLL_DIV2_CLKE_MASK | INTPLL_CLKE_MASK;
		break;
	case ANATOP_SYSTEM_PLL2:
		pll = &ana_pll->sys_pll2;
		pll_clke_masks = INTPLL_DIV20_CLKE_MASK |
			INTPLL_DIV10_CLKE_MASK | INTPLL_DIV8_CLKE_MASK |
			INTPLL_DIV6_CLKE_MASK | INTPLL_DIV5_CLKE_MASK |
			INTPLL_DIV4_CLKE_MASK | INTPLL_DIV3_CLKE_MASK |
			INTPLL_DIV2_CLKE_MASK | INTPLL_CLKE_MASK;
		break;
	case ANATOP_SYSTEM_PLL3:
		pll = &ana_pll->sys_pll3;
		pll_clke_masks = INTPLL_CLKE_MASK;
		break;
	case ANATOP_ARM_PLL:
		pll = &ana_pll->arm_pll;
		pll_clke_masks = INTPLL_CLKE_MASK;
		break;
	case ANATOP_GPU_PLL:
		pll = &ana_pll->gpu_pll;
		pll_clke_masks = INTPLL_CLKE_MASK;
		break;
	case ANATOP_VPU_PLL:
		pll = &ana_pll->vpu_pll;
		pll_clke_masks = INTPLL_CLKE_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (freq) {
	case INTPLL_OUT_600M:
		/* 24 * 0x12c / 3 / 2 ^ 2 */
		div_ctl_val = INTPLL_MAIN_DIV_VAL(0x12c) |
			INTPLL_PRE_DIV_VAL(3) | INTPLL_POST_DIV_VAL(2);
		break;
	case INTPLL_OUT_750M:
		/* 24 * 0xfa / 2 / 2 ^ 2 */
		div_ctl_val = INTPLL_MAIN_DIV_VAL(0xfa) |
			INTPLL_PRE_DIV_VAL(2) | INTPLL_POST_DIV_VAL(2);
		break;
	case INTPLL_OUT_800M:
		/* 24 * 0x190 / 3 / 2 ^ 2 */
		div_ctl_val = INTPLL_MAIN_DIV_VAL(0x190) |
			INTPLL_PRE_DIV_VAL(3) | INTPLL_POST_DIV_VAL(2);
		break;
	case INTPLL_OUT_1000M:
		/* 24 * 0xfa / 3 / 2 ^ 1 */
		div_ctl_val = INTPLL_MAIN_DIV_VAL(0xfa) |
			INTPLL_PRE_DIV_VAL(3) | INTPLL_POST_DIV_VAL(1);
		break;
	case INTPLL_OUT_1200M:
		/* 24 * 0xc8 / 2 / 2 ^ 1 */
		div_ctl_val = INTPLL_MAIN_DIV_VAL(0xc8) |
			INTPLL_PRE_DIV_VAL(2) | INTPLL_POST_DIV_VAL(1);
		break;
	case INTPLL_OUT_2000M:
		/* 24 * 0xfa / 3 / 2 ^ 0 */
		div_ctl_val = INTPLL_MAIN_DIV_VAL(0xfa) |
			INTPLL_PRE_DIV_VAL(3) | INTPLL_POST_DIV_VAL(0);
		break;
	default:
		return -EINVAL;
	}
	/* Bypass clock and set lock to pll output lock */
	setbits_le32(&pll->gnrl_ctl, INTPLL_BYPASS_MASK | INTPLL_LOCK_SEL_MASK);
	/* Enable reset */
	clrbits_le32(&pll->gnrl_ctl, INTPLL_RST_MASK);
	/* Configure */
	writel(div_ctl_val, &pll->div_ctl);

	__udelay(100);

	/* Disable reset */
	setbits_le32(&pll->gnrl_ctl, INTPLL_RST_MASK);
	/* Wait Lock */
	ret = readl_poll_timeout(&pll->gnrl_ctl, val, val & INTPLL_LOCK_MASK, 100);
	if (ret)
		printf("%s timeout\n", __func__);

	/* Clear bypass */
	clrbits_le32(&pll->gnrl_ctl, INTPLL_BYPASS_MASK);
	setbits_le32(&pll->gnrl_ctl, pll_clke_masks);

	return ret;
}

void enable_display_clk(unsigned char enable)
{
	if (enable) {
		clock_enable(CCGR_DISPMIX, false);

		/* Set Video PLL to 594Mhz, p = 1, m = 99,  k = 0, s = 2 */
		fracpll_configure(ANATOP_VIDEO_PLL, VIDEO_PLL_RATE);

		/* 500Mhz */
		clock_set_target_val(DISPLAY_AXI_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1) | CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV2));

		/* 200Mhz */
		clock_set_target_val(DISPLAY_APB_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(2) |CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV4));

		clock_set_target_val(MIPI_DSI_CORE_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));

#ifdef CONFIG_IMX8MN
		/* 27Mhz MIPI DPHY PLL ref from video PLL */
		clock_set_target_val(DISPLAY_DSI_PHY_REF_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(7) |CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV22));
#else
		clock_set_target_val(MIPI_DSI_PHY_REF_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(7) |CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV22));
#endif

		clock_enable(CCGR_DISPMIX, true);
	} else {
		clock_enable(CCGR_DISPMIX, false);
	}
}

void init_uart_clk(u32 index)
{
	/*
	 * set uart clock root
	 * 24M OSC
	 */
	switch (index) {
	case 0:
		clock_enable(CCGR_UART1, 0);
		clock_set_target_val(UART1_CLK_ROOT, CLK_ROOT_ON |
				     CLK_ROOT_SOURCE_SEL(0));
		clock_enable(CCGR_UART1, 1);
		return;
	case 1:
		clock_enable(CCGR_UART2, 0);
		clock_set_target_val(UART2_CLK_ROOT, CLK_ROOT_ON |
				     CLK_ROOT_SOURCE_SEL(0));
		clock_enable(CCGR_UART2, 1);
		return;
	case 2:
		clock_enable(CCGR_UART3, 0);
		clock_set_target_val(UART3_CLK_ROOT, CLK_ROOT_ON |
				     CLK_ROOT_SOURCE_SEL(0));
		clock_enable(CCGR_UART3, 1);
		return;
	case 3:
		clock_enable(CCGR_UART4, 0);
		clock_set_target_val(UART4_CLK_ROOT, CLK_ROOT_ON |
				     CLK_ROOT_SOURCE_SEL(0));
		clock_enable(CCGR_UART4, 1);
		return;
	default:
		printf("Invalid uart index\n");
		return;
	}
}

int clock_init(void)
{
	uint32_t val_cfg0;

	/* Configure ARM at 1GHz */
	clock_set_target_val(ARM_A53_CLK_ROOT, CLK_ROOT_ON |
			     CLK_ROOT_SOURCE_SEL(2));

	intpll_configure(ANATOP_ARM_PLL, INTPLL_OUT_1200M);

	clock_set_target_val(ARM_A53_CLK_ROOT, CLK_ROOT_ON |
			     CLK_ROOT_SOURCE_SEL(1) |
			     CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV1));
	/*
	 * According to ANAMIX SPEC
	 * sys pll1 fixed at 800MHz
	 * sys pll2 fixed at 1GHz
	 * Here we only enable the outputs.
	 */
	val_cfg0 = readl(&ana_pll->sys_pll1.gnrl_ctl);
	val_cfg0 |= INTPLL_CLKE_MASK | INTPLL_DIV2_CLKE_MASK |
		INTPLL_DIV3_CLKE_MASK | INTPLL_DIV4_CLKE_MASK |
		INTPLL_DIV5_CLKE_MASK | INTPLL_DIV6_CLKE_MASK |
		INTPLL_DIV8_CLKE_MASK | INTPLL_DIV10_CLKE_MASK |
		INTPLL_DIV20_CLKE_MASK;
	writel(val_cfg0, &ana_pll->sys_pll1.gnrl_ctl);

	val_cfg0 = readl(&ana_pll->sys_pll2.gnrl_ctl);
	val_cfg0 |= INTPLL_CLKE_MASK | INTPLL_DIV2_CLKE_MASK |
		INTPLL_DIV3_CLKE_MASK | INTPLL_DIV4_CLKE_MASK |
		INTPLL_DIV5_CLKE_MASK | INTPLL_DIV6_CLKE_MASK |
		INTPLL_DIV8_CLKE_MASK | INTPLL_DIV10_CLKE_MASK |
		INTPLL_DIV20_CLKE_MASK;
	writel(val_cfg0, &ana_pll->sys_pll2.gnrl_ctl);

	if (is_imx8mn())
		intpll_configure(ANATOP_SYSTEM_PLL3, INTPLL_OUT_600M);
	else
		intpll_configure(ANATOP_SYSTEM_PLL3, INTPLL_OUT_750M);
	clock_set_target_val(NOC_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(2));

	/* config GIC to sys_pll2_100m */
	clock_enable(CCGR_GIC, 0);
	clock_set_target_val(GIC_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(3));
	clock_enable(CCGR_GIC, 1);

	/*
	 * set uart clock root
	 * 24M OSC
	 */
	clock_enable(CCGR_UART1, 0);
	clock_enable(CCGR_UART2, 0);
	clock_enable(CCGR_UART3, 0);
	clock_enable(CCGR_UART4, 0);
	clock_set_target_val(UART1_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_set_target_val(UART2_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_set_target_val(UART3_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_set_target_val(UART4_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_enable(CCGR_UART1, 1);
	clock_enable(CCGR_UART2, 1);
	clock_enable(CCGR_UART3, 1);
	clock_enable(CCGR_UART4, 1);

	/*
	 * set usdhc clock root
	 * sys pll1 400M
	 */
	clock_enable(CCGR_USDHC1, 0);
	clock_enable(CCGR_USDHC2, 0);
	clock_enable(CCGR_USDHC3, 0);
	clock_set_target_val(NAND_USDHC_BUS_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
	clock_set_target_val(USDHC1_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1) | CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV2));
	clock_set_target_val(USDHC2_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1) | CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV2));
	clock_set_target_val(USDHC3_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1) | CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV2));
	clock_enable(CCGR_USDHC1, 1);
	clock_enable(CCGR_USDHC2, 1);
	clock_enable(CCGR_USDHC3, 1);

	clock_enable(CCGR_DDR1, 0);
	clock_set_target_val(DRAM_ALT_CLK_ROOT,CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
	clock_set_target_val(DRAM_APB_CLK_ROOT,CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
	clock_enable(CCGR_DDR1, 1);

	/*
	 * set qspi root
	 * sys pll1 100M
	 */
	clock_enable(CCGR_QSPI, 0);
	clock_set_target_val(QSPI_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_enable(CCGR_QSPI, 1);

	/*
	 * set rawnand root
	 * sys pll1 400M
	 */
	clock_enable(CCGR_RAWNAND, 0);
	clock_set_target_val(NAND_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(3) | CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV4)); /* 100M */
	clock_enable(CCGR_RAWNAND, 1);

	clock_enable(CCGR_WDOG1, 0);
	clock_enable(CCGR_WDOG2, 0);
	clock_enable(CCGR_WDOG3, 0);
	clock_set_target_val(WDOG_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_enable(CCGR_WDOG1, 1);
	clock_enable(CCGR_WDOG2, 1);
	clock_enable(CCGR_WDOG3, 1);

	clock_enable(CCGR_TEMP_SENSOR, 1);

	clock_enable(CCGR_ECSPI1, 0);
	clock_enable(CCGR_ECSPI2, 0);
	clock_enable(CCGR_ECSPI3, 0);
	clock_set_target_val(ECSPI1_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_set_target_val(ECSPI2_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_set_target_val(ECSPI3_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(0));
	clock_enable(CCGR_ECSPI1, 1);
	clock_enable(CCGR_ECSPI2, 1);
	clock_enable(CCGR_ECSPI3, 1);

	clock_enable(CCGR_SEC_DEBUG, 1);

	enable_display_clk(1);
	return 0;
}
#endif

void mxs_set_lcdclk(uint32_t base_addr, uint32_t freq)
{
	uint32_t div, pre, post;

	div = VIDEO_PLL_RATE / 1000;
	div = (div + freq - 1) / freq;

	if (div < 1)
		div = 1;

	for (pre = 1; pre <= 8; pre++) {
		for (post = 1; post <= 64; post++) {
			if (pre * post == div) {
				goto find;
			}
		}
	}

	printf("Fail to set rate to %dkhz", freq);
	return;

find:
	/* Select to video PLL */
	debug("mxs_set_lcdclk, pre = %d, post = %d\n", pre, post);
#ifdef CONFIG_IMX8MN
	clock_set_target_val(DISPLAY_PIXEL_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1) | CLK_ROOT_PRE_DIV(pre - 1) | CLK_ROOT_POST_DIV(post - 1));
#else
	clock_set_target_val(LCDIF_PIXEL_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1) | CLK_ROOT_PRE_DIV(pre - 1) | CLK_ROOT_POST_DIV(post - 1));
#endif

}

static enum clk_ccgr_index ccgr_usdhc[] = {CCGR_USDHC1, CCGR_USDHC2, CCGR_USDHC3,};
static enum clk_root_index root_usdhc[] = {USDHC1_CLK_ROOT, USDHC2_CLK_ROOT, USDHC3_CLK_ROOT,};
void init_clk_usdhc(u32 index)
{
	if (index > ARRAY_SIZE(ccgr_usdhc)) {
		printf("Invalid usdhc index\n");
		return;
	}
	/*
	 * set usdhc clock root
	 * sys pll1 400M
	 */
	clock_enable(ccgr_usdhc[index], 0);
	clock_set_target_val(root_usdhc[index], CLK_ROOT_ON |
				     CLK_ROOT_SOURCE_SEL(1) |
				     CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV2));
	clock_enable(ccgr_usdhc[index], 1);
}

int set_clk_qspi(void)
{
	clock_enable(CCGR_QSPI, 0);
	/*
	 * TODO: configure clock
	 */
	clock_enable(CCGR_QSPI, 1);

	return 0;
}

#ifdef CONFIG_FEC_MXC
int set_clk_enet(enum enet_freq type)
{
	u32 target;
	u32 enet1_ref;

	/* disable the clock first */
	clock_enable(CCGR_ENET1, 0);
	clock_enable(CCGR_SIM_ENET, 0);

	switch (type) {
	case ENET_125MHZ:
		enet1_ref = ENET1_REF_CLK_ROOT_FROM_PLL_ENET_MAIN_125M_CLK;
		break;
	case ENET_50MHZ:
		enet1_ref = ENET1_REF_CLK_ROOT_FROM_PLL_ENET_MAIN_50M_CLK;
		break;
	case ENET_25MHZ:
		enet1_ref = ENET1_REF_CLK_ROOT_FROM_PLL_ENET_MAIN_25M_CLK;
		break;
	default:
		return -EINVAL;
	}

	/* set enet axi clock 266Mhz */
	target = CLK_ROOT_ON | ENET_AXI_CLK_ROOT_FROM_SYS1_PLL_266M |
		 CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV1) |
		 CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV1);
	clock_set_target_val(ENET_AXI_CLK_ROOT, target);

	target = CLK_ROOT_ON | enet1_ref |
		 CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV1) |
		 CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV1);
	clock_set_target_val(ENET_REF_CLK_ROOT, target);

	target = CLK_ROOT_ON | ENET1_TIME_CLK_ROOT_FROM_PLL_ENET_MAIN_100M_CLK |
		 CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV1) |
		 CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV4);
	clock_set_target_val(ENET_TIMER_CLK_ROOT, target);

#ifdef CONFIG_FEC_MXC_25M_REF_CLK
	target = CLK_ROOT_ON |
		 ENET_PHY_REF_CLK_ROOT_FROM_PLL_ENET_MAIN_25M_CLK |
		 CLK_ROOT_PRE_DIV(CLK_ROOT_PRE_DIV1) |
		 CLK_ROOT_POST_DIV(CLK_ROOT_POST_DIV1);
	clock_set_target_val(ENET_PHY_REF_CLK_ROOT, target);
#endif
	/* enable clock */
	clock_enable(CCGR_SIM_ENET, 1);
	clock_enable(CCGR_ENET1, 1);

	return 0;
}
#endif

u32 get_root_src_clk(enum clk_root_src root_src)
{
	switch (root_src) {
	case OSC_24M_CLK:
		return 24000000u;
	case OSC_HDMI_CLK:
		return 26000000u;
	case OSC_32K_CLK:
		return 32000u;
	case ARM_PLL_CLK:
	case GPU_PLL_CLK:
	case VPU_PLL_CLK:
	case SYSTEM_PLL1_800M_CLK:
	case SYSTEM_PLL1_400M_CLK:
	case SYSTEM_PLL1_266M_CLK:
	case SYSTEM_PLL1_200M_CLK:
	case SYSTEM_PLL1_160M_CLK:
	case SYSTEM_PLL1_133M_CLK:
	case SYSTEM_PLL1_100M_CLK:
	case SYSTEM_PLL1_80M_CLK:
	case SYSTEM_PLL1_40M_CLK:
	case SYSTEM_PLL2_1000M_CLK:
	case SYSTEM_PLL2_500M_CLK:
	case SYSTEM_PLL2_333M_CLK:
	case SYSTEM_PLL2_250M_CLK:
	case SYSTEM_PLL2_200M_CLK:
	case SYSTEM_PLL2_166M_CLK:
	case SYSTEM_PLL2_125M_CLK:
	case SYSTEM_PLL2_100M_CLK:
	case SYSTEM_PLL2_50M_CLK:
	case SYSTEM_PLL3_CLK:
		return decode_intpll(root_src);
	case DRAM_PLL1_CLK:
	case AUDIO_PLL1_CLK:
	case AUDIO_PLL2_CLK:
	case VIDEO_PLL_CLK:
		return decode_fracpll(root_src);
	default:
		return 0;
	}

	return 0;
}

u32 get_root_clk(enum clk_root_index clock_id)
{
	enum clk_root_src root_src;
	u32 post_podf, pre_podf, root_src_clk;

	if (clock_root_enabled(clock_id) <= 0)
		return 0;

	if (clock_get_prediv(clock_id, &pre_podf) < 0)
		return 0;

	if (clock_get_postdiv(clock_id, &post_podf) < 0)
		return 0;

	if (clock_get_src(clock_id, &root_src) < 0)
		return 0;

	root_src_clk = get_root_src_clk(root_src);

	return root_src_clk / (post_podf + 1) / (pre_podf + 1);
}

u32 mxc_get_clock(enum mxc_clock clk)
{
	u32 val;

	switch (clk) {
		case MXC_ARM_CLK:
			return get_root_clk(ARM_A53_CLK_ROOT);
		case MXC_IPG_CLK:
			clock_get_target_val(IPG_CLK_ROOT, &val);
			val = val & 0x3;
			return get_root_clk(AHB_CLK_ROOT) / 2 / (val + 1);
		case MXC_CSPI_CLK:
			return get_root_clk(ECSPI1_CLK_ROOT);
		case MXC_ESDHC_CLK:
			return get_root_clk(USDHC1_CLK_ROOT);
		case MXC_ESDHC2_CLK:
			return get_root_clk(USDHC2_CLK_ROOT);
		case MXC_ESDHC3_CLK:
			return get_root_clk(USDHC3_CLK_ROOT);
		case MXC_I2C_CLK:
			return get_root_clk(I2C1_CLK_ROOT);
		case MXC_UART_CLK:
			return get_root_clk(UART1_CLK_ROOT);
		case MXC_QSPI_CLK:
			return get_root_clk(QSPI_CLK_ROOT);
		default:
			printf("Unsupported mxc_clock %d\n", clk);
			break;
	}

	return 0;
}

u32 imx_get_uartclk(void)
{
	return mxc_get_clock(MXC_UART_CLK);
}

u32 imx_get_fecclk(void)
{
	return get_root_clk(ENET_AXI_CLK_ROOT);
}

void enable_usboh3_clk(unsigned char enable)
{
	if (enable) {
		clock_enable(CCGR_USB_MSCALE_PL301, 0);
		/* 500M */
		clock_set_target_val(USB_BUS_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
		/* 100M */
		clock_set_target_val(USB_CORE_REF_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
		/* 100M */
		clock_set_target_val(USB_PHY_REF_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(1));
		clock_enable(CCGR_USB_MSCALE_PL301, 1);
	} else {
		clock_enable(CCGR_USB_MSCALE_PL301, 0);
	}
}

/*
 * Dump some clockes.
 */
#ifndef CONFIG_SPL_BUILD
static int do_imx8mm_showclocks(cmd_tbl_t *cmdtp, int flag, int argc,
				char * const argv[])
{
	u32 freq;

	freq = decode_intpll(ARM_PLL_CLK);
	printf("ARM_PLL    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_800M_CLK);
	printf("SYS_PLL1_800    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_400M_CLK);
	printf("SYS_PLL1_400    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_266M_CLK);
	printf("SYS_PLL1_266    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_160M_CLK);
	printf("SYS_PLL1_160    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_133M_CLK);
	printf("SYS_PLL1_133    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_100M_CLK);
	printf("SYS_PLL1_100    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_80M_CLK);
	printf("SYS_PLL1_80    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL1_40M_CLK);
	printf("SYS_PLL1_40    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_1000M_CLK);
	printf("SYS_PLL2_1000    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_500M_CLK);
	printf("SYS_PLL2_500    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_333M_CLK);
	printf("SYS_PLL2_333    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_250M_CLK);
	printf("SYS_PLL2_250    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_200M_CLK);
	printf("SYS_PLL2_200    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_166M_CLK);
	printf("SYS_PLL2_166    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_125M_CLK);
	printf("SYS_PLL2_125    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_100M_CLK);
	printf("SYS_PLL2_100    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL2_50M_CLK);
	printf("SYS_PLL2_50    %8d MHz\n", freq / 1000000);
	freq = decode_intpll(SYSTEM_PLL3_CLK);
	printf("SYS_PLL3       %8d MHz\n", freq / 1000000);
	freq = mxc_get_clock(MXC_UART_CLK);
	printf("UART1          %8d MHz\n", freq / 1000000);
	freq = mxc_get_clock(MXC_ESDHC_CLK);
	printf("USDHC1         %8d MHz\n", freq / 1000000);
	freq = mxc_get_clock(MXC_QSPI_CLK);
	printf("QSPI           %8d MHz\n", freq / 1000000);

	return 0;
}

U_BOOT_CMD(
	clocks,	CONFIG_SYS_MAXARGS, 1, do_imx8mm_showclocks,
	"display clocks",
	""
);
#endif
