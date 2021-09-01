/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * software watchdog for BLD.
 */
#ifndef __CROS_EC_SOFTWARE_WATCHDOG_H
#define __CROS_EC_SOFTWARE_WATCHDOG_H

/* WakeUp WDT */
typedef struct EC_WAKEUP_WDT {
     uint8_t wdtEn;
     uint16_t time;
     uint16_t countTime;
     uint8_t timeoutNum;
 }ec_wakeup_WDT;

/* Shutdown WDT */
typedef struct EC_SHUTDOAN_WDT {
      uint8_t wdtEn;
      uint16_t time;
      uint16_t countTime;
  }ec_shutdown_WDT;

#define  SW_WDT_DISENABLE       0x00
#define  SW_WDT_ENABLE          0x01

#define  TIMEOUT_NUM0    0x02 /* */
#define  TIMEOUT_NUM1    0x05 /* */

extern ec_wakeup_WDT g_wakeupWDT;
extern ec_shutdown_WDT g_shutdownWDT;

extern void clearShutdownWDtdata(void);
extern void ClearWakeupWdtdata(void);
extern void setWakeupWDtdata(uint16_t time);

extern uint8_t get_chassisIntrusion_data(void);
extern void set_chassisIntrusion_data(uint8_t data);

#endif  /* __CROS_EC_SOFTWARE_WATCHDOG_H */

