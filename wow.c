// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "fw.h"
#include "wow.h"
#include "reg.h"
#include "debug.h"
#include "mac.h"
#include "ps.h"

static void rtw_wow_resume_wakeup_reason(struct rtw_dev *rtwdev)
{
	u8 reason;

	reason = rtw_read8(rtwdev, REG_WOWLAN_WAKE_REASON);

	if (reason == RTW_WOW_RSN_RX_DEAUTH)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx deauth\n");
	else if (reason == RTW_WOW_RSN_DISCONNECT)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: AP is off\n");
	else if (reason == RTW_WOW_RSN_RX_MAGIC_PKT)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx magic packet\n");
	else if (reason == RTW_WOW_RSN_RX_GTK_REKEY)
		rtw_dbg(rtwdev, RTW_DBG_WOW, "WOW: Rx gtk rekey\n");
	else
		rtw_warn(rtwdev, "Unknown wakeup reason %x\n", reason);
}

static void rtw_wow_bb_stop(struct rtw_dev *rtwdev, u8 *txpause)
{
	if (!(rtw_read32_mask(rtwdev, REG_BCNQ_INFO, BIT_MGQ_CPU_EMPTY) != 1))
		rtw_warn(rtwdev, "Wrong status of MGQ_CPU empty!\n");

	*txpause = rtw_read8(rtwdev, REG_TXPAUSE);
	rtw_write8(rtwdev, REG_TXPAUSE, 0xff);
	rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
}

static void rtw_wow_bb_start(struct rtw_dev *rtwdev, u8 *txpause)
{
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
	rtw_write8(rtwdev, REG_TXPAUSE, *txpause);
}

static bool rtw_wow_check_fw_status(struct rtw_dev *rtwdev, bool wow_enable)
{
	bool res = false;

	/* wait 100ms for wow firmware to start or stop */
	msleep(100);
	if (wow_enable) {
		if (rtw_read32_mask(rtwdev, REG_MCUTST_II, 0xff000000) == 0)
			res = true;
	} else {
		if (rtw_read32_mask(rtwdev, REG_FE1IMR, BIT_FS_RXDONE) == 0 &&
		    rtw_read32_mask(rtwdev, REG_RXPKT_NUM, BIT_RW_RELEASE) == 0)
			res = true;
	}

	if (!res)
		rtw_err(rtwdev, "failed to check wow status %s\n",
			wow_enable ? "enabled" : "disabled");

	return res;
}

static void rtw_wow_fw_security_type_iter(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta,
					  struct ieee80211_key_conf *key,
					  void *data)
{
	struct rtw_fw_key_type_iter_data *iter_data = data;
	struct rtw_dev *rtwdev = hw->priv;
	u8 hw_key_type;

	if (vif != rtwdev->wow.wow_vif)
		return;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		hw_key_type = RTW_CAM_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		hw_key_type = RTW_CAM_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		hw_key_type = RTW_CAM_TKIP;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		hw_key_type = RTW_CAM_AES;
		key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	default:
		rtw_warn(rtwdev, "Unsupported key type");
		hw_key_type = 0;
		break;
	}

	if (sta)
		iter_data->pairwise_key_type = hw_key_type;
	else
		iter_data->group_key_type = hw_key_type;
}

static void rtw_wow_fw_security_type(struct rtw_dev *rtwdev)
{
	struct rtw_fw_key_type_iter_data data = {};
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;

	data.rtwdev = rtwdev;

	rtw_iterate_keys(rtwdev, wow_vif,
			 rtw_wow_fw_security_type_iter, &data);

	rtw_dbg(rtwdev, RTW_DBG_WOW, "pairwise_key: %d group_key: %d\n",
		data.pairwise_key_type, data.group_key_type);

	rtw_fw_set_aoac_global_info_cmd(rtwdev, data.pairwise_key_type,
					data.group_key_type);
}

static void rtw_wow_fw_enable(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	if (rtw_wow->suspend_mode == RTW_SUSPEND_LINKED) {
		rtw_send_rsvd_page_h2c(rtwdev);
		rtw_wow_fw_security_type(rtwdev);
		rtw_fw_set_disconnect_decision_cmd(rtwdev, true);
		rtw_fw_set_keep_alive_cmd(rtwdev, true);
	}
	rtw_fw_set_wowlan_ctrl_cmd(rtwdev, true);
	rtw_fw_set_remote_wake_ctrl_cmd(rtwdev, true);
	rtw_wow_check_fw_status(rtwdev, true);
}

static void rtw_wow_fw_disable(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	if (rtw_wow->suspend_mode == RTW_SUSPEND_LINKED) {
		rtw_fw_set_disconnect_decision_cmd(rtwdev, false);
		rtw_fw_set_keep_alive_cmd(rtwdev, false);
	}
	rtw_fw_set_wowlan_ctrl_cmd(rtwdev, false);
	rtw_fw_set_remote_wake_ctrl_cmd(rtwdev, false);
	rtw_wow_check_fw_status(rtwdev, false);
}

static void rtw_wow_avoid_reset_mac(struct rtw_dev *rtwdev)
{
	/* When resuming from wowlan mode, some host issue signal
	 * (PCIE: PREST, USB: SE0RST) to device, and lead to reset
	 * mac core. If it happens, the connection to AP will be lost.
	 * Setting REG_RSV_CTRL Register can avoid this process.
	 */
	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
	case RTW_HCI_TYPE_USB:
		rtw_write8(rtwdev, REG_RSV_CTRL, BIT_WLOCK_1C_B6);
		rtw_write8(rtwdev, REG_RSV_CTRL,
			   BIT_WLOCK_1C_B6 | BIT_R_DIS_PRST);
		break;
	default:
		rtw_warn(rtwdev, "Unsupported hci type to disable reset MAC");
		break;
	}
}

static void rtw_wow_fw_media_status_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	struct rtw_fw_media_status_iter_data *iter_data = data;
	struct rtw_dev *rtwdev = iter_data->rtwdev;

	rtw_fw_media_status_report(rtwdev, si->mac_id, iter_data->connect);
}

static void rtw_wow_fw_media_status(struct rtw_dev *rtwdev, bool connect)
{
	struct rtw_fw_media_status_iter_data data;

	data.rtwdev = rtwdev;
	data.connect = connect;

	rtw_iterate_stas_atomic(rtwdev, rtw_wow_fw_media_status_iter, &data);
}

static int rtw_wow_dl_fw_rsvd(struct rtw_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;

	rtw_fw_config_rsvd_page(rtwdev);

	return rtw_fw_download_rsvd_page(rtwdev, wow_vif);
}

static int rtw_wow_swap_fw(struct rtw_dev *rtwdev, bool wow)
{
	struct rtw_fw_state *fw;
	int ret;

	if (wow)
		fw = &rtwdev->wow_fw;
	else
		fw = &rtwdev->fw;

	rtw_hci_setup(rtwdev);

	ret = rtw_download_firmware(rtwdev, fw);
	if (ret) {
		rtw_err(rtwdev, "failed to download %s firmware\n",
			wow ? "wow" : "normal");
		return ret;
	}
	rtw_wow_fw_media_status(rtwdev, true);

	return rtw_wow_dl_fw_rsvd(rtwdev);
}

static void rtw_wow_set_param(struct rtw_dev *rtwdev,
			      struct cfg80211_wowlan *wowlan)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	rtw_wow->any = wowlan->any;
	rtw_wow->disconnect = wowlan->disconnect;
	rtw_wow->magic_pkt = wowlan->magic_pkt;
	rtw_wow->gtk_rekey_failure = wowlan->gtk_rekey_failure;
}

static void rtw_wow_suspend_normal_mode(struct rtw_dev *rtwdev,
					u8 *txpause_status)
{
	rtw_hci_stop(rtwdev);

	/* wait 100ms for wow firmware to stop TX */
	msleep(100);
	rtw_wow_bb_stop(rtwdev, txpause_status);

	rtw_hci_setup(rtwdev);
}

static void rtw_wow_sleep(struct rtw_dev *rtwdev, u8 *txpause_status)
{
	rtw_wow_fw_enable(rtwdev);
	rtw_wow_bb_start(rtwdev, txpause_status);
	rtw_wow_avoid_reset_mac(rtwdev);
}

static int rtw_wow_suspend_start(struct rtw_dev *rtwdev)
{
	int ret = 0;
	u8 txpause_status;

	rtw_wow_suspend_normal_mode(rtwdev, &txpause_status);

	ret = rtw_wow_swap_fw(rtwdev, true);
	if (ret)
		return ret;

	rtw_wow_sleep(rtwdev, &txpause_status);

	return ret;
}

static int rtw_wow_suspend_linked(struct rtw_dev *rtwdev,
				  struct cfg80211_wowlan *wowlan)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw_vif *rtwvif = (struct rtw_vif *)wow_vif->drv_priv;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);

	cancel_delayed_work_sync(&rtwdev->watch_dog_work);
	cancel_delayed_work_sync(&rtwdev->lps_work);
	cancel_work_sync(&rtwdev->c2h_work);

	rtw_leave_lps(rtwdev);

	rtw_wow_set_param(rtwdev, wowlan);
	rtw_wow->suspend_mode = RTW_SUSPEND_LINKED;

	ret = rtw_wow_suspend_start(rtwdev);
	if (ret) {
		rtw_wow->suspend_mode = RTW_SUSPEND_IDLE;
		goto unlock;
	}

	rtw_enter_lps(rtwdev, rtwvif->port);

unlock:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static void rtw_wow_awake(struct rtw_dev *rtwdev, u8 *txpause_status)
{
	rtw_wow_fw_disable(rtwdev);
	rtw_wow_bb_stop(rtwdev, txpause_status);
	rtw_hci_setup(rtwdev);
}

static void rtw_wow_resume_normal_mode(struct rtw_dev *rtwdev,
				       u8 *txpause_status)
{
	rtw_wow_bb_start(rtwdev, txpause_status);
	rtw_hci_start(rtwdev);
	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->watch_dog_work,
				     RTW_WATCH_DOG_DELAY_TIME);
}

static int rtw_wow_resume_start(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	u8 txpause_status;
	int ret = 0;

	rtw_wow_awake(rtwdev, &txpause_status);

	ret = rtw_wow_swap_fw(rtwdev, false);
	if (ret)
		return ret;

	rtw_wow_resume_normal_mode(rtwdev, &txpause_status);

	rtw_wow->wow_vif = NULL;
	rtw_wow->suspend_mode = RTW_SUSPEND_IDLE;

	return ret;
}

static int rtw_wow_resume_linked(struct rtw_dev *rtwdev)
{
	int ret = 0;

	mutex_lock(&rtwdev->mutex);

	rtw_leave_lps(rtwdev);

	rtw_wow_resume_wakeup_reason(rtwdev);

	ret = rtw_wow_resume_start(rtwdev);

	mutex_unlock(&rtwdev->mutex);

	return ret;
}

static void rtw_wow_suspend_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct rtw_dev *rtwdev = data;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;

	if (rtwdev->wow.wow_vif || vif->type != NL80211_IFTYPE_STATION)
		return;

	if (rtwvif->net_type == RTW_NET_MGD_LINKED)
		rtwdev->wow.wow_vif = vif;
}

int rtw_wow_suspend(struct rtw_dev *rtwdev, struct cfg80211_wowlan *wowlan)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;

	rtw_iterate_vifs_atomic(rtwdev, rtw_wow_suspend_iter, rtwdev);
	if (rtw_wow->wow_vif)
		return rtw_wow_suspend_linked(rtwdev, wowlan);

	return -EPERM;
}

int rtw_wow_resume(struct rtw_dev *rtwdev)
{
	struct rtw_wow_param *rtw_wow = &rtwdev->wow;
	int ret = 0;

	if (rtw_wow->suspend_mode == RTW_SUSPEND_LINKED)
		ret = rtw_wow_resume_linked(rtwdev);

	return ret;
}
