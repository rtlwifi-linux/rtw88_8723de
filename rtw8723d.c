// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "rx.h"
#include "phy.h"
#include "rtw8723d.h"
#include "rtw8723d_table.h"
#include "mac.h"
#include "reg.h"
#include "debug.h"

static struct rtw_chip_ops rtw8723d_ops = {
	.read_rf		= rtw_phy_read_rf_sipi,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_antenna		= NULL,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
};

static struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8723d[] = {
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(7), 0},
	{0x0086, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0086, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x004A, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_USB_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), 0},
	{0x0023, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x0301, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_PCI_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd trans_cardemu_to_act_8723d[] = {
	{0x0020, RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC, RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0001, RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC, RTW_PWR_CMD_DELAY, 1, RTW_PWR_DELAY_MS},
	{0x0000, RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC, RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3) | BIT(2)), 0},
	{0x0075, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_PCI_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0006, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x0075, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_PCI_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0006, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, (BIT(1) | BIT(0)), 0},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(0), 0},
	{0x0010, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0049, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0063, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0062, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0058, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x005A, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0068, RTW_PWR_CUT_TEST_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0069, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x001f, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0077, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x001f, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x07},
	{0x0077, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x07},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd *card_enable_flow_8723d[] = {
	trans_carddis_to_cardemu_8723d,
	trans_cardemu_to_act_8723d,
	NULL
};

static struct rtw_pwr_seq_cmd trans_act_to_lps_8723d[] = {
	{0x0301, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_PCI_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x0522, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x05F8, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05F9, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FA, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FB, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x0002, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0002, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 0, RTW_PWR_DELAY_US},
	{0x0002, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0100, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x03},
	{0x0101, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0093, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0553, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd trans_act_to_pre_carddis_8723d[] = {
	{0x0003, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0080, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd trans_act_to_cardemu_8723d[] = {
	{0x0002, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0049, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0006, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0010, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), 0},
	{0x0000, RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0020, RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8723d[] = {
	{0x0007, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0005, RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_PCI_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x0005, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_PCI_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) | BIT(4)},
	{0x004A, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_USB_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 1},
	{0x0023, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0086, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0086, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_SDIO_MSK, RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd trans_act_to_post_carddis_8723d[] = {
	{0x001D, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x001D, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x001C, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x0E},
	{0xFFFF, RTW_PWR_CUT_ALL_MSK, RTW_PWR_INTF_ALL_MSK, 0, RTW_PWR_CMD_END,
	 0, 0},
};

static struct rtw_pwr_seq_cmd *card_disable_flow_8723d[] = {
	trans_act_to_lps_8723d,
	trans_act_to_pre_carddis_8723d,
	trans_act_to_cardemu_8723d,
	trans_cardemu_to_carddis_8723d,
	trans_act_to_post_carddis_8723d,
	NULL
};

static struct rtw_rf_sipi_addr rtw8723d_rf_sipi_addr[] = {
	[0] = { .hssi_1 = 0x820, .lssi_read    = 0x8a0,
		.hssi_2 = 0x824, .lssi_read_pi = 0x8b8},
	[1] = { .hssi_1 = 0x828, .lssi_read    = 0x8a4,
		.hssi_2 = 0x82c, .lssi_read_pi = 0x8bc},
};

struct rtw_chip_info rtw8723d_hw_spec = {
	.ops = &rtw8723d_ops,
	.id = RTW_CHIP_TYPE_8723D,
	.fw_name = "rtw88/rtw8723d_fw.bin",
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 96 + 1,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G,
	.ht_supported = true,
	.vht_supported = false,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xFD,
	.pwr_on_seq = card_enable_flow_8723d,
	.pwr_off_seq = card_disable_flow_8723d,
	.rf_sipi_addr = {0x840, 0x844},
	.rf_sipi_read_addr = rtw8723d_rf_sipi_addr,
	.rf_phy_nr = 2,
	.wow_supported = false,
};
EXPORT_SYMBOL(rtw8723d_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8723d_fw.bin");
