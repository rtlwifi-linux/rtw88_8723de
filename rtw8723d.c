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

static const struct rtw_hw_reg rtw8723d_txagc[] = {
	[DESC_RATE1M]	= { .addr = 0xe08, .mask = 0x0000ff00 },
	[DESC_RATE2M]	= { .addr = 0x86c, .mask = 0x0000ff00 },
	[DESC_RATE5_5M]	= { .addr = 0x86c, .mask = 0x00ff0000 },
	[DESC_RATE11M]	= { .addr = 0x86c, .mask = 0xff000000 },
	[DESC_RATE6M]	= { .addr = 0xe00, .mask = 0x000000ff },
	[DESC_RATE9M]	= { .addr = 0xe00, .mask = 0x0000ff00 },
	[DESC_RATE12M]	= { .addr = 0xe00, .mask = 0x00ff0000 },
	[DESC_RATE18M]	= { .addr = 0xe00, .mask = 0xff000000 },
	[DESC_RATE24M]	= { .addr = 0xe04, .mask = 0x000000ff },
	[DESC_RATE36M]	= { .addr = 0xe04, .mask = 0x0000ff00 },
	[DESC_RATE48M]	= { .addr = 0xe04, .mask = 0x00ff0000 },
	[DESC_RATE54M]	= { .addr = 0xe04, .mask = 0xff000000 },
	[DESC_RATEMCS0]	= { .addr = 0xe10, .mask = 0x000000ff },
	[DESC_RATEMCS1]	= { .addr = 0xe10, .mask = 0x0000ff00 },
	[DESC_RATEMCS2]	= { .addr = 0xe10, .mask = 0x00ff0000 },
	[DESC_RATEMCS3]	= { .addr = 0xe10, .mask = 0xff000000 },
	[DESC_RATEMCS4]	= { .addr = 0xe14, .mask = 0x000000ff },
	[DESC_RATEMCS5]	= { .addr = 0xe14, .mask = 0x0000ff00 },
	[DESC_RATEMCS6]	= { .addr = 0xe14, .mask = 0x00ff0000 },
	[DESC_RATEMCS7]	= { .addr = 0xe14, .mask = 0xff000000 },
};

#define WLAN_TXQ_RPT_EN		0x1F
#define WLAN_SLOT_TIME		0x09
#define WLAN_RL_VAL		0x3030
#define WLAN_BAR_VAL		0x0201ffff
#define BIT_MASK_TBTT_HOLD	0x00000fff
#define BIT_SHIFT_TBTT_HOLD	8
#define BIT_MASK_TBTT_SETUP	0x000000ff
#define BIT_SHIFT_TBTT_SETUP	0
#define BIT_MASK_TBTT_MASK	((BIT_MASK_TBTT_HOLD << BIT_SHIFT_TBTT_HOLD) | \
				 (BIT_MASK_TBTT_SETUP << BIT_SHIFT_TBTT_SETUP))
#define TBTT_TIME(s, h)((((s) & BIT_MASK_TBTT_SETUP) << BIT_SHIFT_TBTT_SETUP) |\
			(((h) & BIT_MASK_TBTT_HOLD) << BIT_SHIFT_TBTT_HOLD))
#define WLAN_TBTT_TIME_NORMAL	TBTT_TIME(0x04, 0x80)
#define WLAN_TBTT_TIME_STOP_BCN	TBTT_TIME(0x04, 0x64)
#define WLAN_PIFS_VAL		0
#define WLAN_AGG_BRK_TIME	0x16
#define WLAN_NAV_PROT_LEN	0x0040
#define WLAN_SPEC_SIFS		0x100a
#define WLAN_RX_PKT_LIMIT	0x17
#define WLAN_MAX_AGG_NR		0x0A
#define WLAN_AMPDU_MAX_TIME	0x1C
#define WLAN_ANT_SEL		0x82
#define WLAN_LTR_IDLE_LAT	0x883C883C
#define WLAN_LTR_ACT_LAT	0x880B880B
#define WLAN_LTR_CTRL1		0xCB004010
#define WLAN_LTR_CTRL2		0x01233425

static void rtw8723d_lck(struct rtw_dev *rtwdev)
{
#define BIT_LCK		BIT(15)
	u8 val_ctx;
	u32 lc_cal, cnt;

	val_ctx = rtw_read8(rtwdev, REG_CTX);
	if ((val_ctx & BIT_MASK_CTX_TYPE) != 0)
		rtw_write8(rtwdev, REG_CTX, val_ctx & ~BIT_MASK_CTX_TYPE);
	else
		rtw_write8(rtwdev, REG_TXPAUSE, 0xFF);
	lc_cal = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal | BIT_LCK);
	for (cnt = 0; cnt < 100; cnt++) {
		if (rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, BIT_LCK) != 0x1)
			break;
		mdelay(10);
	}

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal);
	if ((val_ctx & BIT_MASK_CTX_TYPE) != 0)
		rtw_write8(rtwdev, REG_CTX, val_ctx);
	else
		rtw_write8(rtwdev, REG_TXPAUSE, 0x00);
}

#define RTW_DEF_OFDM_SWING_INDEX	28
#define RTW_OFDM_SWING_TABLE_SIZE	43
#define OFDM_SWING_A(swing)	((swing) & 0x000003FF)
#define OFDM_SWING_B(swing)	(((swing) & 0x0000FC00) >> 10)
#define OFDM_SWING_C(swing)	(((swing) & 0x003F0000) >> 16)
#define OFDM_SWING_D(swing)	(((swing) & 0xFFC00000) >> 22)
static const u32 rtw8723d_ofdm_swing_table[] = {
	0x0b40002d, 0x0c000030, 0x0cc00033, 0x0d800036, 0x0e400039, 0x0f00003c,
	0x10000040, 0x11000044, 0x12000048, 0x1300004c, 0x14400051, 0x15800056,
	0x16c0005b, 0x18000060, 0x19800066, 0x1b00006c, 0x1c800072, 0x1e400079,
	0x20000080, 0x22000088, 0x24000090, 0x26000098, 0x288000a2, 0x2ac000ab,
	0x2d4000b5, 0x300000c0, 0x32c000cb, 0x35c000d7, 0x390000e4, 0x3c8000f2,
	0x40000100, 0x43c0010f, 0x47c0011f, 0x4c000130, 0x50800142, 0x55400155,
	0x5a400169, 0x5fc0017f, 0x65400195, 0x6b8001ae, 0x71c001c7, 0x788001e2,
	0x7f8001fe,
};

#define RTW_DEF_CCK_SWING_INDEX	28
#define RTW_CCK_TABLE_SIZE	41
static const u32 rtw8723d_cck_swing_table[] = {
	0x0CD, 0x0D9, 0x0E6, 0x0F3, 0x102, 0x111, 0x121, 0x132, 0x144, 0x158,
	0x16C, 0x182, 0x198, 0x1B1, 0x1CA, 0x1E5, 0x202, 0x221, 0x241, 0x263,
	0x287, 0x2AE, 0x2D6, 0x301, 0x32F, 0x35F, 0x392, 0x3C9, 0x402, 0x43F,
	0x47F, 0x4C3, 0x50C, 0x558, 0x5A9, 0x5FF, 0x65A, 0x6BA, 0x720, 0x78C,
	0x7FF,
};

static_assert(ARRAY_SIZE(rtw8723d_ofdm_swing_table) == RTW_OFDM_SWING_TABLE_SIZE);
static_assert(ARRAY_SIZE(rtw8723d_cck_swing_table) == RTW_CCK_TABLE_SIZE);

static void rtw8723d_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 path;

	dm_info->default_ofdm_index = RTW_DEF_OFDM_SWING_INDEX;

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->delta_power_index[path] = 0;
		dm_info->delta_power_index_last[path] = 0;
	}
	dm_info->pwrtrack_trigger = false;
	dm_info->pwrtrack_initial_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
	dm_info->txagc_remnant_cck = 0;
	dm_info->txagc_remnant_ofdm = 0;
}

static void rtw8723d_phy_set_param(struct rtw_dev *rtwdev)
{
	u8 xtal_cap;
	u32 val32;

	/* power on BB/RF domain */
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN,
			BIT_FEN_EN_25_1 | BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);
	rtw_write8_set(rtwdev, REG_RF_CTRL,
		       BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write8(rtwdev, REG_AFE_CTRL1 + 1, 0x80);

	rtw_phy_load_tables(rtwdev);

	/* post init after header files config */
	rtw_write32_clr(rtwdev, REG_RCR, BIT_RCR_ADF);
	rtw_write8_set(rtwdev, REG_HIQ_NO_LMT_EN, BIT_HIQ_NO_LMT_EN_ROOT);
	rtw_write16_set(rtwdev, REG_AFE_CTRL_4, BIT_CK320M_AFE_EN | BIT_EN_SYN);

	xtal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
	rtw_write32_set(rtwdev, REG_FPGA0_RFMOD, BIT_CCKEN | BIT_OFDMEN);
	if ((rtwdev->efuse.afe >> 4) == 14) {
		rtw_write32_set(rtwdev, REG_AFE_CTRL3, BIT_XTAL_GMP_BIT4);
		rtw_write32_clr(rtwdev, REG_AFE_CTRL1, BITS_PLL);
		rtw_write32_set(rtwdev, REG_LDO_SWR_CTRL, BIT_XTA1);
		rtw_write32_clr(rtwdev, REG_LDO_SWR_CTRL, BIT_XTA0);
	}

	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	rtw_write16(rtwdev, REG_RETRY_LIMIT, WLAN_RL_VAL);
	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, WLAN_BAR_VAL);
	rtw_write8(rtwdev, REG_ATIMWND, 0x2);
	rtw_write8(rtwdev, REG_BCN_CTRL,
		   BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION | BIT_EN_TXBCN_RPT);
	val32 = rtw_read32(rtwdev, REG_TBTT_PROHIBIT);
	val32 &= ~BIT_MASK_TBTT_MASK;
	val32 |= WLAN_TBTT_TIME_STOP_BCN;
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT, val32);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_VAL);
	rtw_write8(rtwdev, REG_AGGR_BREAK_TIME, WLAN_AGG_BRK_TIME);
	rtw_write16(rtwdev, REG_NAV_PROT_LEN, WLAN_NAV_PROT_LEN);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS + 2, WLAN_SPEC_SIFS);
	rtw_write8(rtwdev, REG_SINGLE_AMPDU_CTRL, BIT_EN_SINGLE_APMDU);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RX_PKT_LIMIT);
	rtw_write8(rtwdev, REG_MAX_AGGR_NUM, WLAN_MAX_AGG_NR);
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, WLAN_AMPDU_MAX_TIME);
	rtw_write8(rtwdev, REG_LEDCFG2, WLAN_ANT_SEL);

	rtw_write32(rtwdev, REG_LTR_IDLE_LATENCY, WLAN_LTR_IDLE_LAT);
	rtw_write32(rtwdev, REG_LTR_ACTIVE_LATENCY, WLAN_LTR_ACT_LAT);
	rtw_write32(rtwdev, REG_LTR_CTRL_BASIC, WLAN_LTR_CTRL1);
	rtw_write32(rtwdev, REG_LTR_CTRL_BASIC + 4, WLAN_LTR_CTRL2);

	rtw_phy_init(rtwdev);

	rtw_write16_set(rtwdev, REG_TXDMA_OFFSET_CHK, BIT_DROP_DATA_EN);

	rtw8723d_lck(rtwdev);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x20);

	rtw8723d_pwrtrack_init(rtwdev);
}

static int rtw8723d_mac_init_system_cfg(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_CR, 0xff);
	mdelay(2);
	rtw_write8(rtwdev, REG_HWSEQ_CTRL, 0x7f);
	mdelay(2);

	rtw_write8_set(rtwdev, REG_SYS_CLKR, BIT_WAKEPAD_EN);
	rtw_write16_clr(rtwdev, REG_GPIO_MUXCFG, BIT_EN_SIC);

	rtw_write16(rtwdev, REG_CR, 0x2ff);

	return 0;
}

static void rtw8723de_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8723d_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
}

static int rtw8723d_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw8723d_efuse *map;
	int i;

	map = (struct rtw8723d_efuse *)log_map;

	efuse->rfe_option = 0;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->pa_type_2g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g[0];
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[0] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;
	efuse->afe = map->afe;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8723de_efuse_parsing(efuse, map);
		break;
	default:
		/* unsupported now */
		return -ENOTSUPP;
	}

	return 0;
}

static void query_phy_status_page0(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 min_rx_power = -120;
	u8 pwdb = GET_PHY_STAT_P0_PWDB(phy_status);

	pkt_stat->rx_power[RF_PATH_A] = pwdb - 97;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
}

static void query_phy_status_page1(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 rxsc, bw;
	s8 min_rx_power = -120;
	s8 rx_evm;

	if (pkt_stat->rate > DESC_RATE11M && pkt_stat->rate < DESC_RATEMCS0)
		rxsc = GET_PHY_STAT_P1_L_RXSC(phy_status);
	else
		rxsc = GET_PHY_STAT_P1_HT_RXSC(phy_status);

	if (GET_PHY_STAT_P1_RF_MODE(phy_status) == 0)
		bw = RTW_CHANNEL_WIDTH_20;
	else if ((rxsc == 1) || (rxsc == 2))
		bw = RTW_CHANNEL_WIDTH_20;
	else
		bw = RTW_CHANNEL_WIDTH_40;

	pkt_stat->rx_power[RF_PATH_A] = GET_PHY_STAT_P1_PWDB_A(phy_status) - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->bw = bw;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
	pkt_stat->rx_evm[RF_PATH_A] = GET_PHY_STAT_P1_RXEVM_A(phy_status);
	pkt_stat->rx_snr[RF_PATH_A] = GET_PHY_STAT_P1_RXSNR_A(phy_status);
	pkt_stat->cfo_tail[RF_PATH_A] = GET_PHY_STAT_P1_CFO_TAIL_A(phy_status);

	dm_info->curr_rx_rate = pkt_stat->rate;
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
	dm_info->rx_snr[RF_PATH_A] = pkt_stat->rx_snr[RF_PATH_A] >> 1;
	dm_info->cfo_tail[RF_PATH_A] = (pkt_stat->cfo_tail[RF_PATH_A] * 5) >> 1;

	rx_evm = clamp_t(s8, -pkt_stat->rx_evm[RF_PATH_A] >> 1, 0, 64);
	rx_evm &= 0x3F;	/* 64->0: second path of 1SS rate is 64 */
	dm_info->rx_evm_dbm[RF_PATH_A] = rx_evm;
}

static void query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			     struct rtw_rx_pkt_stat *pkt_stat)
{
	u8 page;

	page = *phy_status & 0xf;

	switch (page) {
	case 0:
		query_phy_status_page0(rtwdev, phy_status, pkt_stat);
		break;
	case 1:
		query_phy_status_page1(rtwdev, phy_status, pkt_stat);
		break;
	default:
		rtw_warn(rtwdev, "unused phy status page (%d)\n", page);
		return;
	}
}

static void rtw8723d_query_rx_desc(struct rtw_dev *rtwdev, u8 *rx_desc,
				   struct rtw_rx_pkt_stat *pkt_stat,
				   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hdr *hdr;
	u32 desc_sz = rtwdev->chip->rx_pkt_desc_sz;
	u8 *phy_status = NULL;

	memset(pkt_stat, 0, sizeof(*pkt_stat));

	pkt_stat->phy_status = GET_RX_DESC_PHYST(rx_desc);
	pkt_stat->icv_err = GET_RX_DESC_ICV_ERR(rx_desc);
	pkt_stat->crc_err = GET_RX_DESC_CRC32(rx_desc);
	pkt_stat->decrypted = !GET_RX_DESC_SWDEC(rx_desc);
	pkt_stat->is_c2h = GET_RX_DESC_C2H(rx_desc);
	pkt_stat->pkt_len = GET_RX_DESC_PKT_LEN(rx_desc);
	pkt_stat->drv_info_sz = GET_RX_DESC_DRV_INFO_SIZE(rx_desc);
	pkt_stat->shift = GET_RX_DESC_SHIFT(rx_desc);
	pkt_stat->rate = GET_RX_DESC_RX_RATE(rx_desc);
	pkt_stat->cam_id = GET_RX_DESC_MACID(rx_desc);
	pkt_stat->ppdu_cnt = 0;
	pkt_stat->tsf_low = GET_RX_DESC_TSFL(rx_desc);

	/* drv_info_sz is in unit of 8-bytes */
	pkt_stat->drv_info_sz *= 8;

	/* c2h cmd pkt's rx/phy status is not interested */
	if (pkt_stat->is_c2h)
		return;

	hdr = (struct ieee80211_hdr *)(rx_desc + desc_sz + pkt_stat->shift +
				       pkt_stat->drv_info_sz);
	if (pkt_stat->phy_status) {
		phy_status = rx_desc + desc_sz + pkt_stat->shift;
		query_phy_status(rtwdev, phy_status, pkt_stat);
	}

	rtw_rx_fill_rx_status(rtwdev, pkt_stat, hdr, rx_status, phy_status);
}

static
bool rtw8723d_check_spur_ov_thres(struct rtw_dev *rtwdev, u8 channel, u32 thres)
{
#define DIS_3WIRE	0xccf000c0
#define EN_3WIRE	0xccc000c0
#define START_PSD	0x400000
#define FREQ_CH13	0xFCCD
#define FREQ_CH14	0xFF9A

	u32 freq;
	bool ret = false;

	if (channel == 13)
		freq = FREQ_CH13;
	else if (channel == 14)
		freq = FREQ_CH14;
	else
		return false;

	rtw_write32(rtwdev, REG_ANALOG_P4, DIS_3WIRE);
	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_PSDFN, START_PSD | freq);

	msleep(30);
	if (rtw_read32(rtwdev, REG_PSDRPT) >= thres)
		ret = true;

	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_ANALOG_P4, EN_3WIRE);

	return ret;
}

static void rtw8723d_cfg_notch(struct rtw_dev *rtwdev, u8 channel, bool notch)
{
#define BIT_MASK_RXDSP	(BIT(28) | BIT(27) | BIT(26) | BIT(25) | BIT(24))
#define BIT_EN_RXDSP	BIT(9)
#define BIT_EN_CFOTRK	BIT(28)

	if (!notch)
		goto no_notch;

	switch (channel) {
	case 13:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0xB);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x04000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 14:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x5);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00080000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	default:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		break;
	}

	return;

no_notch:
	rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x1f);
	rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
	rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
	rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
	rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
	rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
	rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
}

static void rtw8723d_spur_cal(struct rtw_dev *rtwdev, u8 channel)
{
#define SPUR_THRES	0x16
	bool notch = false;

	if (channel < 13)
		goto do_notch;

	notch = rtw8723d_check_spur_ov_thres(rtwdev, channel, SPUR_THRES);

do_notch:
	rtw8723d_cfg_notch(rtwdev, channel, notch);
}

static void rtw8723d_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
#define RFCFGCH_CHANNEL_MASK	GENMASK(7, 0)
#define RFCFGCH_BW_MASK		(BIT(11) | BIT(10))
#define RFCFGCH_BW_20M		(BIT(11) | BIT(10))
#define RFCFGCH_BW_40M		(BIT(10))

	u32 rf_cfgch[2];

	rf_cfgch[0] = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);
	rf_cfgch[1] = rtw_read_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK);

	rf_cfgch[0] &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch[1] &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch[0] |= (channel & RFCFGCH_CHANNEL_MASK);
	rf_cfgch[1] |= (channel & RFCFGCH_CHANNEL_MASK);

	rf_cfgch[0] &= ~RFCFGCH_BW_MASK;
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rf_cfgch[0] |= RFCFGCH_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_cfgch[0] |= RFCFGCH_BW_40M;
		break;
	default:
		break;
	}

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, rf_cfgch[0]);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK, rf_cfgch[1]);

	rtw8723d_spur_cal(rtwdev, channel);
}

#define CCK_DFIR_NR	3
static const struct rtw_backup_info cck_dfir_cfg[][CCK_DFIR_NR] = {
	[0] = {
		{ .len = 4, .reg = 0xA24, .val = 0x64B80C1C },
		{ .len = 4, .reg = 0xA28, .val = 0x00008810 },
		{ .len = 4, .reg = 0xAAC, .val = 0x01235667 },
	},
	[1] = {
		{ .len = 4, .reg = 0xA24, .val = 0x0000B81C },
		{ .len = 4, .reg = 0xA28, .val = 0x00000000 },
		{ .len = 4, .reg = 0xAAC, .val = 0x00003667 },
	},
};

static void rtw8723d_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
#define BIT_CCK_SIDE_BAND	BIT(4)
#define BIT_MASK_RFMOD		BIT(0)
#define BIT_RXBB_DFIR_EN	BIT(19)
#define BIT_MASK_RXBB_DFIR	(BIT(27) | BIT(26) | BIT(25) | BIT(24))

	const struct rtw_backup_info *cck_dfir =
			channel <= 13 ? cck_dfir_cfg[0] : cck_dfir_cfg[1];
	int i;

	for (i = 0; i < CCK_DFIR_NR; i++, cck_dfir++)
		rtw_write32(rtwdev, cck_dfir->reg, cck_dfir->val);

	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_RXBB_DFIR_EN, 1);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_MASK_RXBB_DFIR, 0xa);
		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_RXBB_DFIR_EN, 0);
		rtw_write32_mask(rtwdev, REG_CCK0_SYS, BIT_CCK_SIDE_BAND,
				 (primary_ch_idx == RTW_SC_20_UPPER ? 1 : 0));
		break;
	default:
		break;
	}
}

static void rtw8723d_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	rtw8723d_set_channel_rf(rtwdev, channel, bw);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8723d_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
}

#define BIT_CFENDFORM		BIT(9)
#define BIT_WMAC_TCR_ERR0	BIT(12)
#define BIT_WMAC_TCR_ERR1	BIT(13)
#define BIT_TCR_CFG		(BIT_CFENDFORM | BIT_WMAC_TCR_ERR0 |	       \
				 BIT_WMAC_TCR_ERR1)
#define WLAN_RX_FILTER0		0xFFFF
#define WLAN_RX_FILTER1		0x400
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0x700060CE

static int rtw8723d_mac_init(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	rtw_write32(rtwdev, REG_TCR, BIT_TCR_CFG);

	rtw_write16(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP1, WLAN_RX_FILTER1);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);

	rtw_write32(rtwdev, REG_INT_MIG, 0);
	rtw_write32(rtwdev, REG_MCUTST_1, 0x0);

	rtw_write8(rtwdev, REG_MISC_CTRL, BIT_DIS_SECOND_CCA);
	rtw_write8(rtwdev, REG_2ND_CCA_CTRL, 0);

	return 0;
}

static void rtw8723d_shutdown(struct rtw_dev *rtwdev)
{
	rtw_write16_set(rtwdev, REG_HCI_OPT_CTRL, BIT_USB_SUS_DIS);
}

static void rtw8723d_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_LDO_EFUSE_CTRL + 3);
	if (enable) {
		ldo_pwr &= ~BIT_MASK_LDO25_VOLTAGE;
		ldo_pwr = (BIT_LDO25_VOLTAGE_V25 << 4) | BIT_LDO25_EN;
	} else {
		ldo_pwr &= ~BIT_LDO25_EN;
	}
	rtw_write8(rtwdev, REG_LDO_EFUSE_CTRL + 3, ldo_pwr);
}

static void
rtw8723d_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	const struct rtw_hw_reg *txagc;
	u8 rate, pwr_index;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];
		pwr_index = hal->tx_pwr_tbl[path][rate];

		if (rate >= ARRAY_SIZE(rtw8723d_txagc)) {
			rtw_warn(rtwdev, "rate 0x%x isn't supported\n", rate);
			continue;
		}
		txagc = &rtw8723d_txagc[rate];
		if (!txagc->addr) {
			rtw_warn(rtwdev, "rate 0x%x isn't defined\n", rate);
			continue;
		}

		rtw_write32_mask(rtwdev, txagc->addr, txagc->mask, pwr_index);
	}
}

static void rtw8723d_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int rs, path;

	for (path = 0; path < hal->rf_path_num; path++) {
		for (rs = 0; rs <= RTW_RATE_SECTION_HT_1S; rs++)
			rtw8723d_set_tx_power_index_by_rate(rtwdev, path, rs);
	}
}

static void rtw8723d_efuse_en(struct rtw_dev *rtwdev, bool enable)
{
	if (enable) {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_ELDR);
		rtw_write16_set(rtwdev, REG_SYS_CLKR, BIT_LOADER_CLK_EN | BIT_ANA8M);
	} else {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);
	}
}

static void rtw8723d_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 crc32_cnt;
	u32 val32;

	/* hold counter */
	rtw_write32_mask(rtwdev, REG_OFDM_FA_HOLDC_11N, BIT(31), 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT(31), 1);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT(12), 1);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT(14), 1);

	cck_fa_cnt = rtw_read32_mask(rtwdev, REG_CCK_FA_LSB_11N, MASKBYTE0);
	cck_fa_cnt += rtw_read32_mask(rtwdev, REG_CCK_FA_MSB_11N, MASKBYTE3) << 8;

	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE1_11N);
	ofdm_fa_cnt = (val32 & 0xffff);
	ofdm_fa_cnt += ((val32 & 0xffff0000) >> 16);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE2_11N);
	dm_info->ofdm_cca_cnt = (val32 & 0xffff);
	ofdm_fa_cnt += ((val32 & 0xffff0000) >> 16);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE3_11N);
	ofdm_fa_cnt += (val32 & 0xffff);
	ofdm_fa_cnt += ((val32 & 0xffff0000) >> 16);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE4_11N);
	ofdm_fa_cnt += (val32 & 0xffff);

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt = cck_fa_cnt + ofdm_fa_cnt;

	dm_info->cck_err_cnt = rtw_read32(rtwdev, REG_IGI_C_11N);
	dm_info->cck_ok_cnt = rtw_read32(rtwdev, REG_IGI_D_11N);
	crc32_cnt = rtw_read32(rtwdev, REG_OFDM_CRC32_CNT_11N);
	dm_info->ofdm_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	dm_info->ofdm_ok_cnt = crc32_cnt & 0xffff;
	crc32_cnt = rtw_read32(rtwdev, REG_HT_CRC32_CNT_11N);
	dm_info->ht_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	dm_info->ht_ok_cnt = crc32_cnt & 0xffff;
	dm_info->vht_err_cnt = 0;
	dm_info->vht_ok_cnt = 0;

	val32 = rtw_read32(rtwdev, REG_CCK_CCA_CNT_11N);
	dm_info->cck_cca_cnt = ((val32 & 0xFF) << 8) | ((val32 & 0xFF00) >> 8);
	dm_info->total_cca_cnt = dm_info->cck_cca_cnt + dm_info->ofdm_cca_cnt;

	/* reset counter */
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTC_11N, BIT(31), 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTC_11N, BIT(31), 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT(27), 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT(27), 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_HOLDC_11N, BIT(31), 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT(31), 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 2);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 2);
	rtw_write32_mask(rtwdev, REG_PAGE_F_RST_11N, BIT(16), 1);
	rtw_write32_mask(rtwdev, REG_PAGE_F_RST_11N, BIT(16), 0);
}

#define MAX_TOLERANCE 5

static const u32 iqk_adda_regs[] = {
	0x85c, 0xe6c, 0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84, 0xe88, 0xe8c,
	0xed0, 0xed4, 0xed8, 0xedc, 0xee0, 0xeec
};

static const u32 iqk_mac8_regs[] = {0x522, 0x550, 0x551};
static const u32 iqk_mac32_regs[] = {0x40};

static const u32 iqk_bb_regs[] = {
	0xc04, 0xc08, 0x874, 0xb68, 0xb6c, 0x870, 0x860, 0x864, 0xa04
};

#define IQK_ADDA_REG_NUM	ARRAY_SIZE(iqk_adda_regs)
#define IQK_MAC8_REG_NUM	ARRAY_SIZE(iqk_mac8_regs)
#define IQK_MAC32_REG_NUM	ARRAY_SIZE(iqk_mac32_regs)
#define IQK_BB_REG_NUM		ARRAY_SIZE(iqk_bb_regs)

struct iqk_backup_regs {
	u32 adda[IQK_ADDA_REG_NUM];
	u8 mac8[IQK_MAC8_REG_NUM];
	u32 mac32[IQK_MAC32_REG_NUM];
	u32 bb[IQK_BB_REG_NUM];

	u32 lte_path;
	u32 lte_gnt;

	u8 btg_sel;
	u32 bb_sel_btg;

	u8 igia;
	u8 igib;
};

static void rtw8723d_iqk_backup_regs(struct rtw_dev *rtwdev,
				     struct iqk_backup_regs *backup)
{
	int i;

	for (i = 0; i < IQK_ADDA_REG_NUM; i++)
		backup->adda[i] = rtw_read32(rtwdev, iqk_adda_regs[i]);

	for (i = 0; i < IQK_MAC8_REG_NUM; i++)
		backup->mac8[i] = rtw_read8(rtwdev, iqk_mac8_regs[i]);
	for (i = 0; i < IQK_MAC32_REG_NUM; i++)
		backup->mac32[i] = rtw_read32(rtwdev, iqk_mac32_regs[i]);

	for (i = 0; i < IQK_BB_REG_NUM; i++)
		backup->bb[i] = rtw_read32(rtwdev, iqk_bb_regs[i]);

	backup->igia = (u8)rtw_read32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0);
	backup->igib = (u8)rtw_read32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0);

	backup->bb_sel_btg = rtw_read32(rtwdev, REG_BB_SEL_BTG);
}

static void rtw8723d_iqk_restore_regs(struct rtw_dev *rtwdev,
				      const struct iqk_backup_regs *backup)
{
	int i;

	for (i = 0; i < IQK_ADDA_REG_NUM; i++)
		rtw_write32(rtwdev, iqk_adda_regs[i], backup->adda[i]);

	for (i = 0; i < IQK_MAC8_REG_NUM; i++)
		rtw_write8(rtwdev, iqk_mac8_regs[i], backup->mac8[i]);
	for (i = 0; i < IQK_MAC32_REG_NUM; i++)
		rtw_write32(rtwdev, iqk_mac32_regs[i], backup->mac32[i]);

	for (i = 0; i < IQK_BB_REG_NUM; i++)
		rtw_write32(rtwdev, iqk_bb_regs[i], backup->bb[i]);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, backup->igia);

	rtw_write32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0, backup->igib);

	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x01008c00);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x01008c00);
}

static void rtw8723d_iqk_backup_path_ctrl(struct rtw_dev *rtwdev,
					  struct iqk_backup_regs *backup)
{
	backup->btg_sel = rtw_read8(rtwdev, REG_BTG_SEL);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] original 0x67 = 0x%x\n",
		backup->btg_sel);
}

static void rtw8723d_iqk_config_path_ctrl(struct rtw_dev *rtwdev)
{
	rtw_write32_mask(rtwdev, REG_PAD_CTRL1, BIT_BT_BTG_SEL, 0x1);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] set 0x67 = 0x%x\n",
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));
}

static void rtw8723d_iqk_restore_path_ctrl(struct rtw_dev *rtwdev,
					   const struct iqk_backup_regs *backup)
{
	rtw_write8(rtwdev, REG_BTG_SEL, backup->btg_sel);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] restore 0x67 = 0x%x\n",
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));
}

static void rtw8723d_iqk_backup_lte_path_gnt(struct rtw_dev *rtwdev,
					     struct iqk_backup_regs *backup)
{
	backup->lte_path = rtw_read32(rtwdev, REG_LTECOEX_PATH_CONTROL);
	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0x800f0038);
	mdelay(1);
	backup->lte_gnt = rtw_read32(rtwdev, REG_LTECOEX_READ_DATA);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] OriginalGNT = 0x%x\n",
		backup->lte_gnt);
}

static void rtw8723d_iqk_config_lte_path_gnt(struct rtw_dev *rtwdev)
{
	rtw_write32(rtwdev, REG_LTECOEX_WRITE_DATA, 0x0000ff00);
	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0xc0020038);
	rtw_write32_mask(rtwdev, REG_LTECOEX_PATH_CONTROL, BIT_LTE_MUX_CTRL_PATH, 0x1);
}

static void rtw8723d_iqk_restore_lte_path_gnt(struct rtw_dev *rtwdev,
					      const struct iqk_backup_regs *bak)
{
	rtw_write32(rtwdev, REG_LTECOEX_WRITE_DATA, bak->lte_gnt);
	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0xc00f0038);
	rtw_write32(rtwdev, REG_LTECOEX_PATH_CONTROL, bak->lte_path);
}

struct rtw_8723d_iqk_cfg {
	const char *name;
	u32 val_bb_sel_btg;
	u32 reg_lutwe;
	u32 val_txiqk_pi;
	u32 reg_padlut;
	u32 reg_gaintx;
	u32 reg_bspad;
	u32 val_wlint;
	u32 val_wlsel;
	u32 val_iqkpts;
};

static const struct rtw_8723d_iqk_cfg iqk_tx_cfg[PATH_NR] = {
	[PATH_S1] = {
		.name = "S1",
		.val_bb_sel_btg = 0x99000000,
		.reg_lutwe = RF_LUTWE,
		.val_txiqk_pi = 0x8214019f,
		.reg_padlut = RF_LUTDBG,
		.reg_gaintx = RF_GAINTX,
		.reg_bspad = RF_BSPAD,
		.val_wlint = 0xe0d,
		.val_wlsel = 0x60d,
		.val_iqkpts = 0xfa000000,
	},
	[PATH_S0] = {
		.name = "S0",
		.val_bb_sel_btg = 0x99000280,
		.reg_lutwe = RF_LUTWE2,
		.val_txiqk_pi = 0x8214018a,
		.reg_padlut = RF_TXADBG,
		.reg_gaintx = RF_TRXIQ,
		.reg_bspad = RF_TXATANK,
		.val_wlint = 0xe6d,
		.val_wlsel = 0x66d,
		.val_iqkpts = 0xf9000000,
	},
};

enum {
	IQK_TX_OK = BIT(0),
	IQK_RX_OK = BIT(1),
};

static u8 rtw8723d_iqk_check_tx_failed(struct rtw_dev *rtwdev,
				       const struct rtw_8723d_iqk_cfg *iqk_cfg)
{
	u32 tx_x, tx_y, tx_fail;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xeac = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_RY));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xe94 = 0x%x, 0xe9c = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_TX),
		rtw_read32(rtwdev, REG_IQK_RES_TY));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		rtw_read32(rtwdev, 0xe90),
		rtw_read32(rtwdev, 0xe98));

	tx_fail = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_IQK_TX_FAIL);
	tx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
	tx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);

	if (!tx_fail && tx_x != 0x142 && tx_y != 0x42)
		return IQK_TX_OK;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] %s TXIQK is failed\n",
		iqk_cfg->name);

	return 0;
}

static u8 rtw8723d_iqk_check_rx_failed(struct rtw_dev *rtwdev,
				       const struct rtw_8723d_iqk_cfg *iqk_cfg)
{
	u32 rx_x, rx_fail;
	s32 rx_y;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xea4 = 0x%x, 0xeac = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_RX),
		rtw_read32(rtwdev, REG_IQK_RES_RY));

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
		rtw_read32(rtwdev, 0xea0),
		rtw_read32(rtwdev, 0xea8));

	rx_fail = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_IQK_RX_FAIL);
	rx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
	rx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
	rx_y = abs(bits_to_s32(rx_y, 9, 0));

	if (!rx_fail && rx_x != 0x132 && rx_x < 0x11a && rx_x > 0xe6 &&
	    rx_y != 0x36 && rx_y < 0x1a)
		return IQK_RX_OK;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] %s RXIQK STEP2 is failed\n",
		iqk_cfg->name);

	return 0;
}

static void rtw8723d_iqk_one_shot(struct rtw_dev *rtwdev, bool tx,
				  const struct rtw_8723d_iqk_cfg *iqk_cfg)
{
	u32 pts = (tx ? iqk_cfg->val_iqkpts : 0xf9000000);

	/* enter IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
	rtw8723d_iqk_config_lte_path_gnt(rtwdev);

	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0x800f0054);
	mdelay(1);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] GNT_BT @%s %sIQK1 = 0x%x\n",
		iqk_cfg->name, tx ? "TX" : "RX",
		rtw_read32(rtwdev, REG_LTECOEX_READ_DATA));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x948 @%s %sIQK1 = 0x%x\n",
		iqk_cfg->name, tx ? "TX" : "RX",
		rtw_read32(rtwdev, REG_BB_SEL_BTG));

	/* One shot, LOK & IQK */
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, pts);
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf8000000);

	if (!check_hw_ready(rtwdev, REG_IQK_RES_RY, BIT_IQK_DONE, 1))
		rtw_warn(rtwdev, "%s %s IQK isn't done\n", iqk_cfg->name,
			 tx ? "TX" : "RX");
}

static void rtw8723d_iqk_txrx_path_post(struct rtw_dev *rtwdev,
					const struct rtw_8723d_iqk_cfg *iqk_cfg,
					const struct iqk_backup_regs *backup)
{
	rtw8723d_iqk_restore_lte_path_gnt(rtwdev, backup);
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup->bb_sel_btg);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_padlut, 0x800, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, BIT(0), 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLSEL, BIT(0), 0x0);
}

static u8 rtw8723d_iqk_tx_path(struct rtw_dev *rtwdev,
			       const struct rtw_8723d_iqk_cfg *iqk_cfg,
			       const struct iqk_backup_regs *backup)
{
	u8 result = 0x00;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path %s TXIQK!!\n", iqk_cfg->name);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @%s TXIQK = 0x%x\n",
		iqk_cfg->name,
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));

	rtw_write32(rtwdev, REG_BB_SEL_BTG, iqk_cfg->val_bb_sel_btg);
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x80000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00004);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x0005d);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0xBFFE0);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x00000);

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x08008c0c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, iqk_cfg->val_txiqk_pi);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28160200);
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x00462911);

	/* PA, PAD setting */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_padlut, 0x800, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_gaintx, 0x600, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_gaintx, 0x1E0, 0x3);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_RXIQGEN, 0x1F, 0xf);

	/* LOK setting for 8723D */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, 0x10, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_bspad, 0x1, 0x1);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, iqk_cfg->val_wlint);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK, iqk_cfg->val_wlsel);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x1 @%s TXIQK = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x2 @%s TXIQK = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK));

	rtw8723d_iqk_one_shot(rtwdev, true, iqk_cfg);
	result |= rtw8723d_iqk_check_tx_failed(rtwdev, iqk_cfg);

	rtw8723d_iqk_txrx_path_post(rtwdev, iqk_cfg, backup);

	return result;
}

static u8 rtw8723d_iqk_rx_path(struct rtw_dev *rtwdev,
			       const struct rtw_8723d_iqk_cfg *iqk_cfg,
			       const struct iqk_backup_regs *backup)
{
	u32 tx_x, tx_y;
	u8 result = 0x00;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path %s RXIQK Step1!!\n",
		iqk_cfg->name);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @%s RXIQK1 = 0x%x\n",
		iqk_cfg->name,
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));
	rtw_write32(rtwdev, REG_BB_SEL_BTG, iqk_cfg->val_bb_sel_btg);

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* path IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82160000);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28160000);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a911);

	/* RXIQK mode */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x80000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00006);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x0005f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0xa7ffb);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x00000);

	/* PA/PAD=0 */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_padlut, 0x800, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_gaintx, 0x600, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, iqk_cfg->val_wlint);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK, iqk_cfg->val_wlsel);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x1@ path %s RXIQK1 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x2@ path %s RXIQK1 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK));

	rtw8723d_iqk_one_shot(rtwdev, false, iqk_cfg);
	result |= rtw8723d_iqk_check_tx_failed(rtwdev, iqk_cfg);

	if (!result)
		goto restore;

	tx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
	tx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);

	rtw_write32(rtwdev, REG_TXIQK_11N, BIT_SET_TXIQK_11N(tx_x, tx_y));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xe40 = 0x%x u4tmp = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQK_11N),
		BIT_SET_TXIQK_11N(tx_x, tx_y));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path %s RXIQK STEP2!!\n",
		iqk_cfg->name);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @%s RXIQK2 = 0x%x\n",
		iqk_cfg->name,
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));

	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82170000);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28171400);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a8d1);

	/* RXIQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00007);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x0005f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0xb3fdb);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x00000);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x1 @%s RXIQK2 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x2 @%s RXIQK2 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK));

	rtw8723d_iqk_one_shot(rtwdev, false, iqk_cfg);
	result |= rtw8723d_iqk_check_rx_failed(rtwdev, iqk_cfg);

restore:
	rtw8723d_iqk_txrx_path_post(rtwdev, iqk_cfg, backup);

	return result;
}

static
void rtw8723d_iqk_fill_s1_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
	u32 oldval_1, x, tx1_a, tx1_a_ext;
	s32 y, tx1_c, tx1_c_ext;

	if (result[IQK_S1_TX_X] == 0)
		return;

	oldval_1 = rtw_read32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
				   BIT_MASK_TXIQ_ELM_D);

	x = bits_to_s32(result[IQK_S1_TX_X], 9, 0);
	tx1_a = q16_to_q8(x * oldval_1);
	tx1_a_ext = q16_to_q9(x * oldval_1) & 0x1;
	rtw_write32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_A, tx1_a);
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD,
			 BIT_MASK_OFDM0_EXT_A, tx1_a_ext);

	y = bits_to_s32(result[IQK_S1_TX_Y], 9, 0);
	tx1_c = q16_to_q8(y * oldval_1);
	tx1_c_ext = q16_to_q9(y * oldval_1) & 0x1;
	rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
			 BIT_SET_TXIQ_ELM_C1(tx1_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_C, BIT_SET_TXIQ_ELM_C2(tx1_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD,
			 BIT_MASK_OFDM0_EXT_C, tx1_c_ext);

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] X = 0x%x, TX1_A = 0x%x, oldval_1 0x%x\n",
		x, tx1_a, oldval_1);
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] Y = 0x%x, TX1_C = 0x%x\n", y, tx1_c);

	if (result[IQK_S1_RX_X] == 0)
		return;

	rtw_write32_mask(rtwdev, REG_A_RXIQI, BIT_MASK_RXIQ_S1_X,
			 result[IQK_S1_RX_X]);
	rtw_write32_mask(rtwdev, REG_A_RXIQI, BIT_MASK_RXIQ_S1_Y1,
			 BIT_SET_RXIQ_S1_Y1(result[IQK_S1_RX_Y]));
	rtw_write32_mask(rtwdev, REG_RXIQK_MATRIX_LSB_11N, BIT_MASK_RXIQ_S1_Y2,
			 BIT_SET_RXIQ_S1_Y2(result[IQK_S1_RX_Y]));
}

static
void rtw8723d_iqk_fill_s0_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
	u32 oldval_0, x, tx0_a, tx0_a_ext;
	s32 y, tx0_c, tx0_c_ext;

	if (result[IQK_S0_TX_X] == 0)
		return;

	oldval_0 = rtw_read32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_D_S0);

	x = bits_to_s32(result[IQK_S0_TX_X], 9, 0);
	tx0_a = q16_to_q8(x * oldval_0);
	tx0_a_ext = q16_to_q9(x * oldval_0) & 0x1;

	rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_S0, tx0_a);
	rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_EXT_S0, tx0_a_ext);

	y = bits_to_s32(result[IQK_S0_TX_Y], 9, 0);
	tx0_c = q16_to_q8(y * oldval_0) & MASK_Q10_8;
	tx0_c_ext = q16_to_q9(y * oldval_0) & 0x1;

	rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_S0, tx0_c);
	rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_EXT_S0, tx0_c_ext);

	if (result[IQK_S0_RX_X] == 0)
		return;

	rtw_write32_mask(rtwdev, REG_RXIQ_AB_S0, BIT_MASK_RXIQ_X_S0,
			 result[IQK_S0_RX_X]);
	rtw_write32_mask(rtwdev, REG_RXIQ_AB_S0, BIT_MASK_RXIQ_Y_S0,
			 result[IQK_S0_RX_Y]);
}

static void rtw8723d_iqk_path_adda_on(struct rtw_dev *rtwdev)
{
	int i;

	for (i = 0; i < IQK_ADDA_REG_NUM; i++)
		rtw_write32(rtwdev, iqk_adda_regs[i], 0x03c00016);
}

static void rtw8723d_iqk_config_mac(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_TXPAUSE, 0xff);
}

static
void rtw8723d_iqk_rf_standby(struct rtw_dev *rtwdev, enum rtw_rf_path path)
{
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path-%s standby mode!\n",
		path == RF_PATH_A ? "S1" : "S0");

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, path, RF_MODE, RFREG_MASK, 0x10000);
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
}

static
bool rtw8723d_iqk_similarity_cmp(struct rtw_dev *rtwdev, s32 result[][IQK_NR],
				 u8 c1, u8 c2)
{
	u32 i, j, diff;
	u32 bitmap = 0;
	u8 candidate[PATH_NR] = {IQK_ROUND_INVALID, IQK_ROUND_INVALID};
	bool ret = true;

	s32 tmp1, tmp2;

	for (i = 0; i < IQK_NR; i++) {
		tmp1 = bits_to_s32(result[c1][i], 9, 0);
		tmp2 = bits_to_s32(result[c2][i], 9, 0);

		diff = abs(tmp1 - tmp2);

		if (diff <= MAX_TOLERANCE)
			continue;

		if ((i == IQK_S1_RX_X || i == IQK_S0_RX_X) && !bitmap) {
			if (result[c1][i] + result[c1][i + 1] == 0)
				candidate[i / IQK_SX_NR] = c2;
			else if (result[c2][i] + result[c2][i + 1] == 0)
				candidate[i / IQK_SX_NR] = c1;
			else
				bitmap |= BIT(i);
		} else {
			bitmap |= BIT(i);
		}
	}

	if (bitmap != 0)
		goto check_sim;

	for (i = 0; i < PATH_NR; i++) {
		if (candidate[i] == IQK_ROUND_INVALID)
			continue;

		for (j = i * IQK_SX_NR; j < i * IQK_SX_NR + 2; j++)
			result[IQK_ROUND_HYBRID][j] = result[candidate[i]][j];
		ret = false;
	}

	return ret;

check_sim:
	for (i = 0; i < IQK_NR; i++) {
		j = i & ~1;	/* 2 bits are a pair for IQ[X, Y] */
		if (bitmap & GENMASK(j + 1, j))
			continue;

		result[IQK_ROUND_HYBRID][i] = result[c1][i];
	}

	return false;
}

static
void rtw8723d_iqk_precfg_path(struct rtw_dev *rtwdev, enum rtw8723d_path path)
{
	if (path == PATH_S0) {
		rtw8723d_iqk_rf_standby(rtwdev, RF_PATH_A);
		rtw8723d_iqk_path_adda_on(rtwdev);
	}

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	if (path == PATH_S1) {
		rtw8723d_iqk_rf_standby(rtwdev, RF_PATH_B);
		rtw8723d_iqk_path_adda_on(rtwdev);
	}
}

#define PATH_IQK_RETRY	2

static
void rtw8723d_iqk_one_round(struct rtw_dev *rtwdev, s32 result[][IQK_NR], u8 t,
			    const struct iqk_backup_regs *backup)
{
	u32 i;
	u8 s1_ok, s0_ok;

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] IQ Calibration for 1T1R_S0/S1 for %d times\n", t);

	rtw8723d_iqk_path_adda_on(rtwdev);
	rtw8723d_iqk_config_mac(rtwdev);
	rtw_write32_mask(rtwdev, REG_CCK_ANT_SEL_11N, 0x0f000000, 0xf);
	rtw_write32(rtwdev, REG_BB_RX_PATH_11N, 0x03a05611);
	rtw_write32(rtwdev, REG_TRMUX_11N, 0x000800e4);
	rtw_write32(rtwdev, REG_BB_PWR_SAV1_11N, 0x25204200);
	rtw8723d_iqk_precfg_path(rtwdev, PATH_S1);

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s1_ok = rtw8723d_iqk_tx_path(rtwdev, &iqk_tx_cfg[PATH_S1], backup);
		if (s1_ok == IQK_TX_OK) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S1 Tx IQK Success!!\n");
			result[t][IQK_S1_TX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
			result[t][IQK_S1_TX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S1 Tx IQK Fail!!\n");
		result[t][IQK_S1_TX_X] = 0x100;
		result[t][IQK_S1_TX_Y] = 0x0;
	}

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s1_ok = rtw8723d_iqk_rx_path(rtwdev, &iqk_tx_cfg[PATH_S1], backup);
		if (s1_ok == (IQK_TX_OK | IQK_RX_OK)) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S1 Rx IQK Success!!\n");
			result[t][IQK_S1_RX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
			result[t][IQK_S1_RX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S1 Rx IQK Fail!!\n");
		result[t][IQK_S1_RX_X] = 0x100;
		result[t][IQK_S1_RX_Y] = 0x0;
	}

	if (s1_ok == 0x0)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S1 IQK is failed!!\n");

	rtw8723d_iqk_precfg_path(rtwdev, PATH_S0);

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s0_ok = rtw8723d_iqk_tx_path(rtwdev, &iqk_tx_cfg[PATH_S0], backup);
		if (s0_ok == IQK_TX_OK) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S0 Tx IQK Success!!\n");
			result[t][IQK_S0_TX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
			result[t][IQK_S0_TX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S0 Tx IQK Fail!!\n");
		result[t][IQK_S0_TX_X] = 0x100;
		result[t][IQK_S0_TX_Y] = 0x0;
	}

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s0_ok = rtw8723d_iqk_rx_path(rtwdev, &iqk_tx_cfg[PATH_S0], backup);
		if (s0_ok == (IQK_TX_OK | IQK_RX_OK)) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S0 Rx IQK Success!!\n");

			result[t][IQK_S0_RX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
			result[t][IQK_S0_RX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S0 Rx IQK Fail!!\n");
		result[t][IQK_S0_RX_X] = 0x100;
		result[t][IQK_S0_RX_Y] = 0x0;
	}

	if (s0_ok == 0x0)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S0 IQK is failed!!\n");

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] back to BB mode, load original value!\n");
}

static void rtw8723d_phy_calibration(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s32 result[IQK_ROUND_SIZE][IQK_NR];
	struct iqk_backup_regs backup;
	u8 i, j;
	u8 final_candidate = IQK_ROUND_INVALID;
	bool good;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] Start!!!\n");

	memset(result, 0, sizeof(result));

	rtw8723d_iqk_backup_path_ctrl(rtwdev, &backup);
	rtw8723d_iqk_backup_lte_path_gnt(rtwdev, &backup);
	rtw8723d_iqk_backup_regs(rtwdev, &backup);

	for (i = IQK_ROUND_0; i <= IQK_ROUND_2; i++) {
		rtw8723d_iqk_config_path_ctrl(rtwdev);
		rtw8723d_iqk_config_lte_path_gnt(rtwdev);

		rtw8723d_iqk_one_round(rtwdev, result, i, &backup);

		if (i > IQK_ROUND_0)
			rtw8723d_iqk_restore_regs(rtwdev, &backup);
		rtw8723d_iqk_restore_lte_path_gnt(rtwdev, &backup);
		rtw8723d_iqk_restore_path_ctrl(rtwdev, &backup);

		for (j = IQK_ROUND_0; j < i; j++) {
			good = rtw8723d_iqk_similarity_cmp(rtwdev, result, j, i);

			if (good) {
				final_candidate = j;
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[IQK] cmp %d:%d final_candidate is %x\n",
					j, i, final_candidate);
				goto iqk_done;
			}
		}
	}

	if (final_candidate == IQK_ROUND_INVALID) {
		s32 reg_tmp = 0;

		for (i = 0; i < IQK_NR; i++)
			reg_tmp += result[IQK_ROUND_HYBRID][i];

		if (reg_tmp != 0) {
			final_candidate = IQK_ROUND_HYBRID;
		} else {
			WARN(1, "IQK is failed\n");
			goto out;
		}
	}

iqk_done:
	rtw8723d_iqk_fill_s1_matrix(rtwdev, result[final_candidate]);
	rtw8723d_iqk_fill_s0_matrix(rtwdev, result[final_candidate]);

	dm_info->iqk.result.s1_x = result[final_candidate][IQK_S1_TX_X];
	dm_info->iqk.result.s1_y = result[final_candidate][IQK_S1_TX_Y];
	dm_info->iqk.result.s0_x = result[final_candidate][IQK_S0_TX_X];
	dm_info->iqk.result.s0_y = result[final_candidate][IQK_S0_TX_Y];
	dm_info->iqk.done = true;

out:
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup.bb_sel_btg);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] final_candidate is %x\n",
		final_candidate);

	for (i = IQK_ROUND_0; i < IQK_ROUND_SIZE; i++)
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] rege94_s1=%x rege9c_s1=%x regea4_s1=%x regeac_s1=%x rege94_s0=%x rege9c_s0=%x regea4_s0=%x regeac_s0=%x %s\n",
			result[i][0], result[i][1], result[i][2], result[i][3],
			result[i][4], result[i][5], result[i][6], result[i][7],
			final_candidate == i ? "(final candidate)" : "");

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK]0xc80 = 0x%x 0xc94 = 0x%x 0xc14 = 0x%x 0xca0 = 0x%x\n",
		rtw_read32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE),
		rtw_read32(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N),
		rtw_read32(rtwdev, REG_A_RXIQI),
		rtw_read32(rtwdev, REG_RXIQK_MATRIX_LSB_11N));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK]0xcd0 = 0x%x 0xcd4 = 0x%x 0xcd8 = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQ_AB_S0),
		rtw_read32(rtwdev, REG_TXIQ_CD_S0),
		rtw_read32(rtwdev, REG_RXIQ_AB_S0));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] finished\n");
}

static u8 rtw8723d_pwrtrack_get_limit_ofdm(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 tx_rate = dm_info->tx_rate;
	u8 limit_ofdm = 30;

	switch (tx_rate) {
	case DESC_RATE1M...DESC_RATE5_5M:
	case DESC_RATE11M:
		break;
	case DESC_RATE6M...DESC_RATE48M:
		limit_ofdm = 36;
		break;
	case DESC_RATE54M:
		limit_ofdm = 34;
		break;
	case DESC_RATEMCS0...DESC_RATEMCS2:
		limit_ofdm = 38;
		break;
	case DESC_RATEMCS3...DESC_RATEMCS4:
		limit_ofdm = 36;
		break;
	case DESC_RATEMCS5...DESC_RATEMCS7:
		limit_ofdm = 34;
		break;
	default:
		rtw_warn(rtwdev, "pwrtrack unhandled tx_rate 0x%x\n", tx_rate);
		break;
	}

	return limit_ofdm;
}

static void _rtw8723d_set_iqk_matrix(struct rtw_dev *rtwdev, u32 ofdm_swing,
				     u8 rf_path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s32 ele_A, ele_D, ele_C;
	s32 ele_A_ext, ele_C_ext, ele_D_ext;
	s32 iqk_result_x;
	s32 iqk_result_y;
	s32 value32;

	switch (rf_path) {
	default:
	case RF_PATH_A:
		iqk_result_x = dm_info->iqk.result.s1_x;
		iqk_result_y = dm_info->iqk.result.s1_y;
		break;
	case RF_PATH_B:
		iqk_result_x = dm_info->iqk.result.s0_x;
		iqk_result_y = dm_info->iqk.result.s0_y;
		break;
	}

	/* new element D */
	ele_D = OFDM_SWING_D(ofdm_swing);
	ele_D_ext = q16_to_q9(iqk_result_x * ele_D) & 0x1;
	/* new element A */
	iqk_result_x = bits_to_s32(iqk_result_x, 9, 0);
	ele_A = q16_to_q8(iqk_result_x * ele_D) & MASK_Q10_8;
	ele_A_ext = q16_to_q9(iqk_result_x * ele_D) & 0x1;
	/* new element C */
	iqk_result_y = bits_to_s32(iqk_result_y, 9, 0);
	ele_C = q16_to_q8(iqk_result_y * ele_D) & MASK_Q10_8;
	ele_C_ext = q16_to_q9(iqk_result_y * ele_D) & 0x1;

	switch (rf_path) {
	case RF_PATH_A:
	default:
		/* write new elements A, C, D, and element B is always 0 */
		value32 = BIT_SET_TXIQ_ELM_ACD(ele_A, ele_C, ele_D);
		rtw_write32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE, value32);
		value32 = BIT_SET_TXIQ_ELM_C1(ele_C);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
				 value32);
		value32 = rtw_read32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD);
		value32 &= ~BIT_MASK_OFDM0_EXTS;
		value32 |= BIT_SET_OFDM0_EXTS(ele_A_ext, ele_C_ext, ele_D_ext);
		rtw_write32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, value32);
		break;

	case RF_PATH_B:
		/* write new elements A, C, D, and element B is always 0 */
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_D_S0, ele_D);
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_S0, ele_C);
		rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_S0, ele_A);

		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_D_EXT_S0,
				 ele_D_ext);
		rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_EXT_S0,
				 ele_A_ext);
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_EXT_S0,
				 ele_C_ext);
		break;
	}
}

static void rtw8723d_set_iqk_matrix(struct rtw_dev *rtwdev, s8 ofdm_index,
				    u8 rf_path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s32 value32;
	u32 ofdm_swing;

	if (ofdm_index >= RTW_OFDM_SWING_TABLE_SIZE)
		ofdm_index = RTW_OFDM_SWING_TABLE_SIZE - 1;
	else if (ofdm_index < 0)
		ofdm_index = 0;

	ofdm_swing = rtw8723d_ofdm_swing_table[ofdm_index];

	if (dm_info->iqk.done) {
		_rtw8723d_set_iqk_matrix(rtwdev, ofdm_swing, rf_path);
		return;
	}

	switch (rf_path) {
	case RF_PATH_A:
	default:
		rtw_write32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE, ofdm_swing);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
				 0x00);
		value32 = rtw_read32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD);
		value32 &= ~BIT_MASK_OFDM0_EXTS;
		rtw_write32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, value32);
		break;

	case RF_PATH_B:
		/* image S1:c80 to S0:Cd0 and Cd4 */
		rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_S0,
				 OFDM_SWING_A(ofdm_swing));
		rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_B_S0,
				 OFDM_SWING_B(ofdm_swing));
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_S0,
				 OFDM_SWING_C(ofdm_swing));
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_D_S0,
				 OFDM_SWING_D(ofdm_swing));
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_D_EXT_S0, 0x0);
		rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_EXT_S0, 0x0);
		rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_EXT_S0, 0x0);
		break;
	}
}

static void rtw8723d_pwrtrack_set_ofdm_pwr(struct rtw_dev *rtwdev, s8 swing_idx,
					   s8 txagc_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->txagc_remnant_ofdm = txagc_idx;

	rtw8723d_set_iqk_matrix(rtwdev, swing_idx, RF_PATH_A);
	rtw8723d_set_iqk_matrix(rtwdev, swing_idx, RF_PATH_B);
}

static void rtw8723d_pwrtrack_set_cck_pwr(struct rtw_dev *rtwdev, s8 swing_idx,
					  s8 txagc_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->txagc_remnant_cck = txagc_idx;

	rtw_write32_mask(rtwdev, 0xab4, 0x000007FF,
			 rtw8723d_cck_swing_table[swing_idx]);
}

static void rtw8723d_pwrtrack_set(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 limit_ofdm;
	u8 limit_cck = 40;
	s8 final_ofdm_swing_index;
	s8 final_cck_swing_index;

	limit_ofdm = rtw8723d_pwrtrack_get_limit_ofdm(rtwdev);

	final_ofdm_swing_index = RTW_DEF_OFDM_SWING_INDEX +
				 dm_info->delta_power_index[path];
	final_cck_swing_index = RTW_DEF_CCK_SWING_INDEX +
				dm_info->delta_power_index[path];

	if (final_ofdm_swing_index > limit_ofdm)
		rtw8723d_pwrtrack_set_ofdm_pwr(rtwdev, limit_ofdm,
					       final_ofdm_swing_index - limit_ofdm);
	else if (final_ofdm_swing_index < 0)
		rtw8723d_pwrtrack_set_ofdm_pwr(rtwdev, 0,
					       final_ofdm_swing_index);
	else
		rtw8723d_pwrtrack_set_ofdm_pwr(rtwdev, final_ofdm_swing_index, 0);

	if (final_cck_swing_index > limit_cck)
		rtw8723d_pwrtrack_set_cck_pwr(rtwdev, limit_cck,
					      final_cck_swing_index - limit_cck);
	else if (final_cck_swing_index < 0)
		rtw8723d_pwrtrack_set_cck_pwr(rtwdev, 0,
					      final_cck_swing_index);
	else
		rtw8723d_pwrtrack_set_cck_pwr(rtwdev, final_cck_swing_index, 0);

	rtw_phy_set_tx_power_level(rtwdev, hal->current_channel);
}

static void rtw8723d_pwrtrack_set_xtal(struct rtw_dev *rtwdev, u8 therm_path,
				       u8 delta)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	const struct rtw_pwr_track_tbl *tbl = rtwdev->chip->pwr_track_tbl;
	const s8 *pwrtrk_xtal;
	s8 xtal_cap;

	if (dm_info->thermal_avg[therm_path] >
	    rtwdev->efuse.thermal_meter[therm_path])
		pwrtrk_xtal = tbl->pwrtrk_xtal_p;
	else
		pwrtrk_xtal = tbl->pwrtrk_xtal_n;

	xtal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	xtal_cap = clamp_t(s8, xtal_cap + pwrtrk_xtal[delta], 0, 0x3F);
	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
}

static void rtw8723d_phy_pwrtrack(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_swing_table swing_table;
	u8 thermal_value, delta, path;
	bool do_iqk = false;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	if (rtwdev->efuse.thermal_meter[0] == 0xff)
		return;

	thermal_value = rtw_read_rf(rtwdev, RF_PATH_A, RF_T_METER, 0xfc00);

	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, RF_PATH_A);

	do_iqk = rtw_phy_pwrtrack_need_iqk(rtwdev);

	if (do_iqk)
		rtw8723d_lck(rtwdev);

	if (dm_info->pwrtrack_initial_trigger)
		dm_info->pwrtrack_initial_trigger = false;
	else if (!rtw_phy_pwrtrack_thermal_changed(rtwdev, thermal_value,
						   RF_PATH_A))
		goto iqk;

	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	delta = min_t(u8, delta, RTW_PWRTRACK_TBL_SIZE - 1);

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		dm_info->delta_power_index[path] =
			rtw_phy_pwrtrack_get_pwridx(rtwdev, &swing_table, path,
						    RF_PATH_A, delta);
		if (dm_info->delta_power_index[path] ==
				dm_info->delta_power_index_last[path])
			continue;
		else
			dm_info->delta_power_index_last[path] =
				dm_info->delta_power_index[path];
		rtw8723d_pwrtrack_set(rtwdev, path);
	}

	rtw8723d_pwrtrack_set_xtal(rtwdev, RF_PATH_A, delta);

iqk:
	if (do_iqk)
		rtw8723d_phy_calibration(rtwdev);
}

void rtw8723d_pwrtrack_check(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (efuse->power_track_type != 0)
		return;

	if (!dm_info->pwrtrack_trigger) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwrtrack_trigger = true;
		return;
	}

	rtw8723d_phy_pwrtrack(rtwdev);
	dm_info->pwrtrack_trigger = false;
}

static struct rtw_chip_ops rtw8723d_ops = {
	.phy_set_param		= rtw8723d_phy_set_param,
	.mac_init_system_cfg	= rtw8723d_mac_init_system_cfg,
	.read_efuse		= rtw8723d_read_efuse,
	.query_rx_desc		= rtw8723d_query_rx_desc,
	.set_channel		= rtw8723d_set_channel,
	.mac_init		= rtw8723d_mac_init,
	.shutdown		= rtw8723d_shutdown,
	.read_rf		= rtw_phy_read_rf_sipi,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_tx_power_index	= rtw8723d_set_tx_power_index,
	.set_antenna		= NULL,
	.cfg_ldo25		= rtw8723d_cfg_ldo25,
	.efuse_en		= rtw8723d_efuse_en,
	.false_alarm_statistics	= rtw8723d_false_alarm_statistics,
	.phy_calibration	= rtw8723d_phy_calibration,
	.pwrtrack_check		= rtw8723d_pwrtrack_check,
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

static struct rtw_page_table page_table_8723d[] = {
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
};

static struct rtw_rqpn rqpn_table_8723d[] = {
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
};

static struct rtw_prioq_addrs prioq_addrs_8723d = {
	.prio[RTW_DMA_MAPPING_EXTRA] = {
		.rsvd = REG_RQPN_NPQ + 2, .avail = REG_RQPN_NPQ + 3,
	},
	.prio[RTW_DMA_MAPPING_LOW] = {
		.rsvd = REG_RQPN + 1, .avail = REG_FIFOPAGE_CTRL_2 + 1,
	},
	.prio[RTW_DMA_MAPPING_NORMAL] = {
		.rsvd = REG_RQPN_NPQ, .avail = REG_RQPN_NPQ + 1,
	},
	.prio[RTW_DMA_MAPPING_HIGH] = {
		.rsvd = REG_RQPN, .avail = REG_FIFOPAGE_CTRL_2,
	},
	.wsize = false,
};

static struct rtw_intf_phy_para pcie_gen1_param_8723d[] = {
	{0x0008, 0x4a22,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0009, 0x1000,
	 RTW_IP_SEL_PHY,
	 ~(RTW_INTF_PHY_CUT_A | RTW_INTF_PHY_CUT_B),
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static struct rtw_intf_phy_para_table phy_para_table_8723d = {
	.gen1_para	= pcie_gen1_param_8723d,
	.n_gen1_para	= ARRAY_SIZE(pcie_gen1_param_8723d),
};

static struct rtw_hw_reg rtw8723d_dig[] = {
	[0] = { .addr = 0xc50, .mask = 0x7f },
	[1] = { .addr = 0xc50, .mask = 0x7f },
};

static struct rtw_hw_reg rtw8723d_dig_cck[] = {
	[0] = { .addr = 0xa0c, .mask = 0x3f00 },
};

static struct rtw_rf_sipi_addr rtw8723d_rf_sipi_addr[] = {
	[0] = { .hssi_1 = 0x820, .lssi_read    = 0x8a0,
		.hssi_2 = 0x824, .lssi_read_pi = 0x8b8},
	[1] = { .hssi_1 = 0x828, .lssi_read    = 0x8a4,
		.hssi_2 = 0x82c, .lssi_read_pi = 0x8bc},
};

static struct rtw_ltecoex_addr rtw8723d_ltecoex_addr = {
	.ctrl = REG_LTECOEX_CTRL,
	.wdata = REG_LTECOEX_WRITE_DATA,
	.rdata = REG_LTECOEX_READ_DATA,
};

static const struct rtw_rfe_def rtw8723d_rfe_defs[] = {
	[0] = { .phy_pg_tbl = &rtw8723d_bb_pg_tbl,
		.txpwr_lmt_tbl = &rtw8723d_txpwr_lmt_tbl,},
};

static const u8 rtw8723d_pwrtrk_2gb_n[] = {
	0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 4, 4, 5, 5, 5,
	6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 10
};

static const u8 rtw8723d_pwrtrk_2gb_p[] = {
	0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7,
	7, 8, 8, 8, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10
};

static const u8 rtw8723d_pwrtrk_2ga_n[] = {
	0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 4, 4, 5, 5, 5,
	6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 10
};

static const u8 rtw8723d_pwrtrk_2ga_p[] = {
	0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7,
	7, 8, 8, 8, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10
};

static const u8 rtw8723d_pwrtrk_2g_cck_b_n[] = {
	0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 11, 11, 11
};

static const u8 rtw8723d_pwrtrk_2g_cck_b_p[] = {
	0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7,
	7, 8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11
};

static const u8 rtw8723d_pwrtrk_2g_cck_a_n[] = {
	0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 11, 11, 11
};

static const u8 rtw8723d_pwrtrk_2g_cck_a_p[] = {
	0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7,
	7, 8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11
};

static const s8 rtw8723d_pwrtrk_xtal_n[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const s8 rtw8723d_pwrtrk_xtal_p[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, -10, -12, -14, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16, -16
};

static const struct rtw_pwr_track_tbl rtw8723d_rtw_pwr_track_tbl = {
	.pwrtrk_2gb_n = rtw8723d_pwrtrk_2gb_n,
	.pwrtrk_2gb_p = rtw8723d_pwrtrk_2gb_p,
	.pwrtrk_2ga_n = rtw8723d_pwrtrk_2ga_n,
	.pwrtrk_2ga_p = rtw8723d_pwrtrk_2ga_p,
	.pwrtrk_2g_cckb_n = rtw8723d_pwrtrk_2g_cck_b_n,
	.pwrtrk_2g_cckb_p = rtw8723d_pwrtrk_2g_cck_b_p,
	.pwrtrk_2g_ccka_n = rtw8723d_pwrtrk_2g_cck_a_n,
	.pwrtrk_2g_ccka_p = rtw8723d_pwrtrk_2g_cck_a_p,
	.pwrtrk_xtal_p = rtw8723d_pwrtrk_xtal_p,
	.pwrtrk_xtal_n = rtw8723d_pwrtrk_xtal_n,
};

struct rtw_chip_info rtw8723d_hw_spec = {
	.ops = &rtw8723d_ops,
	.id = RTW_CHIP_TYPE_8723D,
	.fw_name = "rtw88/rtw8723d_fw.bin",
	.wlan_cpu = RTW_WCPU_11N,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 96 + 1,
	.txff_size = 32768,
	.rxff_size = 16384,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G,
	.page_size = 128,
	.dig_min = 0x20,
	.ht_supported = true,
	.vht_supported = false,
	.ldpc_cap = false,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xFD,
	.pwr_on_seq = card_enable_flow_8723d,
	.pwr_off_seq = card_disable_flow_8723d,
	.page_table = page_table_8723d,
	.rqpn_table = rqpn_table_8723d,
	.prioq_addrs = &prioq_addrs_8723d,
	.intf_table = &phy_para_table_8723d,
	.dig = rtw8723d_dig,
	.dig_cck = rtw8723d_dig_cck,
	.rf_sipi_addr = {0x840, 0x844},
	.rf_sipi_read_addr = rtw8723d_rf_sipi_addr,
	.rf_phy_nr = 2,
	.ltecoex_addr = &rtw8723d_ltecoex_addr,
	.mac_tbl = &rtw8723d_mac_tbl,
	.agc_tbl = &rtw8723d_agc_tbl,
	.bb_tbl = &rtw8723d_bb_tbl,
	.rf_tbl = {&rtw8723d_rf_a_tbl},
	.rfe_defs = rtw8723d_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8723d_rfe_defs),
	.pwr_track_tbl = &rtw8723d_rtw_pwr_track_tbl,
	.iqk_threshold = 8,
	.wow_supported = false,
};
EXPORT_SYMBOL(rtw8723d_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8723d_fw.bin");
