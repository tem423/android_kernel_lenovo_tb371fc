/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/
#ifndef __TMP_LED_H__
#define __TMP_LED_H__

#define APPLY_PRECISE_NTC_TABLE
#define APPLY_AUXADC_CALI_DATA
#define APPLY_PRECISE_LED_TEMP

#define AUX_IN6_NTC (6)

#define LED_RAP_PULL_UP_R		100000 /* 100K, pull up resister */

#define LED_TAP_OVER_CRITICAL_LOW	4397119 /* base on 100K NTC temp
						 * default value -40 deg
						 */

#define LED_RAP_PULL_UP_VOLTAGE		1800 /* 1.8V ,pull up voltage */

#define LED_RAP_NTC_TABLE		7 /* default is NCP15WF104F03RC(100K) */

#define LED_RAP_ADC_CHANNEL		AUX_IN6_NTC /* default is 0 */

#define LEDMDPA_RAP_PULL_UP_R		100000 /* 100K, pull up resister */


extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);
extern int IMM_IsAdcInitReady(void);

#endif	/* __TMP_LED_H__ */
