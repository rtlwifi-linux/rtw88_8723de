// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/acpi.h>
#include "main.h"
#include "debug.h"
#include "phy.h"
#include "sar.h"

#define SAR_WRDS_CHAIN_NUM	2

enum sar_wrds_sbs {
	SAR_WRDS_CH1_14,	/* 2.4G */
	SAR_WRDS_CH36_64,	/* 5.15~5.35G */
	SAR_WRDS_UND1,		/* 5.35~5.47G */
	SAR_WRDS_CH100_144,	/* 5.47~5.725G */
	SAR_WRDS_CH149_165,	/* 5.725~5.95G */
	SAR_WRDS_SB_NUM,
};

struct sar_wrds {
	u8 val[SAR_WRDS_CHAIN_NUM][SAR_WRDS_SB_NUM]; /* Q3 */
};

#define ACPI_WRDS_METHOD        "WRDS"
#define ACPI_WRDS_WIFI          0x07
#define ACPI_WRDS_TABLE_SIZE    (SAR_WRDS_CHAIN_NUM * SAR_WRDS_SB_NUM)

#ifdef CONFIG_ACPI
static bool rtw_sar_get_wrds(struct rtw_dev *rtwdev, union acpi_object *wrds,
			     struct sar_wrds *sar_wrds)
{
	union acpi_object *data_pkg;
	u32 i;
	int path, sb, idx;

	if (wrds->type != ACPI_TYPE_PACKAGE ||
	    wrds->package.count < 2 ||
	    wrds->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    wrds->package.elements[0].integer.value != 0) {
		rtw_warn(rtwdev, "SAR: Unsupported wrds structure\n");
		return false;
	}

	/* loop through all the packages to find the one for WiFi */
	for (i = 1; i < wrds->package.count; i++) {
		union acpi_object *domain;

		data_pkg = &wrds->package.elements[i];

		/* Skip anything that is not a package with the right
		 * amount of elements (i.e. domain_type,
		 * enabled/disabled plus the sar table size.
		 */
		if (data_pkg->type != ACPI_TYPE_PACKAGE ||
		    data_pkg->package.count != ACPI_WRDS_TABLE_SIZE + 2)
			continue;

		domain = &data_pkg->package.elements[0];
		if (domain->type == ACPI_TYPE_INTEGER &&
		    domain->integer.value == ACPI_WRDS_WIFI)
			break;

		data_pkg = NULL;
	}

	if (!data_pkg)
		return false;

	if (data_pkg->package.elements[1].type != ACPI_TYPE_INTEGER)
		return false;

	/* WiFiSarEnable 0: ignore BIOS config; 1: use BIOS config */
	if (data_pkg->package.elements[1].integer.value == 0)
		return false;

	/* read elements[2~11] */
	idx = 2;
	for (path = 0; path < SAR_WRDS_CHAIN_NUM; path++)
		for (sb = 0; sb < SAR_WRDS_SB_NUM; sb++) {
			union acpi_object *entry;

			entry = &data_pkg->package.elements[idx++];
			if (entry->type != ACPI_TYPE_INTEGER ||
			    entry->integer.value > U8_MAX)
				return false;

			sar_wrds->val[path][sb] = entry->integer.value;
		}

	return true;
}

static bool rtw_sar_load_wrds_from_acpi(struct rtw_dev *rtwdev,
					struct sar_wrds *sar_wrds)
{
	struct device *dev = rtwdev->dev;
	acpi_handle root_handle;
	acpi_handle handle;
	acpi_status status;
	struct acpi_buffer wrds = {ACPI_ALLOCATE_BUFFER, NULL};
	s32 ret;

	/* Check device handler */
	root_handle = ACPI_HANDLE(dev);
	if (!root_handle) {
		rtw_warn(rtwdev, "SAR: Could not retireve root port ACPI handle\n");
		return false;
	}

	/* Get method's handler */
	status = acpi_get_handle(root_handle, (acpi_string)ACPI_WRDS_METHOD,
				 &handle);
	if (ACPI_FAILURE(status)) {
		rtw_dbg(rtwdev, RTW_DBG_REGD, "SAR: WRDS method not found\n");
		return false;
	}

	/* Call WRDS with no argument */
	status = acpi_evaluate_object(handle, NULL, NULL, &wrds);
	if (ACPI_FAILURE(status)) {
		rtw_dbg(rtwdev, RTW_DBG_REGD,
			"SAR: WRDS invocation failed (0x%x)\n", status);
		return false;
	}

	/* Process WRDS returned wrapper */
	ret = rtw_sar_get_wrds(rtwdev, wrds.pointer, sar_wrds);
	kfree(wrds.pointer);

	return true;
}

static bool rtw_sar_load_wrds_table(struct rtw_dev *rtwdev)
{
	struct sar_wrds sar_wrds;
	bool ret;
	int path;

	ret = rtw_sar_load_wrds_from_acpi(rtwdev, &sar_wrds);
	if (!ret)
		return false;

	for (path = 0; path < SAR_WRDS_CHAIN_NUM; path++) {
		rtw_phy_set_tx_power_sar(rtwdev, RTW_REGD_WW, path, 1, 14,
					 sar_wrds.val[path][SAR_WRDS_CH1_14]);
		rtw_phy_set_tx_power_sar(rtwdev, RTW_REGD_WW, path, 36, 64,
					 sar_wrds.val[path][SAR_WRDS_CH36_64]);
		rtw_phy_set_tx_power_sar(rtwdev, RTW_REGD_WW, path, 100, 144,
					 sar_wrds.val[path][SAR_WRDS_CH100_144]);
		rtw_phy_set_tx_power_sar(rtwdev, RTW_REGD_WW, path, 149, 165,
					 sar_wrds.val[path][SAR_WRDS_CH149_165]);
	}

	return true;
}
#else
static bool rtw_sar_load_wrds_table(struct rtw_dev *rtwdev)
{
	return false;
}
#endif /* CONFIG_ACPI */

bool rtw_sar_load_table(struct rtw_dev *rtwdev)
{
	return rtw_sar_load_wrds_table(rtwdev);
}
