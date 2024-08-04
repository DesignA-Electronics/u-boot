// SPDX-License-Identifier: GPL-2.0+
/*
 * Micrel KSZ9896 PHY driver
 *
 * Copyright 2024 DesignA Electronics
 * author Andre Renaud
 */
#include <common.h>
#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <phy.h>
#include <i2c.h>

// MMD access
#define KSZ9896_PHY_MMD_SETUP 0x11a
#define KSZ9896_PHY_MMD_DATA 0x11c

//PHY MMD Setup register
#define KSZ9896_MMDACR_FUNC                                    0xC000
#define KSZ9896_MMDACR_FUNC_ADDR                               0x0000
#define KSZ9896_MMDACR_FUNC_DATA_NO_POST_INC                   0x4000
#define KSZ9896_MMDACR_FUNC_DATA_POST_INC_RW                   0x8000
#define KSZ9896_MMDACR_FUNC_DATA_POST_INC_W                    0xC000
#define KSZ9896_MMDACR_DEVAD                                   0x001F

// PHY status register
#define KSZ9896_PHY_BASIC_STATUS								0x0102
#define KSZ9896_PHY_LINK_PARTNER_STATUS							0x010A
#define KSZ9896_PHY_1000BT_STATUS								0x0114

//MMD LED Mode register
#define KSZ9896_MMD_LED_MODE_LED_MODE                          0x0010
#define KSZ9896_MMD_LED_MODE_LED_MODE_TRI_COLOR_DUAL           0x0000
#define KSZ9896_MMD_LED_MODE_LED_MODE_SINGLE                   0x0010
#define KSZ9896_MMD_LED_MODE_RESERVED                          0x000F
#define KSZ9896_MMD_LED_MODE_RESERVED_DEFAULT                  0x0001
  
//KSZ9896 MMD registers
#define KSZ9896_MMD_LED_MODE                                   0x02, 0x00
#define KSZ9896_MMD_EEE_ADV                                    0x07, 0x3C

static int ksz9896_write16(struct udevice *dev, uint8_t port, uint16_t reg, uint16_t val)
{
	uint8_t data[2] = {val >> 8, val};
	if (dm_i2c_write(dev, (port << 12) | reg, data, 2)) {
		printf("cannot write 0x%x=0x%04x\n", reg, val);
		return -1;
	}
	return 0;
}

static int ksz9896_read16(struct udevice *dev, uint8_t port, uint16_t reg)
{
	uint16_t data;
	if (dm_i2c_read(dev, (port << 12) | reg, (uint8_t *)&data, 2)) {
		return -1;
	}
	return __be16_to_cpu(data);
}

static int ksz9896_write32(struct udevice *dev, uint8_t port, uint16_t reg, uint32_t val)
{
	uint8_t data[4] = {val >> 24, val >> 16, val >> 8, val};
	if (dm_i2c_write(dev, (port << 12) | reg, data, sizeof(data))) {
		printf("cannot write 0x%x=0x%04x\n", reg, val);
		return -1;
	}
	return 0;
}

static uint32_t ksz9896_read32(struct udevice *dev, uint8_t port, uint16_t reg)
{
	uint32_t val;
	if (dm_i2c_read(dev, (port << 12) | reg, (uint8_t *)&val, sizeof(val))) {
		return -1;
	}
    return __be32_to_cpu(val);
}

static int ksz9896_write_mmd(struct udevice *dev, uint8_t port, uint8_t dev_addr, uint16_t reg_addr, uint16_t data)
{
    //Select register operation
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_SETUP,
       KSZ9896_MMDACR_FUNC_ADDR | (dev_addr & KSZ9896_MMDACR_DEVAD));

    //Write MMD register address
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_DATA, reg_addr);

    //Select data operation
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_SETUP,
       KSZ9896_MMDACR_FUNC_DATA_NO_POST_INC | (dev_addr & KSZ9896_MMDACR_DEVAD));

    //Write the content of the MMD register
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_DATA, data);

	return 0;
}

#if 0
static int ksz9896_read_mmd(struct udevice *dev, uint8_t port, uint8_t dev_addr, uint16_t reg_addr)
{
    //Select register operation
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_SETUP,
       KSZ9896_MMDACR_FUNC_ADDR | (dev_addr & KSZ9896_MMDACR_DEVAD));

    //Write MMD register address
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_DATA, reg_addr);

    //Select data operation
    ksz9896_write16(dev, port, KSZ9896_PHY_MMD_SETUP,
       KSZ9896_MMDACR_FUNC_DATA_NO_POST_INC | (dev_addr & KSZ9896_MMDACR_DEVAD));

    // Read the content of the MMD register
    return ksz9896_read16(dev, port, KSZ9896_PHY_MMD_DATA);
}
#endif

static uint32_t ksz9896_read_mib(struct udevice *dev, uint8_t port, uint8_t mib_index)
{
	uint8_t ctrl[4] = {0x2, mib_index, 0, 0};
	if (dm_i2c_write(dev, 0x500 | (port << 12), ctrl, 4)) {
		printf("cannot write %d 0x%x\n", port, mib_index);
		return -1;
	}
	udelay(1);
	return ksz9896_read32(dev, port, 0x504);
}

static int ksz9896_probe(struct udevice *dev)
{
    uint32_t id;
	const char *str;
	uint16_t led_mode = KSZ9896_MMD_LED_MODE_LED_MODE_TRI_COLOR_DUAL | KSZ9896_MMD_LED_MODE_RESERVED_DEFAULT;
	uint32_t raw_mode;

	id = ksz9896_read32(dev, 0, 0);
	if (id != 0x00989600) {
		dev_err(dev, "invalid id: %08x", id);
		return 1;
	}

    // TODO: This should be based on whether rx-internal-delay-ps & tx-internal-delay-ps is set for the cpu port
	// Enable RGMII Ingress Internal Delay (RGMII_ID_ig)
	str = ofnode_read_string(dev->node, "phy-mode");
	if (str) {
    	uint8_t xmii_ctrl_1;
		int phy_mode = phy_get_interface_by_name(str);
		if (phy_mode == PHY_INTERFACE_MODE_RGMII_ID) {
    		xmii_ctrl_1 = 0x18;
			dm_i2c_write(dev, 0x6301, &xmii_ctrl_1, 1);
		} else {
			dev_err(dev, "unsupported phy mode: %s", str);
		}
	}
	if (!ofnode_read_u32(dev->node, "micrel,led-mode", &raw_mode)) {
		if (raw_mode == 1)
			led_mode = KSZ9896_MMD_LED_MODE_LED_MODE_SINGLE | KSZ9896_MMD_LED_MODE_RESERVED_DEFAULT;
		else {
			dev_err(dev, "unsupported led mode: %d", raw_mode);
		}
	}

	// Apply errata as per.
	// See 'KSZ9896C Silicon Errata and Data Sheet Clarification'
	// https://ww1.microchip.com/downloads/en/DeviceDoc/80000757C.pdf
	for (int port = 1; port <= 5; port++) {
		// Improve PHY receive performance (silicon errata workaround 1)
		ksz9896_write_mmd(dev, port, 0x01, 0x6F, 0xDD0B);
		ksz9896_write_mmd(dev, port, 0x01, 0x8F, 0x6032);
		ksz9896_write_mmd(dev, port, 0x01, 0x9D, 0x248C);
		ksz9896_write_mmd(dev, port, 0x01, 0x75, 0x0060);
		ksz9896_write_mmd(dev, port, 0x01, 0xD3, 0x7777);
		ksz9896_write_mmd(dev, port, 0x1C, 0x06, 0x3008);
		ksz9896_write_mmd(dev, port, 0x1C, 0x08, 0x2001);

		// Improve transmit waveform amplitude (silicon errata workaround 2)
		ksz9896_write_mmd(dev, port, 0x1C, 0x04, 0x00D0);

		// EEE must be manually disabled (silicon errata workaround 3)
		ksz9896_write_mmd(dev, port, KSZ9896_MMD_EEE_ADV, 0);

		// Adjust power supply settings (silicon errata workaround 6)
		ksz9896_write_mmd(dev, port, 0x1C, 0x13, 0x6EFF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x14, 0xE6FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x15, 0x6EFF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x16, 0xE6FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x17, 0x00FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x18, 0x43FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x19, 0xC3FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x1A, 0x6FFF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x1B, 0x07FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x1C, 0x0FFF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x1D, 0xE7FF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x1E, 0xEFFF);
		ksz9896_write_mmd(dev, port, 0x1C, 0x20, 0xEEEE);


		// Select single-color mode (silicon errata workaround 14)
		// https://microchipsupport.force.com/s/article/Single-LED-mode-in-the-KSZ9897-and-KSZ9893-Ethernet-switch-families
		// https://ww1.microchip.com/downloads/aemDocuments/documents/UNG/ProductDocuments/Errata/KSZ9896C-Errata-DS80000757.pdf
		ksz9896_write_mmd(dev, port, KSZ9896_MMD_LED_MODE, led_mode);

		// This write must be 32-bit because of a separate errata issue
		ksz9896_write32(dev, port, 0x13C, 0xfa000300);
	}

	printf("KSz9896 probe complete\n");

	return 0;
}

static const struct udevice_id ksz9896_phy_ids[] = {
	{ .compatible = "micrel,ksz9896" },
	{ }
};

U_BOOT_DRIVER(phy_ksz9896) = {
	.name	= "phy-ksz9896",
	.id	= UCLASS_PHY,
	.probe	= ksz9896_probe,
	.of_match = ksz9896_phy_ids,
	.ops	= NULL, //&ksz9896_phy_ops,
};

static struct udevice *ksz9896_find(void)
{
	struct udevice *dev;
	uclass_foreach_dev_probe(UCLASS_PHY, dev) {
		if (dev->driver && strcmp(dev->driver->name, "phy-ksz9896") == 0)
			return dev;
	}
	return NULL;
}

static int do_ksz9896(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct udevice *dev = ksz9896_find();
	if (!dev) {
		printf("No KSZ9896 detected in I2C bus\n");
		return 1;
	}

	if (ksz9896_read32(dev, 0, 0) != 0x00989600) {
		dev_err(dev, "KSZ9698 chip present, but invalid id\n");
		return 1;
	}

	for (int port = 1; port <= 4; port++) {
		uint16_t status = ksz9896_read16(dev, port, KSZ9896_PHY_BASIC_STATUS);
		printf("%d:", port);

		if (status & (1 << 2)) {
			printf(" LINKUP");
			if (status & (1 << 5)) {
				printf(" AUTONEG");
			}
			status = ksz9896_read16(dev, port, KSZ9896_PHY_1000BT_STATUS);
			if (status & (1 << 11 | 1 << 10)) {
				printf(" 1000");
			} else {
				status = ksz9896_read16(dev, port, KSZ9896_PHY_LINK_PARTNER_STATUS);
				if (status & (1 << 9 | 1 << 8 | 1 << 7)) {
					printf(" 100");
				} else if (status & (1 << 6 | 1 << 5)) {
					printf(" 10");
				}
			}

			printf(" RX_UNICAST=%d", ksz9896_read_mib(dev, port, 0x0c));
			printf(" TX_UNICAST=%d", ksz9896_read_mib(dev, port, 0x1a));

			printf(" RX_BYTES=%d", ksz9896_read_mib(dev, port, 0x80));
			printf(" TX_BYTES=%d", ksz9896_read_mib(dev, port, 0x81));
		}

		printf("\n");
	}

	return 0;
}

U_BOOT_CMD(
	ksz9896,   3,   0,     do_ksz9896,
	"Display KSZ9896 PHY info",
	""
);
