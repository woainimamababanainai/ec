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

GPIO_INT(PCH_SLP_SUS_L,		PIN(D, 2), GPIO_INT_BOTH,			power_signal_interrupt)		/* PCH_SLP_SUS_L */
GPIO_INT(SYSTEM_ALW_PG,		PIN(9, 1), GPIO_INT_BOTH,			power_signal_interrupt)		/* xxx_AUX power good */
GPIO_INT(SLP_S3_L,		    PIN(9, 4), GPIO_INT_BOTH,			power_signal_interrupt)		/* xxx_MAIN power enable */
GPIO_INT(SLP_S4_L,		    PIN(C, 6), GPIO_INT_BOTH,			power_signal_interrupt) 	/* PCH SLP_S4 */
GPIO_INT(ATX_PG,		    PIN(D, 1), GPIO_INT_BOTH,			power_signal_interrupt)		/* PSON# 12V power good */
GPIO_INT(VCORE_EN,		    PIN(6, 3), GPIO_INT_BOTH,			power_signal_interrupt)		/* xxx_MAIN power, enable by EC_SLP_S3 */
GPIO_INT(VRMPWRGD,		    PIN(A, 5), GPIO_INT_BOTH,			power_signal_interrupt)		/* VRMPWRGD->CPU_VR_PG */
GPIO_INT(EC_TS3A227_INT,    PIN(B, 7), GPIO_INT_FALLING,	    audio_ts3a227_interrupt)        /* EC_TS3A227_INT */
#ifdef CONFIG_LAN_WAKE_SWITCH
GPIO_INT(EC_LAN_WAKE_L,		PIN(2, 2), GPIO_INT_BOTH,			power_lan_wake_interrupt)	    /* LAN Waked up */
GPIO_INT(EC_WLAN_WAKE_L,    PIN(1, 1), GPIO_INT_BOTH,			power_wlan_wake_interrupt)	    /* WLAN Waked up */
#endif
GPIO_INT(EC_PWR_BTN_ODL,	PIN(0, 0), GPIO_INT_BOTH,			power_button_interrupt)   /* power button */
#if 0
GPIO_INT(EC_PLT_RST_L,      PIN(3, 1), GPIO_INT_BOTH,			cpu_plt_reset_interrupt)  /* Pass through to other xxx_RST_L (PCIEX1/PCIEX16/M2_2280_SSD1) */
#endif
/* Replace of the above for PanGuB board */
GPIO(EC_PCH_PWRGD,          PIN(A, 6), GPIO_OUT_LOW)        /* Power OK to SOC, EC_PCH_PWRGD */
GPIO(DSW_PWROK_EN,          PIN(7, 6), GPIO_OUT_LOW)        /* ec to SOC, DSW_PWROK signal */
GPIO(EC_FCH_PWR_BTN_L,      PIN(7, 4), GPIO_ODR_LOW)          /* Power Button to SOC, EC_PWRBTN_OUT_N */
GPIO(EC_RTC_RST,            PIN(E, 2), GPIO_OUT_LOW)        /* Reserved Pin, RTC reset, EC output */
GPIO(VCCST_PWRGD,           PIN(0, 1), GPIO_OUT_LOW)        /* VCCST power OK, EC output */
GPIO(EC_SLP_S5_L,           PIN(A, 4), GPIO_OUT_LOW)        /* EC_SLP_S5#, output, passthrough SLP_S5_L */
GPIO(TYPEC_VBUS_CTRL,       PIN(D, 7), GPIO_ODR_HIGH)       /* EC control Type-C VBUS power, default to H, no output 5V */
GPIO(PROCHOT_ODL,           PIN(D, 4), GPIO_ODR_LOW)        /* PROCHOT# */
GPIO(PWRGD_140MS,           PIN(B, 0), GPIO_OUT_LOW)        /* VRMPWRGD && PWRGD_140MS -> SYSPWRGD */
GPIO(USB_PWR_EN_L,          PIN(E, 4), GPIO_ODR_HIGH)       /* USB_PWR_EN# */
GPIO(EC_LAN_OFF,            PIN(3, 2), GPIO_OUT_LOW)        /* EC_LAN_OFF */
GPIO(EC_TO_USB_SWITCH,      PIN(A, 2), GPIO_OUT_LOW)        /* G3 to low, other state should to High, connect usb to APU */
GPIO(EC_AUDIO_MUTE_L,       PIN(E, 3), GPIO_OUT_HIGH)       /* EC_AUDIO_MUTE_L */
GPIO(EC_PSON_L,             PIN(E, 5), GPIO_OUT_HIGH)       /* EC_PSO_N, PSU 12V enable, default to high */
GPIO(USB_FING_BLUE_EN_L,    PIN(9, 7), GPIO_ODR_HIGH)       /* Finger KBD USB port power enable */
GPIO(EC_SLP_S3_L,           PIN(D, 5), GPIO_OUT_LOW)        /* EC_SLP_S3#, output, passthrough SLP_S3_L */
GPIO(EC_SLP_S3_PQ9309_L,    PIN(2, 7), GPIO_OUT_LOW)        /* EC passthrough SLP_S3 */
GPIO(EC_FCH_RSMRST_L,       PIN(A, 7), GPIO_OUT_LOW)        /* RSMRST_L */
GPIO(EC_PORT0_PD0,          PIN(6, 1), GPIO_OUT_LOW)        /* TODO: confirm its intial output, EC_PORT0_PD0 */
GPIO(EC_LOM_PWR_EN,         PIN(0, 5), GPIO_OUT_LOW)        /* EC_LOM_PWR_EN */
GPIO(CPU_NMI_L,             PIN(9, 2), GPIO_ODR_LOW)       /* APU_NMI# */
GPIO(EC_CASE_OPEN_CLR,      PIN(1, 0), GPIO_OUT_LOW)        /* Chassis Intrusion Status Clear */
GPIO(HC32F460_PB1_SLP3,     PIN(6, 0), GPIO_ODR_LOW)        /* EC to HC32F460 SLP3 status */
GPIO(HC32F460_PB0_SLP5,     PIN(C, 0), GPIO_ODR_LOW)        /* EC to HC32F460 SLP5 status */
GPIO(EC_PCI_SOCKET_RST_L,   PIN(C, 4), GPIO_OUT_LOW)        /* PCI SOCKET RST */
GPIO(EC_PCI_SSD_RST_L,      PIN(0, 2), GPIO_OUT_LOW)        /* PCI SSD RST */
GPIO(EC_LAN_WLAN_RST_L,     PIN(2, 3), GPIO_OUT_LOW)        /* LAN WLAN RST */
GPIO(EC_TPM_RST_L,          PIN(8, 3), GPIO_OUT_LOW)        /* TPM RST */
GPIO(F22_VCCIO0_VID0,	    PIN(6, 7), GPIO_OUT_LOW)	    /* power sequence VCCIO0_VID0 */
GPIO(F23_VCCIO0_VID1,	    PIN(A, 3), GPIO_OUT_LOW)        /* power sequence VCCIO0_VID1 */

/* Phase PVT use */
GPIO(EC_3VSBSW,	            PIN(0, 6), GPIO_OUT_LOW)       /* Control EC_3VSBSW power, EC_3VSBSW */
/* Phase EVT DVT use */
GPIO(EC_3V_5V_ALW_EN,	    PIN(A, 0), GPIO_OUT_LOW)       /* Control 3V/5V ALW power, EC_3V_5V_ALW_EN */
GPIO(EC_1V8_AUX_EN,	        PIN(7, 0), GPIO_OUT_LOW)        /* 1.8V AUX Enable, EC_1.8V_AUX_EN */

/* SYS_RST# as gpio */
GPIO(SYS_RESET_L,           PIN(C, 5), GPIO_ODR_LOW)			/* reserved pin SYS_RST# */

/* Input */
GPIO(3V3_SB_PGOOD,		    PIN(D, 0), GPIO_INPUT)				/* 3.3V_SB_PGOOD */
GPIO(SLP_S5_L,		        PIN(B, 1), GPIO_INPUT)              /* PCH SLP_S5 */
GPIO(EC_PROJECT_ID0,		PIN(0, 7), GPIO_INPUT)				/* EC_PROJECT_ID0 */
GPIO(EC_PROJECT_ID1,		PIN(3, 0), GPIO_INPUT)				/* EC_PROJECT_ID1 */
GPIO(EC_BRD_ID0,		    PIN(8, 2), GPIO_INPUT)				/* EC_BRD_ID0 */
GPIO(EC_BRD_ID1,		    PIN(0, 4), GPIO_INPUT)				/* EC_BRD_ID1 */
GPIO(EC_BRD_ID2,		    PIN(A, 1), GPIO_INPUT)				/* EC_BRD_ID2 */
GPIO(FPS_RST_L,			    PIN(9, 6), GPIO_INPUT)				/* Connected to CH549, can reset it, but we do not use it */
GPIO(EC_DDR_VDDQ_EN,		PIN(C, 1), GPIO_INPUT)				/* reserved(not used), EC enable VDDQ power */
GPIO(EC_DDR_VPP_EN,		    PIN(D, 3), GPIO_INPUT)				/* reserved(not used), EC eanble VPP power */
GPIO(EC_WLAN_PWR_EN,		PIN(9, 5), GPIO_INPUT)				/* Not used, reserved for WLAN power en, EC_WLAN_PWR_EN */
GPIO(EC_GPIO0_CASE_OPEN_L,	PIN(6, 2), GPIO_INPUT)				/* Chassis Intrusion Detection is inbreak? */
GPIO(EC_DGPU_PROCHOT_L,     PIN(9, 3), GPIO_INPUT)              /* Reserved Pin, DGPU PROCHOT */
GPIO(VCCIO_1_2_GOOG,        PIN(5, 6), GPIO_INPUT)              /* Reserved Pin, VCCIO_1_2_GOOG */
GPIO(5V_3V_PGOOD,	        PIN(1, 4), GPIO_INPUT)	            /* power sequence 5V_3V_PGOOD */

/* Phase gpio Optional */  
GPIO(EC_SLP_S4_L,            PIN(2, 4),        GPIO_INPUT)      /* EC_SLP_S4#, EVT input DVT output, passthrough SLP_S4_L */

/* CLKRUN# as input with pull up */  
UNUSED(PIN(E, 0))		/* SX_EXIT_HOLDOFF# */
UNUSED(PIN(8, 5))
UNUSED(PIN(2, 5))
UNUSED(PIN(2, 6))
UNUSED(PIN(5, 0))
UNUSED(PIN(5, 7))

/* The following signal not implmented by PanGuL board, need confirm We Do Not Them!!! */
UNIMPLEMENTED(AC_PRESENT)
UNIMPLEMENTED(PCH_WAKE_L)
UNIMPLEMENTED(WP_L)
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(PACKET_MODE_EN)

/*
 * The NPCX ESPI driver configures and do not neet to controls SCI, so PCH_SCI_ODL [PIN(7, 6)]
 * is not defined here as GPIO.
 */

/* I2C pins - these will be reconfigured for alternate function below */
GPIO(F460_PA2_CLK,          PIN(B, 5), GPIO_INPUT)                      /* EC_I2C0_SCL0 */
GPIO(F460_PA3_DAT,          PIN(B, 4), GPIO_INPUT)                      /* EC_I2C0_SDA0 */
GPIO(TI_I2C_SCL,            PIN(9, 0), GPIO_INPUT)                      /* EC_I2C1_SCL0 */
GPIO(TI_I2C_SDA,            PIN(8, 7), GPIO_INPUT)                      /* EC_I2C1_SDA0 */
GPIO(EC_PD_I2C1_SCL,        PIN(F, 3), GPIO_INPUT)                      /* EC_I2C4_SCL1 */
GPIO(EC_PD_I2C1_SDA,        PIN(F, 2), GPIO_INPUT)                      /* EC_I2C4_SDA1 */

ALTERNATE(PIN_MASK(B, BIT(4) | BIT(5)), 0, MODULE_I2C, 0)               /* I2C0_SDA0 I2C0_SCL0 */
ALTERNATE(PIN_MASK(9, BIT(0)), 0, MODULE_I2C, 0)                        /* I2C1_SCL0 */
ALTERNATE(PIN_MASK(8, BIT(7)), 0, MODULE_I2C, 0)                        /* I2C1_SDA0 */
ALTERNATE(PIN_MASK(F, BIT(2) | BIT(3)), 0, MODULE_I2C, 0)               /* I2C4_SDA1 I2C4_SCL1 */

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
