/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _MT_GPUFREQ_H_
#define _MT_GPUFREQ_H_

#include <linux/module.h>
#include <linux/clk.h>

struct mt_gpufreq_power_table_info {
	unsigned int gpufreq_khz;
	unsigned int gpufreq_volt;
	unsigned int gpufreq_power;
};

/**************************************************
 * Condition Setting
 **************************************************/
#ifdef CONFIG_MTK_RAM_CONSOLE
#define MT_GPUFREQ_SRAM_DEBUG
#endif

//#define MTK_GPU_BRING_UP
//#define MTK_GPU_LOG
//#define MTK_PBM_READY

/****************************
 * MTK GPUFREQ API
 ****************************/
extern unsigned int mt_gpufreq_get_cur_freq_index(void);
extern unsigned int mt_gpufreq_get_cur_freq(void);
extern unsigned int mt_gpufreq_get_cur_volt(void);
extern unsigned int mt_gpufreq_get_dvfs_table_num(void);
extern unsigned int mt_gpufreq_target(unsigned int request_idx,
						bool is_real_idx);
extern unsigned int mt_gpufreq_voltage_enable_set(unsigned int enable);
extern unsigned int mt_gpufreq_update_volt(unsigned int pmic_volt[],
					   unsigned int array_size);
extern unsigned int mt_gpufreq_get_freq_by_idx(unsigned int idx);
extern unsigned int mt_gpufreq_get_volt_by_idx(unsigned int idx);
extern unsigned int mt_gpufreq_get_ori_opp_idx(unsigned int idx);
extern void mt_gpufreq_thermal_protect(unsigned int limited_power);
extern void mt_gpufreq_restore_default_volt(void);
extern void mt_gpufreq_enable_by_ptpod(void);
extern void mt_gpufreq_disable_by_ptpod(void);
extern unsigned int mt_gpufreq_get_max_power(void);
extern unsigned int mt_gpufreq_get_min_power(void);
extern unsigned int mt_gpufreq_get_thermal_limit_index(void);
extern unsigned int mt_gpufreq_get_thermal_limit_freq(void);
extern void mt_gpufreq_set_power_limit_by_pbm(unsigned int limited_power);
extern unsigned int mt_gpufreq_get_leakage_mw(void);
extern int mt_gpufreq_get_cur_ceiling_idx(void);
extern void mt_gpufreq_set_loading(unsigned int gpu_loading);	/* legacy */
extern void mt_gpufreq_enable_CG(void);
extern void mt_gpufreq_disable_CG(void);
extern void mt_gpufreq_enable_MTCMOS(void);
extern void mt_gpufreq_disable_MTCMOS(void);


extern void (*mtk_devfreq_set_cur_freq_fp)(unsigned long cur_freq);
extern void mt_gpufreq_devfreq_target(unsigned int limited_freq);


#ifdef CONFIG_MTK_RAM_CONSOLE
extern void aee_rr_rec_gpu_dvfs_vgpu(u8 val);
extern void aee_rr_rec_gpu_dvfs_oppidx(u8 val);
extern void aee_rr_rec_gpu_dvfs_status(u8 val);
extern u8 aee_rr_curr_gpu_dvfs_status(void);
#endif				/* CONFIG_MTK_RAM_CONSOLE */

/*****************
 * power limit notification
 ******************/
typedef void (*gpufreq_power_limit_notify) (unsigned int);
extern void mt_gpufreq_power_limit_notify_registerCB(gpufreq_power_limit_notify
						     pCB);

/*****************
 * input boost notification
 ******************/
typedef void (*gpufreq_input_boost_notify) (unsigned int);
extern void mt_gpufreq_input_boost_notify_registerCB(gpufreq_input_boost_notify
						     pCB);

#endif				/* _MT_GPUFREQ_H_ */
