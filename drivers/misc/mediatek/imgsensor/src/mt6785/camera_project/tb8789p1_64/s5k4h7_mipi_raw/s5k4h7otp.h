/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/

#ifndef __S5K4H7_OTP_H__
#define __S5K4H7_OTP_H__

extern int iReadRegI2C(u8 *a_pSendData, u16 a_sizeSendData,
		       u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId);

extern int s5k4h7_af_mac;
extern int s5k4h7_af_inf;
extern int s5k4h7_af_lsb;

extern bool S5K4H7_otp_update(void);
extern bool S5K4H7_checksum_awb(void);
extern bool S5K4H7_checksum_lsc(void);

#endif
