// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 DesignA Electronics Ltd
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/mx6-pins.h>
#include <asm/sections.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <linux/errno.h>
#include <asm/gpio.h>
#include <asm/mach-imx/iomux-v3.h>
#include <fsl_esdhc_imx.h>
#include <miiphy.h>
#include <netdev.h>
#include <i2c.h>

#include "common.h"

DECLARE_GLOBAL_DATA_PTR;

void board_debug_uart_init(void)
{
	/* Done in SPL */
}

int dram_init(void)
{
	gd->ram_size = SZ_1G;

	return 0;
}

static iomux_v3_cfg_t const uart5_pads[] = {
	MX6_PAD_CSI0_DAT14__UART5_TX_DATA | MUX_PAD_CTRL(UART_PAD_CTRL),
	MX6_PAD_CSI0_DAT15__UART5_RX_DATA | MUX_PAD_CTRL(UART_PAD_CTRL),
	MX6_PAD_GPIO_9__GPIO1_IO09	  | MUX_PAD_CTRL(NO_PAD_CTRL),
};

static int salmon_version_read(void)
{
	int retval = 0;
	// We need to do this using a low-level read as this is used prior to the device
	// tree being initialised, so the dm_ functions will not work
	// We're on GPIO bank 2, pins 9 & 10

	uint32_t gpio_state = readl(GPIO2_BASE_ADDR);

	if (gpio_state & (1 << 10))
		retval |= 1;
	if (gpio_state & (1 << 9))
		retval |= 2;

	// Convert from the pull-up states to more useful version numbers
	// Mainboard rev2 uses in-CPU pulls up, so we get all 0x3
	// Mainbaord rev3 uses a pull-down on VERSION0, so we get 0x2
	// Future mainboards not yet defined
	if (retval == 0x3) {
		return 2;
	} else if (retval == 0x2) {
		return 3;
	} else {
		printf("WARNING: Unknown mainboard strapping state 0x%x\n", retval);
		return 0;
	}
}

#ifdef CONFIG_FEC_MXC
static int setup_fec_clock(void)
{
	struct iomuxc *iomuxc_regs = (struct iomuxc *)IOMUXC_BASE_ADDR;

	/* set gpr1[21] to select anatop clock */
	setbits_le32(&iomuxc_regs->gpr[1], IOMUXC_GPR1_ENET_CLK_SEL_MASK);
	iomuxc_set_rgmii_io_voltage(DDR_SEL_1P5V_IO);
	enable_fec_anatop_clock(0, ENET_125MHZ);
	enable_enet_clk(1);
	return 0;
}

static void salmon_enet_init(void)
{
	int reset_gpio = IMX_GPIO_NR(4, 8);
	int version = salmon_version_read();
	int reset_active = 0;

	// Mainboard rev2 & mainboard rev3 invert the ethernet reset
	if (version == 2) {
		reset_active = 0;
	} else if (version == 3) {
		setup_fec_clock();

		reset_active = 1;
	} else {
		puts("Invalid mainboard - not setting up ethernet\n");
		return;
	}

	/* Reset the ethernet PHY on the Salmon carrier board */
	gpio_request(reset_gpio, "PHY_RESET");
	gpio_direction_output(reset_gpio, reset_active);
	mdelay(5);
	gpio_set_value(reset_gpio, !reset_active);

	if (version == 3) {
		struct udevice *dev;
		mdelay(200); // Give the chip time to come out of reset
		if (uclass_get_device_by_name(UCLASS_PHY, "switch@5f", &dev))
			puts("cannot initialise phy\n");
	}
}
#endif

static void setup_iomux_uart(void)
{
	imx_iomux_v3_setup_multiple_pads(uart5_pads, ARRAY_SIZE(uart5_pads));
	gpio_request(IMX_GPIO_NR(1, 9), "rs232_enable");
	gpio_direction_output(IMX_GPIO_NR(1, 9), 1);
}

int board_early_init_f(void)
{
	setup_iomux_uart();

	return 0;
}

#ifdef CONFIG_OF_BOARD_FIXUP
int board_fix_fdt(void *fdt_blob)
{
	const char *eth0_path = "/soc/aips-bus@2100000/ethernet@2188000";
	const char *fixed_path = "/soc/aips-bus@2100000/ethernet@2188000/fixed-link";
	int version = salmon_version_read();
	int fixed_speed = 0;

	const char *pinctrl_group = NULL;;
	const char *phy_mode = NULL;
	if (version == 3) {
		pinctrl_group = "/soc/aips-bus@2000000/iomuxc@20e0000/enet_rev3grp";
		phy_mode = "rgmii";
		fixed_speed = 1000;
	} else if (version == 2) {
		pinctrl_group = "/soc/aips-bus@2000000/iomuxc@20e0000/enet_rev2grp";
		phy_mode = "mii";
		fixed_speed = 100;
	}
 	do_fixup_by_path_string(fdt_blob, eth0_path, "status", "okay");
	if (phy_mode) {
 		do_fixup_by_path_string(fdt_blob, eth0_path, "phy-mode", phy_mode);
	}
	if (pinctrl_group) {
		int group_off = fdt_path_offset(fdt_blob, pinctrl_group);
		uint32_t group_phandle = fdt_get_phandle(fdt_blob, group_off);
		do_fixup_by_path_u32(fdt_blob, eth0_path, "pinctrl-0", group_phandle, false);
	}
	if (fixed_speed) {
		do_fixup_by_path_u32(fdt_blob, fixed_path, "speed", fixed_speed, false);
	}

	return 0;
}
#endif

int power_init_board(void)
{
	set_ldo_voltage(LDO_ARM, 1250);	/* Set VDDARM to 1.25V */
	set_ldo_voltage(LDO_SOC, 1250);	/* Set VDDSOC to 1.25V */

	printf("LDO:   SOC=%dmV PU=%dmV ARM=%dmV\n",
		get_ldo_voltage(LDO_SOC),
		get_ldo_voltage(LDO_PU),
		get_ldo_voltage(LDO_ARM));

	salmon_enet_init();

	return 0;
}

int board_init(void)
{
	return 0;
}

int checkboard(void)
{
	puts("Board: SnapperMX6\n");
	printf("Mainboard version: %d\n", salmon_version_read());

	return 0;
}

int ft_board_setup(void *blob, bd_t *bd)
{
	printf("snapper mx6 ft_board_setup\n");
	// TODO: Adjust FDT based on which revision we are?
	return 0;
}
