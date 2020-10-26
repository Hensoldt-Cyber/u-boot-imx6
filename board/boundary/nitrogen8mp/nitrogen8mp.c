// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 NXP
 * Copyright 2020 Boundary Devices
 */

#include <common.h>
#include <command.h>
#include <errno.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/io.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <asm/arch/imx8mp_pins.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/fbpanel.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <asm/mach-imx/dma.h>
#include <asm/arch/clock.h>
#include <linux/delay.h>
#include <mmc.h>
#include <spl.h>
#include <power/pmic.h>
#include <usb.h>
#include <dwc3-uboot.h>
#include "../common/padctrl.h"
#include "../common/bd_common.h"

DECLARE_GLOBAL_DATA_PTR;

#define UART_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL1)
#define WDOG_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_ODE | PAD_CTL_PUE | PAD_CTL_PE)

static iomux_v3_cfg_t const init_pads[] = {
	IOMUX_PAD_CTRL(GPIO1_IO02__WDOG1_WDOG_B, WDOG_PAD_CTRL),
	IOMUX_PAD_CTRL(UART2_RXD__UART2_DCE_RX, UART_PAD_CTRL),
	IOMUX_PAD_CTRL(UART2_TXD__UART2_DCE_TX, UART_PAD_CTRL),
	IOMUX_PAD_CTRL(SAI1_RXD3__ENET1_MDIO, PAD_CTRL_ENET_MDIO),
	IOMUX_PAD_CTRL(SAI1_RXD2__ENET1_MDC, PAD_CTRL_ENET_MDC),

#define GPIRQ_SN65DSI83		IMX_GPIO_NR(4, 0)
#define GP_LCM_JM430_BKL_EN	IMX_GPIO_NR(4, 0)
#define GP_LTK08_MIPI_EN	IMX_GPIO_NR(4, 0)
	IOMUX_PAD_CTRL(SAI1_RXFS__GPIO4_IO00, 0x04),

#define GPIRQ_TS_GT911		IMX_GPIO_NR(4, 25)
#define GP_TS_GT911_IRQ		IMX_GPIO_NR(4, 25)
#define GPIRQ_TS_FT5X06		IMX_GPIO_NR(4, 25)
#define GP_TS_FT5X06_WAKE	IMX_GPIO_NR(4, 25)
	IOMUX_PAD_CTRL(SAI2_TXC__GPIO4_IO25, 0x106),

#define GP_TS_GT911_RESET	IMX_GPIO_NR(4, 24)
#define GP_ST1633_RESET		IMX_GPIO_NR(4, 24)
#define GP_TS_FT5X06_RESET	IMX_GPIO_NR(4, 24)
	IOMUX_PAD_CTRL(SAI2_TXFS__GPIO4_IO24, 0x109),

#define GP_TC358762_EN		IMX_GPIO_NR(4, 27)
#define GP_SC18IS602B_RESET	IMX_GPIO_NR(4, 27)
#define GP_SN65DSI83_EN		IMX_GPIO_NR(4, 27)
#define GP_MIPI_RESET		IMX_GPIO_NR(4, 27)
	/* enable for TPS65132 Single Inductor - Dual Output Power Supply */
#define GP_LCD133_070_ENABLE	IMX_GPIO_NR(4, 27)
	IOMUX_PAD_CTRL(SAI2_MCLK__GPIO4_IO27, 0x106),
};

int board_early_init_f(void)
{
	struct wdog_regs *wdog = (struct wdog_regs *)WDOG1_BASE_ADDR;

	gpio_request(GP_SN65DSI83_EN, "sn65en");
	gpio_direction_output(GP_SN65DSI83_EN, 0);

	imx_iomux_v3_setup_multiple_pads(init_pads, ARRAY_SIZE(init_pads));
	set_wdog_reset(wdog);
	init_uart_clk(1);

	return 0;
}

#if defined(CONFIG_USB_DWC3) || defined(CONFIG_USB_XHCI_IMX8M)
int board_usb_hub_gpio_init(void)
{
#define GP_USB1_HUB_RESET	IMX_GPIO_NR(1, 6)
	imx_iomux_v3_setup_pad(MX8MP_PAD_GPIO1_IO06__GPIO1_IO06 |
			MUX_PAD_CTRL(WEAK_PULLUP));
	return GP_USB1_HUB_RESET;
}
#endif

#ifdef CONFIG_CMD_FBPANEL
int board_detect_gt911(struct display_info_t const *di)
{
	int ret;
	struct udevice *dev, *chip;

	if (di->bus_gp)
		gpio_direction_output(di->bus_gp, 1);
	gpio_set_value(GP_TS_GT911_RESET, 0);
	mdelay(20);
	gpio_direction_output(GPIRQ_TS_GT911, di->addr_num == 0x14 ? 1 : 0);
	udelay(100);
	gpio_set_value(GP_TS_GT911_RESET, 1);
	mdelay(6);
	gpio_set_value(GPIRQ_TS_GT911, 0);
	mdelay(50);
	gpio_direction_input(GPIRQ_TS_GT911);
	ret = uclass_get_device(UCLASS_I2C, di->bus_num, &dev);
	if (ret)
		return 0;

	ret = dm_i2c_probe(dev, di->addr_num, 0x0, &chip);
	if (ret && di->bus_gp)
		gpio_direction_input(di->bus_gp);
	return (ret == 0);
}

static const struct display_info_t displays[] = {
	VD_MIPI_M101NWWB(MIPI, fbp_detect_i2c, fbp_bus_gp((1 | (3 << 4)), GP_SN65DSI83_EN, 0, 0), 0x2c, FBP_MIPI_TO_LVDS, FBTS_FT5X06),
	VD_MIPI_MTD0900DCP27KF(MIPI, fbp_detect_i2c, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x41, FBP_MIPI_TO_LVDS, FBTS_ILI251X),
	VD_DMT050WVNXCMI(MIPI, fbp_detect_i2c, fbp_bus_gp((1 | (3 << 4)), GP_SC18IS602B_RESET, 0, 30), fbp_addr_gp(0x2f, 0, 6, 0), FBP_SPI_LCD, FBTS_GOODIX),
	VD_LTK080A60A004T(MIPI, board_detect_gt911, fbp_bus_gp((1 | (3 << 4)), GP_LTK08_MIPI_EN, GP_LTK08_MIPI_EN, 0), 0x5d, FBTS_GOODIX),	/* Goodix touchscreen */
	VD_LCM_JM430_MINI(MIPI, fbp_detect_i2c, fbp_bus_gp((1 | (3 << 4)), GP_ST1633_RESET, GP_TC358762_EN, 30), fbp_addr_gp(0x55, GP_LCM_JM430_BKL_EN, 0, 0), FBTS_ST1633I),	/* Sitronix touch */
	VD_LTK0680YTMDB(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), GP_MIPI_RESET, GP_MIPI_RESET, 0), 0x5d, FBTS_GOODIX),
	VD_MIPI_COM50H5N03ULC(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), GP_MIPI_RESET, GP_MIPI_RESET, 0), 0x00),
	/* 0x3e is the TPS65132 power chip on our adapter board */
	VD_MIPI_LCD133_070(MIPI, board_detect_lcd133, fbp_bus_gp((1 | (3 << 4)), GP_LCD133_070_ENABLE, GP_LCD133_070_ENABLE, 1), fbp_addr_gp(0x3e, 0, 0, 0), FBTS_FT7250),
	VD_MIPI_640_480M_60(MIPI, board_detect_pca9546, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_1280_800M_60(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_1280_720M_60(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_1920_1080M_60(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_1024_768M_60(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_800_600MR_60(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_720_480M_60(MIPI, NULL, fbp_bus_gp((1 | (3 << 4)), 0, 0, 0), 0x68, FBP_PCA9546),
	VD_MIPI_VTFT101RPFT20(MIPI, fbp_detect_i2c, 1, 0x70, FBP_PCA9540),
};
#define display_cnt	ARRAY_SIZE(displays)
#else
#define displays	NULL
#define display_cnt	0
#endif

int board_init(void)
{
#ifdef CONFIG_CMD_FBPANEL
	fbp_setup_display(displays, display_cnt);
#endif
	return 0;
}

/* This should be defined for each board */
__weak int mmc_map_to_kernel_blk(int dev_no)
{
	return dev_no;
}

#ifdef CONFIG_FSL_FASTBOOT
#ifdef CONFIG_ANDROID_RECOVERY
int is_recovery_key_pressing(void)
{
	return 0; /*TODO*/
}
#endif /*CONFIG_ANDROID_RECOVERY*/
#endif /*CONFIG_FSL_FASTBOOT*/

#ifdef CONFIG_ANDROID_SUPPORT
bool is_power_key_pressed(void) {
	return (bool)(!!(readl(SNVS_HPSR) & (0x1 << 6)));
}
#endif

#ifdef CONFIG_SPL_MMC_SUPPORT

#define UBOOT_RAW_SECTOR_OFFSET 0x40
unsigned long spl_mmc_get_uboot_raw_sector(struct mmc *mmc)
{
	u32 boot_dev = spl_boot_device();
	switch (boot_dev) {
		case BOOT_DEVICE_MMC2:
			return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR - UBOOT_RAW_SECTOR_OFFSET;
		default:
			return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR;
	}
}
#endif
