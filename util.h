/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_UTIL_H__
#define __RTW_UTIL_H__

struct rtw_dev;

#define rtw_iterate_vifs(rtwdev, iterator, data)                               \
	ieee80211_iterate_active_interfaces(rtwdev->hw,                        \
			IEEE80211_IFACE_ITER_NORMAL, iterator, data)
#define rtw_iterate_vifs_atomic(rtwdev, iterator, data)                        \
	ieee80211_iterate_active_interfaces_atomic(rtwdev->hw,                 \
			IEEE80211_IFACE_ITER_NORMAL, iterator, data)
#define rtw_iterate_stas_atomic(rtwdev, iterator, data)                        \
	ieee80211_iterate_stations_atomic(rtwdev->hw, iterator, data)
#define rtw_iterate_keys(rtwdev, vif, iterator, data)			       \
	ieee80211_iter_keys(rtwdev->hw, vif, iterator, data)

static inline u8 *get_hdr_bssid(struct ieee80211_hdr *hdr)
{
	__le16 fc = hdr->frame_control;
	u8 *bssid;

	if (ieee80211_has_tods(fc))
		bssid = hdr->addr1;
	else if (ieee80211_has_fromds(fc))
		bssid = hdr->addr2;
	else
		bssid = hdr->addr3;

	return bssid;
}

static inline s32 bits_to_s32(s32 val, int msb, int lsb)
{
	return (val << (31 - msb)) >> (31 - msb + lsb);
}

static inline s32 q16_to_q8(s32 q16)
{
	return q16 >> 8;
}

static inline s32 q16_to_q9(s32 q16)
{
	return q16 >> 7;
}

#endif
