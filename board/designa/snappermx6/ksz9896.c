// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2024 DesignA Electronics Ltd
 */

#include <common.h>
#include <linux/errno.h>
#include <i2c.h>

 #define KSZ9896_MMDACR                                         0x0D
 #define KSZ9896_MMDAADR                                        0x0E

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
  
 //MMD EEE Advertisement register
 #define KSZ9896_MMD_EEE_ADV_1000BT_EEE_EN                      0x0004
 #define KSZ9896_MMD_EEE_ADV_100BT_EEE_EN                       0x0002


 //KSZ9896 MMD registers
 #define KSZ9896_MMD_LED_MODE                                   0x02, 0x00
 #define KSZ9896_MMD_EEE_ADV                                    0x07, 0x3C

static int ksz9896_write16(struct udevice *dev, uint8_t port, uint16_t reg, uint16_t val)
{
	uint8_t data[2] = {val >> 8, val};
	if (dm_i2c_write(dev, port << 12 | reg, data, 2)) {
		printf("cannot write 0x%x=0x%04x\n", reg, val);
		return -1;
	}
	return 0;
}

static int ksz9896_read16(struct udevice *dev, uint8_t port, uint16_t reg)
{
	uint16_t data;
	if (dm_i2c_read(dev, port << 12 | reg, (uint8_t *)&data, 2)) {
		return -1;
	}
	return __be16_to_cpu(data);
}

static uint32_t ksz9896_read32(struct udevice *dev, uint8_t port, uint16_t reg)
{
	uint32_t val;
	if (dm_i2c_read(dev, port << 12 | reg, (uint8_t *)&val, sizeof(val))) {
		return -1;
	}
    return __be32_to_cpu(val);
}

static int ksz9896_write_mmd(struct udevice *dev, uint8_t port, uint8_t dev_addr, uint16_t reg_addr, uint16_t data)
{
    //Select register operation
    ksz9896_write16(dev, port, KSZ9896_MMDACR,
       KSZ9896_MMDACR_FUNC_ADDR | (dev_addr & KSZ9896_MMDACR_DEVAD));
  
    //Write MMD register address
    ksz9896_write16(dev, port, KSZ9896_MMDAADR, reg_addr);
  
    //Select data operation
    ksz9896_write16(dev, port, KSZ9896_MMDACR,
       KSZ9896_MMDACR_FUNC_DATA_NO_POST_INC | (dev_addr & KSZ9896_MMDACR_DEVAD));
  
    //Write the content of the MMD register
    ksz9896_write16(dev, port, KSZ9896_MMDAADR, data);

	return 0;
}

int ksz9896_init(void)
{
	struct udevice *dev;
	uint32_t id = 0;
    uint8_t xmii_ctrl_1 = 0x18;

	if (i2c_get_chip_for_busnum(0, 0x5f, 2, &dev) < 0)
		return -1;

	id = ksz9896_read32(dev, 0, 0);

    if (id != 0x00989600) {
		printf("KSZ9698: invalid ID: %08x\n", id);
		return 1;
	}

	// Enable RGMII Ingress Internal Delay (RGMII_ID_ig)
	dm_i2c_write(dev, 0x6301, &xmii_ctrl_1, 1);

	// Apply errata as per.
	// See 'KSZ9896C Silicon Errata and Data Sheet Clarification'
	// https://ww1.microchip.com/downloads/en/DeviceDoc/80000757C.pdf
	for(int port = 1; port <= 5; port++) {
       //Improve PHY receive performance (silicon errata workaround 1)
       ksz9896_write_mmd(dev, port, 0x01, 0x6F, 0xDD0B);
       ksz9896_write_mmd(dev, port, 0x01, 0x8F, 0x6032);
       ksz9896_write_mmd(dev, port, 0x01, 0x9D, 0x248C);
       ksz9896_write_mmd(dev, port, 0x01, 0x75, 0x0060);
       ksz9896_write_mmd(dev, port, 0x01, 0xD3, 0x7777);
       ksz9896_write_mmd(dev, port, 0x1C, 0x06, 0x3008);
       ksz9896_write_mmd(dev, port, 0x1C, 0x08, 0x2001);
  
       //Improve transmit waveform amplitude (silicon errata workaround 2)
       ksz9896_write_mmd(dev, port, 0x1C, 0x04, 0x00D0);
  
       //EEE must be manually disabled (silicon errata workaround 3)
       ksz9896_write_mmd(dev, port, KSZ9896_MMD_EEE_ADV, 0);
  
       //Adjust power supply settings (silicon errata workaround 6)
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
  
       //Select tri-color dual-LED mode (silicon errata workaround 14)
       ksz9896_write_mmd(dev, port, KSZ9896_MMD_LED_MODE,
          KSZ9896_MMD_LED_MODE_LED_MODE_TRI_COLOR_DUAL |
          KSZ9896_MMD_LED_MODE_RESERVED_DEFAULT);
	}

	return 0;
}

static uint32_t ksz9896_read_mib(struct udevice *dev, uint8_t port, uint8_t mib_index)
{
	uint8_t ctrl[4] = {0x2, mib_index, 0, 0};
	if (dm_i2c_write(dev, 0x500 | port << 12, ctrl, 4)) {
		printf("cannot write %d 0x%x\n", port, mib_index);
		return -1;
	}
	udelay(1);
	return ksz9896_read32(dev, port, 0x504);
}

static int do_ksz9896(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct udevice *dev;
	if (i2c_get_chip_for_busnum(0, 0x5f, 2, &dev) < 0) {
		printf("No KSZ9896 detected in I2C bus\n");
		return 1;
	}

	if (ksz9896_read32(dev, 0, 0) != 0x00989600) {
		printf("KSZ9698 chip present, but invalid id\n");
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

