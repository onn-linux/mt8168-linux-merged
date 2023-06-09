/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_switch_charging.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>

#include <mt-plat/mtk_boot.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/battery_metrics.h>
/* #include <musb_core.h> */ /* FIXME */
#include "mtk_charger_intf.h"
#include "mtk_switch_charging.h"
#include "mtk_battery_internal.h"

static int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

static void _disable_all_charging(struct charger_manager *info)
{
	charger_dev_enable(info->chg1_dev, false);

	if (mtk_pe20_get_is_enable(info)) {
		mtk_pe20_set_is_enable(info, false);
		if (mtk_pe20_get_is_connect(info))
			mtk_pe20_reset_ta_vchr(info);
	}

	if (mtk_pe_get_is_enable(info)) {
		mtk_pe_set_is_enable(info, false);
		if (mtk_pe_get_is_connect(info))
			mtk_pe_reset_ta_vchr(info);
	}

	if (mtk_pe40_get_is_enable(info)) {
		if (mtk_pe40_get_is_connect(info))
			mtk_pe40_end(info, 3, true);
	}
}

#define DETECTION_MIVR_UV 4400000
#define ADAPTER_INPUT_CURRENT_5W_UA 1000000
#define ADAPTER_INPUT_CURRENT_9W_UA 1800000
#define ADAPTER_IBAT_CURRENT_5W_UA  1200000
static int adapter_power_detection_by_ocp(struct charger_manager *info)
{
	struct charger_data *pdata = &info->chg1_data;
	int bak_cv_uv = 0, bak_iusb_ua = 0, bak_ichg_ua = 0, bak_mivr = 0;
	int cv_uv = info->data.battery_cv;
	int iusb_ua = info->power_detection.aicl_trigger_iusb;
	int ichg_ua = info->power_detection.aicl_trigger_ichg;

	/* Backup IUSB/ICHG/CV setting */
	charger_dev_get_constant_voltage(info->chg1_dev, &bak_cv_uv);
	charger_dev_get_input_current(info->chg1_dev, &bak_iusb_ua);
	charger_dev_get_charging_current(info->chg1_dev, &bak_ichg_ua);
	charger_dev_get_mivr(info->chg1_dev, &bak_mivr);
	pr_info("%s: backup IUSB[%d] ICHG[%d] CV[%d] MIVR[%d]\n",
			__func__, _uA_to_mA(bak_iusb_ua),
			_uA_to_mA(bak_ichg_ua), bak_cv_uv, bak_mivr);

	/* set higher setting to draw more power */
	pr_info("%s: set IUSB[%d] ICHG[%d] CV[%d] MIVR[%d] for detection\n",
			__func__, _uA_to_mA(iusb_ua), _uA_to_mA(ichg_ua),
			cv_uv, DETECTION_MIVR_UV);
	charger_dev_set_mivr(info->chg1_dev, DETECTION_MIVR_UV);
	charger_dev_set_constant_voltage(info->chg1_dev, cv_uv);
	charger_dev_set_input_current(info->chg1_dev, iusb_ua);
	charger_dev_set_charging_current(info->chg1_dev, ichg_ua);
	charger_dev_dump_registers(info->chg1_dev);

	/* Run AICL */
	msleep(50);
	charger_dev_run_aicl(info->chg1_dev,
			&pdata->input_current_limit_by_aicl);
	pr_info("%s: aicl result: %d mA\n", __func__,
			_uA_to_mA(pdata->input_current_limit_by_aicl));

	/* Restore IUB/ICHG/CV setting */
	pr_info("%s: restore IUSB[%d] ICHG[%d] CV[%d] MIVR[%d]\n",
			__func__, _uA_to_mA(bak_iusb_ua),
			_uA_to_mA(bak_ichg_ua), bak_cv_uv, bak_mivr);
	charger_dev_set_mivr(info->chg1_dev, bak_mivr);
	charger_dev_set_constant_voltage(info->chg1_dev, bak_cv_uv);
	charger_dev_set_input_current(info->chg1_dev, bak_iusb_ua);
	charger_dev_set_charging_current(info->chg1_dev, bak_ichg_ua);
	charger_dev_dump_registers(info->chg1_dev);

	return pdata->input_current_limit_by_aicl;
}

static int adapter_power_detection(struct charger_manager *info)
{
	struct charger_data *pdata = &info->chg1_data;
	struct power_detection_data *det = &info->power_detection;
	int chr_type = info->chr_type;
	int aicl_ua = 0, rp_curr_ma;
	static const char * const category_text[] = {
		"5W", "7.5W", "9W", "12W", "15W"
	};

	if (!det->en)
		return 0;

	if (det->iusb_ua)
		goto skip;

	/* Step 1: Determine Type-C adapter by Rp */
	rp_curr_ma = tcpm_inquire_typec_remote_rp_curr(info->tcpc);
	if (rp_curr_ma == 3000) {
		pdata->input_current_limit = 3000000;
		det->iusb_ua = 3000000;
		det->type = ADAPTER_15W;
		goto done;
	} else if (rp_curr_ma == 1500) {
		pdata->input_current_limit = 1500000;
		det->iusb_ua = 1500000;
		det->type = ADAPTER_7_5W;
		goto done;
	}

	if (chr_type != STANDARD_CHARGER)
		return 0;

	/* Step 2: Run AICL for OCP detection on A2C adapter */
	aicl_ua = adapter_power_detection_by_ocp(info);
	if (aicl_ua < 0) {
		pdata->input_current_limit = det->adapter_12w_iusb_lim;
		det->iusb_ua = det->adapter_12w_iusb_lim;
		det->type = ADAPTER_12W;
		pr_info("%s: CV stage or 15W adapter, keep 12W as default\n",
			__func__);
		goto done;
	}

	/* Step 3: Determine adapter power categroy for 5W/9W/12W */
	if (aicl_ua > det->adapter_12w_aicl_min) {
		pdata->input_current_limit = det->adapter_12w_iusb_lim;
		det->iusb_ua = det->adapter_12w_iusb_lim;
		det->type = ADAPTER_12W;
	} else if (aicl_ua > det->adapter_9w_aicl_min) {
		pdata->input_current_limit = det->adapter_9w_iusb_lim;
		det->iusb_ua = det->adapter_9w_iusb_lim;
		det->type = ADAPTER_9W;
	} else {
		pdata->input_current_limit = det->adapter_5w_iusb_lim;
		det->iusb_ua = det->adapter_5w_iusb_lim;
		det->type = ADAPTER_5W;
	}

done:
	bat_metrics_adapter_power(det->type, _uA_to_mA(aicl_ua));
	pr_info("%s: detect %s adapter\n", __func__, category_text[det->type]);
	return 0;

skip:
	if (pdata->thermal_input_current_limit != -1) {
		pr_info("%s: use thermal_input_current_limit, ignore\n",
			__func__);
	} else {
		pdata->input_current_limit = det->iusb_ua;
		pr_info("%s: alread finish: %d mA, skip\n",
			__func__, _uA_to_mA(pdata->input_current_limit));
	}

	return 0;
}

#define CHECK_INVILID_CHARGER_IUSB 300000
#define CHECK_USB_POWER_LIMIT_STATE_PHASE 200
#define CHECK_INVILID_CHARGER_CHG_CURRENT 1000000
#define CHECK_INVILID_CHARGER_MIVR 4500000
static void check_invalid_charger(struct charger_manager *info)
{
	bool vdpm = false;
	bool idpm = false;
	int vbus_uv;
	int ret;
	int bak_cur, bak_mivr, bak_iusb;

	if (info->invalid_charger_det_done)
		return;

	charger_dev_get_charging_current(info->chg1_dev, &bak_cur);
	charger_dev_get_mivr(info->chg1_dev, &bak_mivr);
	charger_dev_get_input_current(info->chg1_dev, &bak_iusb);
	pr_info("%s: bak_cur %d, bak_mivr %d, bak_iusb %d\n",
		__func__, bak_cur, bak_mivr, bak_iusb);

	charger_dev_set_mivr(info->chg1_dev, CHECK_INVILID_CHARGER_MIVR);
	charger_dev_set_input_current(info->chg1_dev, CHECK_INVILID_CHARGER_IUSB);
	charger_dev_set_charging_current(info->chg1_dev,
		CHECK_INVILID_CHARGER_CHG_CURRENT);
	msleep(CHECK_USB_POWER_LIMIT_STATE_PHASE);
	ret = charger_dev_get_indpm_state(info->chg1_dev, &vdpm, &idpm);
	vbus_uv = battery_get_vbus() * 1000;

	charger_dev_dump_registers(info->chg1_dev);
	pr_info("%s: aicl %d, vbus %d, vdpm %d, idpm %d\n",
		__func__, CHECK_INVILID_CHARGER_IUSB, vbus_uv, vdpm, idpm);

	if (ret != -ENOTSUPP && vdpm) {
		pr_err("%s: invalid charger for vdpm set\n", __func__);
		switch_set_state(&info->invalid_charger, 1);
	}
	if (vbus_uv < CHECK_INVILID_CHARGER_MIVR) {
		pr_err("%s: invalid charger for vbus lower than %d\n",
			__func__, info->sw_aicl.aicl_vbus_valid);
		switch_set_state(&info->invalid_charger, 1);
	}
	if (switch_get_state(&info->invalid_charger))
		bat_metrics_invalid_charger();

	info->invalid_charger_det_done = true;

	charger_dev_set_charging_current(info->chg1_dev, bak_cur);
	charger_dev_set_mivr(info->chg1_dev, bak_mivr);
	charger_dev_set_input_current(info->chg1_dev, bak_iusb);
}

#if defined(CONFIG_SW_AICL_SUPPORT)
static bool aicl_check_vbus_valid(struct charger_manager *info)
{
	int vbus_mv = battery_get_vbus();
	bool is_valid = true;

	if (vbus_mv < info->sw_aicl.aicl_vbus_valid) {
		is_valid = false;
		pr_info("%s: VBUS drop below %d\n", __func__,
			info->sw_aicl.aicl_vbus_valid);
	}

	return is_valid;
}

static int ap15_charger_detection(struct charger_manager *info)
{
	int iusb = 0;
	int current_max = 0;

	if (!info->sw_aicl.aicl_enable)
		return false;

	if ((info->chr_type == CHARGING_HOST) ||
		(info->chr_type == STANDARD_HOST))
		return false;

	if (info->sw_aicl.aicl_done)
		return true;

	iusb = ADAPTER_INPUT_CURRENT_5W_UA;
	pr_info("%s iusb:%d,%d \n", __func__, iusb,
		info->sw_aicl.aicl_charging_current_max);
	current_max = info->sw_aicl.aicl_charging_current_max;

	charger_dev_set_charging_current(info->chg1_dev, current_max);
	while (iusb < info->sw_aicl.aicl_input_current_max) {
		iusb += info->sw_aicl.aicl_step_current;
		charger_dev_set_input_current(info->chg1_dev, iusb);
		pr_info("%s: aicl set %d\n", __func__, iusb);
		msleep(info->sw_aicl.aicl_vbus_state_phase);
		if (!aicl_check_vbus_valid(info)) {
			iusb -= info->sw_aicl.aicl_step_current;
			charger_dev_set_input_current(info->chg1_dev,
					iusb);
			pr_info("%s: aicl set back: %d\n", __func__, iusb);
			break;
		}
		msleep(info->sw_aicl.aicl_step_interval);
	}
	info->sw_aicl.aicl_done = true;
	info->sw_aicl.aicl_result = iusb;

	if (iusb >= info->sw_aicl.aicl_input_current_min) {
		info->sw_aicl.iusb_ua = ADAPTER_INPUT_CURRENT_9W_UA;
		info->sw_aicl.ibat_ua = current_max;
		bat_metrics_adapter_power(ADAPTER_9W, _uA_to_mA(iusb));
		pr_info("%s: AP15(9W) adapter detected, update setting %d %d\n",
			__func__, info->sw_aicl.iusb_ua, info->sw_aicl.ibat_ua);
	} else {
		info->sw_aicl.iusb_ua = ADAPTER_INPUT_CURRENT_5W_UA;
		info->sw_aicl.ibat_ua = ADAPTER_IBAT_CURRENT_5W_UA;
		bat_metrics_adapter_power(ADAPTER_5W, _uA_to_mA(iusb));
		pr_info("%s: aicl: AP15(9W) adapter not detected =%d,=%d\n",
			__func__, info->sw_aicl.iusb_ua,
				info->sw_aicl.ibat_ua);
	}
	return 0;
}
#endif

static void swchg_select_charging_current_limit(struct charger_manager *info)
{
	struct charger_data *pdata;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret = 0, iusb_ma = 0;
	bool wpc_online = false;

	wireless_charger_dev_get_online(get_charger_by_name("wireless_chg"), &wpc_online);
	pdata = &info->chg1_data;
	mutex_lock(&swchgalg->ichg_aicr_access_mutex);

	check_invalid_charger(info);

	/* AICL */
	if (!mtk_pe20_get_is_connect(info) && !mtk_pe_get_is_connect(info)
	    && !mtk_is_TA_support_pd_pps(info) && !wpc_online)
		charger_dev_run_aicl(info->chg1_dev,
				&pdata->input_current_limit_by_aicl);

	if (pdata->force_input_current_limit > 0) {
		pdata->input_current_limit = pdata->force_input_current_limit;
		if (pdata->force_charging_current > 0)
			pdata->charging_current_limit =
				pdata->force_charging_current;
		else
			pdata->charging_current_limit =
				info->data.ac_charger_current;
		goto done;
	}

	if (pdata->force_charging_current > 0) {

		pdata->charging_current_limit = pdata->force_charging_current;
		if (pdata->force_charging_current <= 450000) {
			pdata->input_current_limit = 500000;
		} else {
			pdata->input_current_limit =
					info->data.ac_charger_input_current;
		}
		goto done;
	}

	if (info->usb_unlimited) {
		pdata->input_current_limit =
					info->data.ac_charger_input_current;
		pdata->charging_current_limit =
					info->data.ac_charger_current;
		goto done;
	}

	if ((get_boot_mode() == META_BOOT) ||
	    (get_boot_mode() == ADVMETA_BOOT)) {
		pdata->input_current_limit = 200000; /* 200mA */
		goto done;
	}

	if (info->atm_enabled == true && (info->chr_type == STANDARD_HOST ||
	    info->chr_type == CHARGING_HOST)) {
		pdata->input_current_limit = 100000; /* 100mA */
		goto done;
	}

	if (mtk_is_TA_support_pd_pps(info)) {
		pdata->input_current_limit =
			info->data.pe40_single_charger_input_current;
		pdata->charging_current_limit =
			info->data.pe40_single_charger_current;
	} else if (is_typec_adapter(info)) {
		if (tcpm_inquire_typec_remote_rp_curr(info->tcpc) == 3000) {
			pdata->input_current_limit = 3000000;
			pdata->charging_current_limit =
				info->data.ac_charger_current;
		} else if (tcpm_inquire_typec_remote_rp_curr(info->tcpc)
			   == 1500) {
			pdata->input_current_limit = 1500000;
			pdata->charging_current_limit =
				info->data.ac_charger_current;
		} else {
			chr_err("type-C: inquire rp error\n");
			pdata->input_current_limit = 500000;
			pdata->charging_current_limit = 500000;
		}

		chr_err("type-C:%d current:%d\n",
			info->pd_type,
			tcpm_inquire_typec_remote_rp_curr(info->tcpc));
	} else if (mtk_pdc_check_charger(info) == true) {
		int vbus = 0, cur = 0, idx = 0;

		mtk_pdc_get_setting(info, &vbus, &cur, &idx);
		if (idx != -1) {
			pdata->input_current_limit = cur * 1000;
			pdata->charging_current_limit =
				info->data.pd_charger_current;
			mtk_pdc_setup(info, idx);
		} else {
			pdata->input_current_limit =
				info->data.usb_charger_current_configured;
			pdata->charging_current_limit =
				info->data.usb_charger_current_configured;
		}
		chr_err("[%s]vbus:%d input_cur:%d idx:%d current:%d\n",
			__func__, vbus, cur, idx,
			info->data.pd_charger_current);

	} else if (info->chr_type == STANDARD_HOST) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)) {
			if (info->usb_state == USB_SUSPEND)
				pdata->input_current_limit =
					info->data.usb_charger_current_suspend;
			else if (info->usb_state == USB_UNCONFIGURED)
				pdata->input_current_limit =
				info->data.usb_charger_current_unconfigured;
			else if (info->usb_state == USB_CONFIGURED)
				pdata->input_current_limit =
				info->data.usb_charger_current_configured;
			else
				pdata->input_current_limit =
				info->data.usb_charger_current_unconfigured;

			pdata->charging_current_limit =
					pdata->input_current_limit;
		} else {
			pdata->input_current_limit =
					info->data.usb_charger_current;
			/* it can be larger */
			pdata->charging_current_limit =
					info->data.usb_charger_current;
		}
	} else if (info->chr_type == NONSTANDARD_CHARGER) {
		pdata->input_current_limit =
				info->data.non_std_ac_charger_current;
		pdata->charging_current_limit =
				info->data.non_std_ac_charger_current;
	} else if (info->chr_type == STANDARD_CHARGER) {
		pdata->input_current_limit =
				info->data.ac_charger_input_current;
		pdata->charging_current_limit =
				info->data.ac_charger_current;
		mtk_pe20_set_charging_current(info,
					&pdata->charging_current_limit,
					&pdata->input_current_limit);
		mtk_pe_set_charging_current(info,
					&pdata->charging_current_limit,
					&pdata->input_current_limit);
	} else if (info->chr_type == CHARGING_HOST) {
		pdata->input_current_limit =
				info->data.charging_host_charger_current;
		pdata->charging_current_limit =
				info->data.charging_host_charger_current;
	} else if (info->chr_type == APPLE_1_0A_CHARGER) {
		pdata->input_current_limit =
				info->data.apple_1_0a_charger_current;
		pdata->charging_current_limit =
				info->data.apple_1_0a_charger_current;
	} else if (info->chr_type == APPLE_2_1A_CHARGER) {
		pdata->input_current_limit =
				info->data.apple_2_1a_charger_current;
		pdata->charging_current_limit =
				info->data.apple_2_1a_charger_current;
	} else if (info->chr_type == WIRELESS_5W_CHARGER) {
		pdata->input_current_limit =
				info->data.wireless_5w_charger_input_current;
		pdata->charging_current_limit =
				info->data.wireless_5w_charger_current;

		if (pdata->thermal_input_power_limit != -1) {
			iusb_ma = pdata->thermal_input_power_limit * 1000 / 5;
			if (iusb_ma < pdata->input_current_limit)
				pdata->input_current_limit = iusb_ma;
		}
	} else if (info->chr_type == WIRELESS_10W_CHARGER) {
		pdata->input_current_limit =
				info->data.wireless_10w_charger_input_current;
		pdata->charging_current_limit =
				info->data.wireless_10w_charger_current;

		if (pdata->thermal_input_power_limit != -1) {
			iusb_ma = pdata->thermal_input_power_limit * 1000 / 9;
			if (iusb_ma < pdata->input_current_limit)
				pdata->input_current_limit = iusb_ma;
		}
	} else if (info->chr_type == WIRELESS_DEFAULT_CHARGER) {
		pdata->input_current_limit =
				info->data.wireless_default_charger_input_current;
		pdata->charging_current_limit =
				info->data.wireless_default_charger_current;

		if (pdata->thermal_input_power_limit != -1) {
			iusb_ma = pdata->thermal_input_power_limit * 1000 / 5;
			if (iusb_ma < pdata->input_current_limit)
				pdata->input_current_limit = iusb_ma;
		}
	}
#if defined(CONFIG_SW_AICL_SUPPORT)
	ap15_charger_detection(info);
	if (info->sw_aicl.aicl_done && info->sw_aicl.iusb_ua >
		pdata->input_current_limit) {
			pdata->charging_current_limit = info->sw_aicl.ibat_ua;
			pdata->input_current_limit = info->sw_aicl.iusb_ua;
	}
#endif
	if (info->enable_sw_jeita) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)
		    && info->chr_type == STANDARD_HOST) {
			pr_debug("USBIF & STAND_HOST skip current check\n");
		} else {
			if (info->sw_jeita.sm == TEMP_T0_TO_T1) {
				if (info->data.temp_t0_to_t1_charger_current != -1) {
					if (info->data.temp_t0_to_t1_charger_current <
						pdata->charging_current_limit)
						pdata->charging_current_limit =
							info->data.temp_t0_to_t1_charger_current;
				} else {
					pr_debug("not set current limit for TEMP_T0_TO_T1\n");
				}
			}

			if (info->sw_jeita.sm == TEMP_T3_TO_T4) {
				if (info->data.temp_t3_to_t4_charger_current != -1) {
					if (info->data.temp_t3_to_t4_charger_current <
						pdata->charging_current_limit)
						pdata->charging_current_limit =
							info->data.temp_t3_to_t4_charger_current;
				} else {
					pr_debug("not set current limit for TEMP_T3_TO_T4\n");
				}
			}
		}
	}

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
	}

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
	}

	if (mtk_pe40_get_is_connect(info)) {
		if (info->pe4.pe4_input_current_limit != -1 &&
		    info->pe4.pe4_input_current_limit <
		    pdata->input_current_limit)
			pdata->input_current_limit =
				info->pe4.pe4_input_current_limit;

		info->pe4.input_current_limit = pdata->input_current_limit;

		if (info->pe4.pe4_input_current_limit_setting != -1 &&
		    info->pe4.pe4_input_current_limit_setting <
		    pdata->input_current_limit)
			pdata->input_current_limit =
				info->pe4.pe4_input_current_limit_setting;
	}

	/* Apply selected current setting then do adapter power detection */
#if defined(CONFIG_SW_AICL_SUPPORT)
	if (!info->sw_aicl.aicl_enable) {
#endif
		charger_dev_set_input_current(info->chg1_dev,
			pdata->input_current_limit);
		charger_dev_set_charging_current(info->chg1_dev,
			pdata->charging_current_limit);
		adapter_power_detection(info);
#if defined(CONFIG_SW_AICL_SUPPORT)
	}
#endif
	if (pdata->input_current_limit_by_aicl != -1 &&
	    !mtk_pe20_get_is_connect(info) && !mtk_pe_get_is_connect(info) &&
	    !mtk_is_TA_support_pd_pps(info)) {
		if (info->vbus_recovery_from_uvlo) {
			info->vbus_recovery_from_uvlo = false;
			chr_err("%s: vbus recovery from UVLO, ignore aicl\n", __func__);
		} else {
			if (pdata->input_current_limit_by_aicl <
				pdata->input_current_limit)
				pdata->input_current_limit =
				pdata->input_current_limit_by_aicl;
		}
	}
done:
	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -ENOTSUPP && pdata->charging_current_limit < ichg1_min)
		pdata->charging_current_limit = 0;

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -ENOTSUPP && pdata->input_current_limit < aicr1_min)
		pdata->input_current_limit = 0;

	chr_err("force:%d thermal:%d,%d %d(mW) pe4:%d,%d,%d setting:%d %d type:%d usb_unlimited:%d usbif:%d usbsm:%d aicl:%d atm:%d\n",
		_uA_to_mA(pdata->force_charging_current),
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		pdata->thermal_input_power_limit,
		_uA_to_mA(info->pe4.pe4_input_current_limit),
		_uA_to_mA(info->pe4.pe4_input_current_limit_setting),
		_uA_to_mA(info->pe4.input_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		info->chr_type, info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled);

	charger_dev_set_input_current(info->chg1_dev,
					pdata->input_current_limit);
	charger_dev_set_charging_current(info->chg1_dev,
					pdata->charging_current_limit);

	/* If AICR < 300mA, stop PE+/PE+20 */
	if (pdata->input_current_limit < 300000) {
		if (mtk_pe20_get_is_enable(info)) {
			mtk_pe20_set_is_enable(info, false);
			if (mtk_pe20_get_is_connect(info))
				mtk_pe20_reset_ta_vchr(info);
		}

		if (mtk_pe_get_is_enable(info)) {
			mtk_pe_set_is_enable(info, false);
			if (mtk_pe_get_is_connect(info))
				mtk_pe_reset_ta_vchr(info);
		}
	}

	/*
	 * If thermal current limit is larger than charging IC's minimum
	 * current setting, enable the charger immediately
	 */
	if (pdata->input_current_limit > aicr1_min &&
	    pdata->charging_current_limit > ichg1_min && info->can_charging)
		charger_dev_enable(info->chg1_dev, true);
	mutex_unlock(&swchgalg->ichg_aicr_access_mutex);
}

static void swchg_select_cv(struct charger_manager *info)
{
	u32 constant_voltage;

	if (info->custom_charging_cv != -1) {
		if (info->enable_sw_jeita && info->sw_jeita.cv != 0) {
			if (info->custom_charging_cv > info->sw_jeita.cv)
				constant_voltage = info->sw_jeita.cv;
			else
				constant_voltage = info->custom_charging_cv;
		} else {
			constant_voltage = info->custom_charging_cv;
		}
		chr_err("%s: top_off_mode CV:%duV", __func__,
			constant_voltage);
		charger_dev_set_constant_voltage(info->chg1_dev, constant_voltage);
		return;
	}

	if (info->enable_sw_jeita) {
		if (info->sw_jeita.cv != 0) {
			charger_dev_set_constant_voltage(info->chg1_dev,
				 info->sw_jeita.cv);
			return;
		}
	}

	/* dynamic cv*/
	constant_voltage = mtk_get_battery_cv(info);

	mtk_get_dynamic_cv(info, &constant_voltage);

	charger_dev_set_constant_voltage(info->chg1_dev, constant_voltage);
}

static void swchg_turn_on_charging(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	bool charging_enable = true;

	if (swchgalg->state == CHR_ERROR) {
		charging_enable = false;
		chr_err("[charger]Charger Error, turn OFF charging !\n");
	} else if ((get_boot_mode() == META_BOOT) ||
			((get_boot_mode() == ADVMETA_BOOT))) {
		charging_enable = false;
		info->chg1_data.input_current_limit = 200000; /* 200mA */
		charger_dev_set_input_current(info->chg1_dev,
					info->chg1_data.input_current_limit);
		chr_err("In meta mode, disable charging and set input current limit to 200mA\n");
	} else {
		mtk_pe20_start_algorithm(info);
		mtk_pe_start_algorithm(info);

		swchg_select_charging_current_limit(info);
		if (info->chg1_data.input_current_limit == 0
		    || info->chg1_data.charging_current_limit == 0) {
			charging_enable = false;
			chr_err("[charger]charging current is set 0mA, turn off charging !\n");
		} else {
			swchg_select_cv(info);
		}
	}

	charger_dev_enable(info->chg1_dev, charging_enable);
}

static int mtk_switch_charging_plug_in(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	/* Keep charging state as CHR_BATFULL. */
	if ((info->enable_bat_eoc_protect && info->bat_eoc_protect)
	    || info->dpm_disable_charging) {
		chr_err("%s: Keep charging state full\n", __func__);
		swchgalg->state = CHR_BATFULL;
		info->polling_interval = CHARGING_FULL_INTERVAL;
		battery_main.BAT_STATUS = POWER_SUPPLY_STATUS_CHARGING;
		battery_update(&battery_main);
		charger_dev_do_event(info->chg1_dev, EVENT_EOC, 0);
		return  0;
	}
	swchgalg->state = CHR_CC;
	info->polling_interval = CHARGING_INTERVAL;
	swchgalg->disable_charging = false;
	get_monotonic_boottime(&swchgalg->charging_begin_time);
	charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);

	return 0;
}

static int mtk_switch_charging_plug_out(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	swchgalg->total_charging_time = 0;
	info->power_detection.iusb_ua = 0;

	mtk_pe20_set_is_cable_out_occur(info, true);
	mtk_pe_set_is_cable_out_occur(info, true);
	mtk_pdc_plugout(info);
	mtk_pe40_plugout_reset(info);
	charger_manager_notifier(info, CHARGER_NOTIFY_STOP_CHARGING);
#if defined(CONFIG_SW_AICL_SUPPORT)
	info->sw_aicl.aicl_done = false;
	info->sw_aicl.aicl_result = -1;
	info->sw_aicl.iusb_ua = -1;
	info->sw_aicl.ibat_ua = -1;
#endif
	return 0;
}

static int mtk_switch_charging_do_charging(struct charger_manager *info,
						bool en)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	chr_err("%s: en:%d %s\n", __func__, en, info->algorithm_name);
	if (en) {
		swchgalg->disable_charging = false;
		swchgalg->state = CHR_CC;
		get_monotonic_boottime(&swchgalg->charging_begin_time);
		charger_manager_notifier(info, CHARGER_NOTIFY_NORMAL);
		mtk_pe40_set_is_enable(info, en);
	} else {
		/* disable charging might change state, so call it first */
		_disable_all_charging(info);
		swchgalg->disable_charging = true;
		/* Force state machine to CHR_BATFULL if charging is
		 * disabled by battery EOC protection or DPM mode.
		 */
		if (info->bat_eoc_protect == false && info->dpm_disable_charging == false) {
			swchgalg->state = CHR_ERROR;
			charger_manager_notifier(info, CHARGER_NOTIFY_ERROR);
		} else {
			swchgalg->state = CHR_BATFULL;
			charger_dev_do_event(info->chg1_dev, EVENT_EOC, 0);
			charger_dev_enable_ir_comp(info->chg1_dev, false);
		}
	}

	return 0;
}

static int mtk_switch_chr_pe40_init(struct charger_manager *info)
{
	swchg_turn_on_charging(info);
	return mtk_pe40_init_state(info);
}

static int mtk_switch_chr_pe40_cc(struct charger_manager *info)
{
	swchg_turn_on_charging(info);
	return mtk_pe40_cc_state(info);
}

/* return false if total charging time exceeds max_charging_time */
static bool mtk_switch_check_charging_time(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct timespec time_now;

	if (info->enable_sw_safety_timer) {
		get_monotonic_boottime(&time_now);
		chr_debug("%s: begin: %ld, now: %ld\n", __func__,
			swchgalg->charging_begin_time.tv_sec, time_now.tv_sec);

		if (swchgalg->total_charging_time >=
		    info->data.max_charging_time) {
			chr_err("%s: SW safety timeout: %d sec > %d sec\n",
				__func__, swchgalg->total_charging_time,
				info->data.max_charging_time);
			charger_dev_notify(info->chg1_dev,
					CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT);
			return false;
		}
	}

	return true;
}

static int mtk_switch_chr_cc(struct charger_manager *info)
{
	bool chg_done = false;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	struct timespec time_now, charging_time;

	/* check bif */
	if (IS_ENABLED(CONFIG_MTK_BIF_SUPPORT)) {
		if (pmic_is_bif_exist() != 1) {
			chr_err("CONFIG_MTK_BIF_SUPPORT but no bif , stop charging\n");
			swchgalg->state = CHR_ERROR;
			charger_manager_notifier(info, CHARGER_NOTIFY_ERROR);
		}
	}

	get_monotonic_boottime(&time_now);
	charging_time = timespec_sub(time_now, swchgalg->charging_begin_time);

	swchgalg->total_charging_time = charging_time.tv_sec;

	if (mtk_pe40_is_ready(info)) {
		chr_err("enter PE4.0!\n");
		swchgalg->state = CHR_PE40_INIT;
		info->pe4.is_connect = true;
		return 1;
	}

	swchg_turn_on_charging(info);

	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (chg_done) {
		swchgalg->state = CHR_BATFULL;
		charger_dev_do_event(info->chg1_dev, EVENT_EOC, 0);
		charger_dev_enable_ir_comp(info->chg1_dev, false);
		chr_err("battery full!\n");
	}

	/* If it is not disabled by throttling,
	 * enable PE+/PE+20, if it is disabled
	 */
	if (info->chg1_data.thermal_input_current_limit != -1 &&
		info->chg1_data.thermal_input_current_limit < 300)
		return 0;

	if (!mtk_pe20_get_is_enable(info)) {
		mtk_pe20_set_is_enable(info, true);
		mtk_pe20_set_to_check_chr_type(info, true);
	}

	if (!mtk_pe_get_is_enable(info)) {
		mtk_pe_set_is_enable(info, true);
		mtk_pe_set_to_check_chr_type(info, true);
	}
	return 0;
}

int mtk_switch_chr_err(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	if (info->enable_sw_jeita) {
		if ((info->sw_jeita.sm == TEMP_BELOW_T0) ||
			(info->sw_jeita.sm == TEMP_ABOVE_T4))
			info->sw_jeita.error_recovery_flag = false;

		if ((info->sw_jeita.error_recovery_flag == false) &&
			(info->sw_jeita.sm != TEMP_BELOW_T0) &&
			(info->sw_jeita.sm != TEMP_ABOVE_T4)) {
			info->sw_jeita.error_recovery_flag = true;
			swchgalg->state = CHR_CC;
			get_monotonic_boottime(&swchgalg->charging_begin_time);
		}
	}

	swchgalg->total_charging_time = 0;

	_disable_all_charging(info);
	return 0;
}

int mtk_switch_chr_full(struct charger_manager *info)
{
	bool chg_done = false;
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;

	swchgalg->total_charging_time = 0;

	/* turn off LED */

	/*
	 * If CV is set to lower value by JEITA,
	 * Reset CV to normal value if temperture is in normal zone
	 */
	swchg_select_cv(info);
	info->polling_interval = CHARGING_FULL_INTERVAL;

	/* Keep charging state at CHR_BATFULL after triggering
	 * battery EOC protection.
	 */
	if (info->bat_eoc_protect || info->dpm_disable_charging)
		return 0;

	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	if (!chg_done) {
		swchgalg->state = CHR_CC;
		charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
		mtk_pe20_set_to_check_chr_type(info, true);
		mtk_pe_set_to_check_chr_type(info, true);
		mtk_pe40_set_is_enable(info, true);
		info->enable_dynamic_cv = true;
		get_monotonic_boottime(&swchgalg->charging_begin_time);
		chr_err("battery recharging!\n");
		info->polling_interval = CHARGING_INTERVAL;
	}

	return 0;
}

static int mtk_switch_charging_current(struct charger_manager *info)
{
	swchg_select_charging_current_limit(info);
	return 0;
}

static int mtk_switch_charging_run(struct charger_manager *info)
{
	struct switch_charging_alg_data *swchgalg = info->algorithm_data;
	int ret = 0;

	chr_err("%s [%d %d], timer=%d\n", __func__, swchgalg->state,
		info->pd_type,
		swchgalg->total_charging_time);

	if (mtk_pdc_check_charger(info) == false &&
	    mtk_is_TA_support_pd_pps(info) == false) {
		mtk_pe20_check_charger(info);
		mtk_pe_check_charger(info);
	}

	do {
		switch (swchgalg->state) {
			chr_err("%s_2 [%d] %d\n", __func__, swchgalg->state,
				info->pd_type);
		case CHR_CC:
			ret = mtk_switch_chr_cc(info);
			break;

		case CHR_PE40_INIT:
			ret = mtk_switch_chr_pe40_init(info);
			break;

		case CHR_PE40_CC:
			ret = mtk_switch_chr_pe40_cc(info);
			break;

		case CHR_BATFULL:
			ret = mtk_switch_chr_full(info);
			break;

		case CHR_ERROR:
			ret = mtk_switch_chr_err(info);
			break;
		}
	} while (ret != 0);
	mtk_switch_check_charging_time(info);

	charger_dev_dump_registers(info->chg1_dev);
	return 0;
}

int charger_dev_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info =
			container_of(nb, struct charger_manager, chg1_nb);
	struct chgdev_notify *data = v;

	chr_info("%s %ld", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		charger_manager_notifier(info, CHARGER_NOTIFY_EOC);
		pr_info("%s: end of charge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		chr_err("%s: safety timer timeout\n", __func__);
		bat_metrics_chg_fault(METRICS_FAULT_SAFETY_TIMEOUT);

		/* If sw safety timer timeout, do not wake up charger thread */
		if (info->enable_sw_safety_timer)
			return NOTIFY_DONE;
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = data->vbusov_stat;
		chr_err("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		bat_metrics_chg_fault(METRICS_FAULT_VBUS_OVP);
		break;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}


int mtk_switch_charging_init(struct charger_manager *info)
{
	struct switch_charging_alg_data *swch_alg;

	swch_alg = devm_kzalloc(&info->pdev->dev,
				sizeof(*swch_alg), GFP_KERNEL);
	if (!swch_alg)
		return -ENOMEM;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev)
		chr_err("Found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	else
		chr_err("*** Error : can't find primary charger ***\n");

	mutex_init(&swch_alg->ichg_aicr_access_mutex);

	info->algorithm_data = swch_alg;
	info->do_algorithm = mtk_switch_charging_run;
	info->plug_in = mtk_switch_charging_plug_in;
	info->plug_out = mtk_switch_charging_plug_out;
	info->do_charging = mtk_switch_charging_do_charging;
	info->do_event = charger_dev_event;
	info->change_current_setting = mtk_switch_charging_current;

	return 0;
}




