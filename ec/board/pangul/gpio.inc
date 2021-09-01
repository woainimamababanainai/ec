/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* GPIO_INT(USB_C0_TCPC_INT_ODL,	PIN(3, 4), GPIO_INT_FALLING,			tcpc_alert_event) */
/* GPIO_INT(USB_C0_PPC_FAULT_ODL,	PIN(6, 3), GPIO_INT_FALLING,			ppc_interrupt) */
/* GPIO_INT(USB_C0_BC12_INT_ODL,	PIN(9, 3), GPIO_INT_FALLING | GPIO_PULL_UP,	bc12_interrupt) */

/* GPIO_INT_BOTH is required for PSL wake from hibernate, but we don't need an interrupt handler. */
/* TODO: Maybe this setting is important for PSL pin, please pay attention to */
/* GPIO(EC_RST_ODL,		PIN(0, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH) */

/* Interrupt signal */
GPIO_INT(USB_C0_MUX_INT_ODL,	PIN(7, 2), GPIO_INT_FALLING, 			tcpc_alert_event)

GPIO_INT(SYSTEM_ALW_PG,		PIN(9, 1), GPIO_INT_BOTH,			power_signal_interrupt)		/* xxx_AUX power good */
GPIO_INT(SLP_S3_L,		    PIN(9, 4), GPIO_INT_BOTH,			power_signal_interrupt)		/* xxx_MAIN power enable */
GPIO_INT(SLP_S5_L,		    PIN(B, 1), GPIO_INT_BOTH,			power_signal_interrupt)
GPIO_INT(ATX_PG,		    PIN(D, 1), GPIO_INT_BOTH,			power_signal_interrupt)		/* PSON# 12V power good */
GPIO_INT(VCORE_EN,		    PIN(6, 3), GPIO_INT_BOTH,			power_signal_interrupt)		/* xxx_MAIN power, enable by EC_SLP_S3 */
GPIO_INT(VRMPWRGD,		    PIN(A, 5), GPIO_INT_BOTH,			power_signal_interrupt)		/* VRMPWRGD->CPU_VR_PG */
#ifdef CONFIG_LAN_WAKE_SWITCH
GPIO_INT(EC_LAN_WAKE_L,		PIN(2, 2), GPIO_INT_BOTH,			power_lan_wake_interrupt)	    /* LAN Waked up */
GPIO_INT(EC_WLAN_WAKE_L,    PIN(1, 1), GPIO_INT_BOTH,			power_wlan_wake_interrupt)	    /* WLAN Waked up */
#endif
GPIO_INT(EC_PWR_BTN_ODL,	PIN(0, 0), GPIO_INT_BOTH,			power_button_interrupt)
GPIO_INT(APU_PCIE_RST_L,	PIN(2, 1), GPIO_INT_BOTH,			apu_pcie_reset_interrupt)	/* Pass through to other xxx_RST_L (PCIEX1/PCIEX16/M2_2280_SSD1) */

/* Replace of the above for PanGuL board */
GPIO(EC_FCH_PWRGD,          PIN(A, 6), GPIO_OUT_LOW)        /* Power OK to SOC, EC_FCH_PWRGD */
GPIO(EC_FCH_SCI_ODL,        PIN(7, 6), GPIO_ODR_LOW)        /* SCI to SOC, EC_SCI_N */
GPIO(EC_FCH_PWR_BTN_L,      PIN(7, 4), GPIO_OUT_LOW)        /* Power Button to SOC, EC_PWRBTN_OUT_N */
GPIO(EC_1V8_AUX_EN,         PIN(7, 0), GPIO_OUT_LOW)        /* 1.8V AUX Enable, EC_1.8V_AUX_EN */
GPIO(PCH_SMI_L,             PIN(C, 6), GPIO_OUT_LOW)        /* SMI to SOC, EC_SMI_N */
GPIO(EC_WP_ROM,             PIN(A, 3), GPIO_OUT_LOW)        /* TODO: confirm its function, 1130_EC_WP_ROM */
GPIO(EC_SLP_S5_L,           PIN(A, 4), GPIO_OUT_LOW)        /* EC_SLP_S5#, output, passthrough SLP_S5_L */
GPIO(TYPEC_VBUS_CTRL,       PIN(D, 7), GPIO_OUT_HIGH)       /* EC control Type-C VBUS power, default to H, no output 5V */
GPIO(PROCHOT_ODL,           PIN(D, 4), GPIO_ODR_LOW)        /* PROCHOT# */
GPIO(PWRGD_140MS,           PIN(B, 0), GPIO_OUT_LOW)        /* VRMPWRGD && PWRGD_140MS -> SYSPWRGD */
GPIO(EC_3V_5V_ALW_EN,       PIN(A, 0), GPIO_OUT_LOW)        /* Control 3V/5V ALW power */
GPIO(USB_PWR_EN_L,          PIN(E, 4), GPIO_OUT_HIGH)       /* USB_PWR_EN# */
GPIO(EC_LAN_OFF,            PIN(3, 2), GPIO_OUT_LOW)        /* EC_LAN_OFF */
GPIO(EC_TO_USB_SWITCH,      PIN(A, 2), GPIO_OUT_LOW)        /* G3 to low, other state should to High, connect usb to APU */
GPIO(EC_PSON_L,             PIN(E, 5), GPIO_OUT_HIGH)       /* EC_PSO_N, PSU 12V enable, default to high */
GPIO(PROM19_EN,             PIN(D, 2), GPIO_OUT_LOW)        /* Refer to Power Sequence */
GPIO(USB_FING_BLUE_EN_L,    PIN(9, 7), GPIO_OUT_HIGH)       /* Finger KBD USB port power enable */
GPIO(EC_SLP_S3_L,           PIN(D, 5), GPIO_OUT_LOW)        /* EC_SLP_S3#, output, passthrough SLP_S3_L */
GPIO(DGPU_PROCHOT_L,        PIN(9, 3), GPIO_OUT_LOW)        /* DGPU_PROCHOT_L */
GPIO(RV_SELECT,             PIN(6, 6), GPIO_OUT_LOW)        /* Default High, Set Low after EC load, we do not use it, set it to low directly */
GPIO(EC_FCH_RSMRST_L,       PIN(A, 7), GPIO_OUT_LOW)        /* RSMRST_L */
GPIO(EC_PORT0_PD0,          PIN(6, 1), GPIO_OUT_LOW)        /* TODO: confirm its intial output, EC_PORT0_PD0 */
GPIO(EC_ALERT_L,            PIN(B, 7), GPIO_OUT_HIGH)       /* TODO: confirm its function, EC_ALERT# */
GPIO(EC_LOM_PWR_EN,         PIN(0, 5), GPIO_OUT_LOW)        /* EC_LOM_PWR_EN */
GPIO(APU_NMI_L,             PIN(9, 2), GPIO_ODR_LOW)       /* APU_NMI# */
GPIO(EC_ALW_EN,             PIN(D, 0), GPIO_OUT_LOW)        /* EC_ALW_EN */
GPIO(PCIEX1_RST_L,          PIN(1, 6), GPIO_OUT_LOW)        /* APU_PCIE_RST_L pass through to this signal */
GPIO(M2_2280_SSD1_RST_L,    PIN(1, 7), GPIO_OUT_LOW)        /* APU_PCIE_RST_L pass through to this signal */
GPIO(PCIEX16_RST_L,         PIN(2, 0), GPIO_OUT_LOW)        /* APU_PCIE_RST_L pass through to this signal */
GPIO(EC_CASE_OPEN_CLR,      PIN(1, 0), GPIO_OUT_LOW)        /* Chassis Intrusion Status Clear */
GPIO(HC32F460_PB1_SLP3,     PIN(6, 0), GPIO_OUT_LOW)        /* EC to HC32F460 SLP3 status */
GPIO(HC32F460_PB0_SLP5,     PIN(C, 0), GPIO_OUT_LOW)        /* EC to HC32F460 SLP5 status */

/* KBRST# as gpio */
GPIO(KBRST_L,			    PIN(C, 5), GPIO_OUT_LOW)			/* KBRST# */

/* Input */
GPIO(2230_ID1,			    PIN(D, 6), GPIO_INPUT)				/* 2230_ID1 */
GPIO(2230_ID2,			    PIN(0, 2), GPIO_INPUT)				/* 2230_ID2 */
GPIO(EC_PROJECT_ID0,		PIN(0, 7), GPIO_INPUT)				/* EC_PROJECT_ID0 */
GPIO(EC_PROJECT_ID1,		PIN(3, 0), GPIO_INPUT)				/* EC_PROJECT_ID1 */
GPIO(EC_PROJECT_ID2,		PIN(6, 7), GPIO_INPUT)				/* EC_PROJECT_ID2 */
GPIO(EC_BRD_ID0,		    PIN(8, 2), GPIO_INPUT)				/* EC_BRD_ID0 */
GPIO(EC_BRD_ID1,		    PIN(0, 4), GPIO_INPUT)				/* EC_BRD_ID1 */
GPIO(EC_BRD_ID2,		    PIN(A, 1), GPIO_INPUT)				/* EC_BRD_ID2 */
GPIO(EC_BT_WAKE,		    PIN(C, 4), GPIO_INPUT)				/* Reserved for BT_WAKE */
GPIO(FPS_RST_L,			    PIN(9, 6), GPIO_INPUT)				/* Connected to CH549, can reset it, but we do not use it */
GPIO(EC_1103_PRON_Q,		PIN(8, 3), GPIO_INPUT)				/* TODO: confirm tis function, EC_1103_PRON_Q */
GPIO(EC_AUD_MUTE_L,		    PIN(1, 4), GPIO_INPUT)				/* Reserved Pin, HW no populate components, set it as INPUT */
GPIO(EC_1103_AP_WAKE_WL,	PIN(2, 3), GPIO_INPUT)				/* Reserved Pin */
GPIO(BAT_12V_SEL,		    PIN(2, 4), GPIO_INPUT)				/* Reserved Pin */
GPIO(EC_DDR_VDDQ_EN,		PIN(C, 1), GPIO_INPUT)				/* reserved(not used), EC enable VDDQ power */
GPIO(EC_DDR_VPP_EN,		    PIN(D, 3), GPIO_INPUT)				/* reserved(not used), EC eanble VPP power */
GPIO(EC_WLAN_PWR_EN,		PIN(9, 5), GPIO_INPUT)				/* Not used, reserved for WLAN power en, EC_WLAN_PWR_EN */
GPIO(APU_RST_L,			    PIN(E, 2), GPIO_INPUT)				/* reserved, not real usage */
GPIO(EC_GPIO0_CASE_OPEN_L,	PIN(6, 2), GPIO_INPUT)				/* Chassis Intrusion Detection is inbreak? */

/* CLKRUN# as input with pull up */
UNUSED(PIN(0, 6))								/* EC_3VSBSW */
UNUSED(PIN(0, 1))								/* EC_JIG_PWR_EN_N */
UNUSED(PIN(8, 5))								/* EC_LPS_EN_N */
UNUSED(PIN(E, 0))								/* FPS_IRQ */
UNUSED(PIN(0, 3))								/* FPS_ID */
UNUSED(PIN(5, 6))								/* CLKRUN# */
UNUSED(PIN(E, 3))
UNUSED(PIN(1, 3))
UNUSED(PIN(1, 5))
UNUSED(PIN(2, 5))
UNUSED(PIN(2, 6))
UNUSED(PIN(2, 7))
UNUSED(PIN(5, 0))
UNUSED(PIN(3, 1))
UNUSED(PIN(B, 6))

/* The following signal not implmented by PanGuL board, need confirm We Do Not Them!!! */
UNIMPLEMENTED(AC_PRESENT)
UNIMPLEMENTED(PCH_WAKE_L)
UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(PACKET_MODE_EN)

/*
 * The NPCX LPC driver configures and controls SCI, so PCH_SCI_ODL [PIN(7, 6)]
 * is not defined here as GPIO.
 */

/* I2C pins - these will be reconfigured for alternate function below */
GPIO(F460_PA2_CLK,          PIN(B, 5), GPIO_INPUT)                      /* EC_I2C0_SCL0 */
GPIO(F460_PA3_DAT,          PIN(B, 4), GPIO_INPUT)                      /* EC_I2C0_SDA0 */
GPIO(SMBCLK_AUX,            PIN(9, 0), GPIO_INPUT)                      /* EC_I2C1_SCL0 */
GPIO(SMBDATA_AUX,           PIN(8, 7), GPIO_INPUT)                      /* EC_I2C1_SDA0 */
GPIO(EC_PD_I2C1_SCL,        PIN(F, 3), GPIO_INPUT)                      /* EC_I2C4_SCL1 */
GPIO(EC_PD_I2C1_SDA,        PIN(F, 2), GPIO_INPUT)                      /* EC_I2C4_SDA1 */
GPIO(FCH_SIC,               PIN(F, 5), GPIO_INPUT | GPIO_SEL_1P8V)      /* EC_I2C5_SCL1 */
GPIO(FCH_SID,               PIN(F, 4), GPIO_INPUT | GPIO_SEL_1P8V)      /* EC_I2C5_SDA1 */

ALTERNATE(PIN_MASK(B, BIT(4) | BIT(5)), 0, MODULE_I2C, 0)               /* I2C0_SDA0 I2C0_SCL0 */
ALTERNATE(PIN_MASK(9, BIT(0)), 0, MODULE_I2C, 0)                        /* I2C1_SCL0 */
ALTERNATE(PIN_MASK(8, BIT(7)), 0, MODULE_I2C, 0)                        /* I2C1_SDA0 */
/*ALTERNATE(PIN_MASK(F, BIT(2) | BIT(3)), 0, MODULE_I2C, 0)*/               /* I2C4_SDA1 I2C4_SCL1 */
ALTERNATE(PIN_MASK(F, BIT(4) | BIT(5)), 0, MODULE_I2C, GPIO_SEL_1P8V)   /* I2C5_SDA1 I2C5_SCL1 */

ALTERNATE(PIN_MASK(6, BIT(4) | BIT(5)), 0, MODULE_UART, 0)  /* Cr50 requires no pullups. */

/* Used as Host Serial Port */
ALTERNATE(PIN_MASK(7, BIT(5)), 1, MODULE_HOST_UART, 0)				/* RXD */
ALTERNATE(PIN_MASK(8, BIT(6)), 1, MODULE_HOST_UART, 0)				/* TXD */
ALTERNATE(PIN_MASK(3, BIT(3) | BIT(6)), 1, MODULE_HOST_UART, 0)			/* CTS_L/RTS_L */
ALTERNATE(PIN_MASK(B, BIT(2) | BIT(3)), 1, MODULE_HOST_UART, 0)			/* DSR/DCD */
ALTERNATE(PIN_MASK(4, BIT(2)), 1, MODULE_HOST_UART, 0)				/* RI_L */
ALTERNATE(PIN_MASK(C, BIT(7)), 0, MODULE_HOST_UART, 0)				/* DTR_BOUT_L */

ALTERNATE(PIN_MASK(4, BIT(1) | BIT(4) | BIT(5)), 0, MODULE_ADC, 0)      /* ADC4: EC_VIN12, ADC0:SYS_THERMAL, ADC1:PCIE_THERMAL */
ALTERNATE(PIN_MASK(4, BIT(3)), 0, MODULE_ADC, 0)                        /* ADC2: SSD2_THERMAL*/
ALTERNATE(PIN_MASK(3, BIT(4) | BIT(7)), 1, MODULE_ADC, 0)               /* ADC5: TYPEC_VBUS_SENSE, ADC6: SSD1_THERMAL */
ALTERNATE(PIN_MASK(E, BIT(1)), 0, MODULE_ADC, 0)                        /* ADC7: VR_THERMAL */
ALTERNATE(PIN_MASK(F, BIT(0) | BIT(1)), 0, MODULE_ADC, 0)               /* ADC8: MEM_THERMAL, ADC9:EC_VIN33 */

ALTERNATE(PIN_MASK(C, BIT(2) | BIT(3)), 1, MODULE_PWM, 0)       /* PWM1: SYS_FAN_PWM, PWM0: CPU_FAN_PWM */

ALTERNATE(PIN_MASK(8, BIT(0)), 1, MODULE_PWM, 0)                /* PWM3: power-on led */

/* Power Switch Logic (PSL) inputs */
ALTERNATE(PIN_MASK(0, BIT(0)), 0, MODULE_PMU, 0)				/* POWER_BUTTON_L */

/* TACH */
ALTERNATE(PIN_MASK(4, BIT(0)), 1,  MODULE_PWM,  0)    /* CPU_FAN_TACH,TA1_SL1 GPIO40 for tachometer input */
ALTERNATE(PIN_MASK(7, BIT(3)), 1,  MODULE_PWM,  0)    /* SYS_FAN_TACH,TA2_SL1 GPIO73 for tachometer input */

/* LPC CLKRUN# and KBRST#, at power on, use them as GPIO */
/* ALTERNATE(PIN_MASK(5, BIT(6)), 0, MODULE_LPC, 0)	*/				/* CLKRUN# */
/* ALTERNATE(PIN_MASK(C, BIT(5)), 0, MODULE_LPC, 0)	*/				/* KBRST# */
