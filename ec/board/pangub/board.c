/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/ppc/aoz1380.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/tcpm/rt1715.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "power/rocketlake.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "flash.h"
#include "espi.h"
#include "peci.h"
#include "usb_mux.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#include "gpio_list.h"

/* TODO: confirm with real hardware */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* TODO: need confirm with real hardware */
const struct power_signal_info power_signal_list[] = {
    [X86_SLP_SUS_N] = {
        .gpio = GPIO_PCH_SLP_SUS_L,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "SLP_SUS_DEASSERTED",
    },
    [SYSTEM_ALW_PG] = {
        .gpio = GPIO_SYSTEM_ALW_PG,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "SYSTEM_ALW_PG",
    },
    [X86_SLP_S3_N] = {
        .gpio = GPIO_PCH_SLP_S3_L,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "SLP_S3_DEASSERTED",
    },
    [X86_SLP_S4_N] = {
        .gpio = GPIO_PCH_SLP_S4_L,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "SLP_S4_DEASSERTED",
    },
    [ATX_PG] = {
        .gpio = GPIO_ATX_PG,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "ATX_PG",
    },
    [VCORE_EN] = {
        .gpio = GPIO_VCORE_EN,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "VCORE_EN",
    },
    [VRMPWRGD] = {
        .gpio = GPIO_VRMPWRGD,
        .flags = POWER_SIGNAL_ACTIVE_HIGH,
        .name = "VRMPWRGD",
    },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/*
 * We use 11 as the scaling factor so that the maximum mV value below (2761)
 * can be compressed to fit in a uint8_t.
 */
#define THERMISTOR_SCALING_FACTOR 15

/*
 * Data derived from Seinhart-Hart equation in a resistor divider circuit with
 * Vdd=3300mV, R = 10Kohm, and Murata NCP15XH103F03RC thermistor (B = 3380,
 * T0 = 298.15, nominal resistance (R0) = 10Kohm).
 */
const struct thermistor_data_pair thermistor_data[] = {
    { 2413 / THERMISTOR_SCALING_FACTOR, 0},
    { 2118 / THERMISTOR_SCALING_FACTOR, 10},
    { 1805 / THERMISTOR_SCALING_FACTOR, 20},
    { 1498 / THERMISTOR_SCALING_FACTOR, 30},
    { 1215 / THERMISTOR_SCALING_FACTOR, 40},
    { 969 / THERMISTOR_SCALING_FACTOR, 50},
    { 764 / THERMISTOR_SCALING_FACTOR, 60},
    { 601 / THERMISTOR_SCALING_FACTOR, 70},
    { 471 / THERMISTOR_SCALING_FACTOR, 80},
    { 411 / THERMISTOR_SCALING_FACTOR, 85},
    { 371 / THERMISTOR_SCALING_FACTOR, 90},
    { 329 / THERMISTOR_SCALING_FACTOR, 95},
    { 292 / THERMISTOR_SCALING_FACTOR, 100},
    { 260 / THERMISTOR_SCALING_FACTOR, 105},
    { 232 / THERMISTOR_SCALING_FACTOR, 110},
    { 207 / THERMISTOR_SCALING_FACTOR, 115},
    { 185 / THERMISTOR_SCALING_FACTOR, 120}
};

const struct thermistor_info thermistor_info = {
    .scaling_factor = THERMISTOR_SCALING_FACTOR,
    .num_pairs = ARRAY_SIZE(thermistor_data),
    .data = thermistor_data,
};

int board_get_temp(int idx, int *temp_k)
{
    int mv;
    int temp_c;
    enum adc_channel channel;

    /* thermistor is not powered in G3 */
    if (!chipset_in_state(CHIPSET_STATE_ON))
        return EC_ERROR_NOT_POWERED;

    /* idx is the sensor index set in board temp_sensors[] */
    switch (idx) {
        case TEMP_SENSOR_AMBIENCE_NTC:
            channel = ADC_SENSOR_AMBIENCE_NTC;
            break;    
        case TEMP_SENSOR_SSD1_NTC:
            channel = ADC_SENSOR_SSD1_NTC;
            break;
        case TEMP_SENSOR_PCIEX16_NTC:
            channel = ADC_SENSOR_PCIEX16_NTC;
            break;
        case TEMP_SENSOR_CPU_NTC:
            channel = ADC_SENSOR_CPU_NTC;
            break;
        case TEMP_SENSOR_MEMORY_NTC:
            channel = ADC_SENSOR_MEMORY_NTC;
            break;
        case TEMP_SENSOR_SSD2_NTC:
            channel = ADC_SENSOR_SSD2_NTC;
            break;
        default:
            return EC_ERROR_INVAL;
    }

    mv = adc_read_channel(channel);
    if (mv < 0)
        return EC_ERROR_INVAL;

    temp_c = thermistor_linear_interpolate(mv, &thermistor_info);
    if (temp_c < 0) {
        temp_c = 0;
    }

    *temp_k = C_TO_K(temp_c);
    return EC_SUCCESS;
}

const struct adc_t adc_channels[] = {
	[ADC_SENSOR_AMBIENCE_NTC] = {
		.name = "Ambience NTC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
    [ADC_SENSOR_SSD1_NTC] = {
		.name = "SSD1 NTC",
		.input_ch = NPCX_ADC_CH6,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SENSOR_PCIEX16_NTC] = {
		.name = "PCIEX16 NTC",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
    [ADC_SENSOR_CPU_NTC] = {
		.name = "CPU NTC",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
    [ADC_SENSOR_MEMORY_NTC] = {
		.name = "Memory NTC",
		.input_ch = NPCX_ADC_CH8,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_3P3V] = {
		.name = "Sense_3P3V",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_12V] = {
		.name = "Sense_12V",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
    [ADC_SENSOR_SSD2_NTC] = {
        .name = "SSD2 NTC",
        .input_ch = NPCX_ADC_CH2,
        .factor_mul = ADC_MAX_VOLT,
        .factor_div = ADC_READ_MAX + 1,
        .shift = 0,
    },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CPU_DTS] = {
		.name = "CPU_DTS",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = peci_temp_sensor_get_val,
		.idx = TEMP_SENSOR_CPU_DTS,
	},	
	[TEMP_SENSOR_AMBIENCE_NTC] = {
		.name = "Ambience_NTC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_AMBIENCE_NTC,
	},
    [TEMP_SENSOR_SSD1_NTC] = {
        .name = "SSD1_NTC",
        .type = TEMP_SENSOR_TYPE_BOARD,
        .read = board_get_temp,
        .idx = TEMP_SENSOR_SSD1_NTC,
    },
    [TEMP_SENSOR_PCIEX16_NTC] = {
        .name = "PCIEX16_NTC",
        .type = TEMP_SENSOR_TYPE_BOARD,
        .read = board_get_temp,
        .idx = TEMP_SENSOR_PCIEX16_NTC,
    },
    [TEMP_SENSOR_CPU_NTC] = {
        .name = "CPU_NTC",
        .type = TEMP_SENSOR_TYPE_BOARD,
        .read = board_get_temp,
        .idx = TEMP_SENSOR_CPU_NTC,
    },
    [TEMP_SENSOR_MEMORY_NTC] = {
        .name = "Memory_NTC",
        .type = TEMP_SENSOR_TYPE_BOARD,
        .read = board_get_temp,
        .idx = TEMP_SENSOR_MEMORY_NTC,
    },
    [TEMP_SENSOR_SSD2_NTC] = {
        .name = "SSD2_NTC",
        .type = TEMP_SENSOR_TYPE_BOARD,
        .read = board_get_temp,
        .idx = TEMP_SENSOR_SSD2_NTC,
    },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* TODO: check with real hardware, this is error */
const struct i2c_port_t i2c_ports[] = {
    {
        .name = "hc32F460",
        .port = I2C_PORT_HC32F460,
        .kbps = 400,
        .scl = GPIO_F460_PA2_CLK,
        .sda = GPIO_F460_PA3_DAT,
    },
	{
		.name = "TS3A227E",
		.port = I2C_PORT_TS3A227E,
		.kbps = 400,
		.scl = GPIO_TI_I2C_SCL,
		.sda = GPIO_TI_I2C_SDA,
	},
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl = GPIO_EC_PD_I2C1_SCL,
		.sda = GPIO_EC_PD_I2C1_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. pwm_init. */
const struct pwm_t pwm_channels[] = {
    [PWM_CH_CPU_FAN] = { 
        .channel = 0,
        .flags = PWM_CONFIG_OPEN_DRAIN,
        .freq = 25000,
    },

    [PWM_CH_SYS_FAN] = { 
		.channel = 1,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000,
    },

   [PWM_CH_POWER_LED] = {
        .channel = 3,
        .flags = PWM_CONFIG_DSLEEP,
        .freq = 100,
   },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_conf fan_conf_1 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_1,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 220,
	.rpm_start = 220,
	.rpm_max = 2800,
};

const struct fan_rpm fan_rpm_1 = {
	.rpm_min = 220,
	.rpm_start = 220,
	.rpm_max = 2800,
};

const struct fan_t fans[] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
    [FAN_CH_1] = { .conf = &fan_conf_1, .rpm = &fan_rpm_1, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_CPU_FAN},
    [MFT_CH_1] = { NPCX_MFT_MODULE_2, TCKC_LFCLK, PWM_CH_SYS_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/*******************************************************************************
 * power button
 */

/*
 * b/164921478: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PWRBTN_L.
 */
#define WAIT_GPIO_S5_PGOOD_TIME 80 /* time base ms */
void board_pwrbtn_to_pch(int level)
{
    int i;

    /* Add delay for G3 exit if asserting PWRBTN_L and S5_PGOOD is low. */
    if (!level && !gpio_get_level(GPIO_S5_PGOOD)) {
        /*
        * From Power Sequence, wait 10 ms for RSMRST_L to rise after
        * S5_PGOOD.
        */

        for (i = 0; i <= WAIT_GPIO_S5_PGOOD_TIME; i++) {
            if (gpio_get_level(GPIO_S5_PGOOD)) {
                msleep(10);
                break;
            }

            if (i > (WAIT_GPIO_S5_PGOOD_TIME - 1)) {
                ccprints("Error: pwrbtn S5_PGOOD low ");
                break;
            }
            msleep(20);
        }
    }
    ccprints("PB PCH pwrbtn=%s", level ? "HIGH" : "LOW");
    gpio_set_level(GPIO_PCH_PWRBTN_L, level);

}
#ifdef RECORD_POWER_BUTTON_SHUTDOWN
static void power_button_record(void)
{
    if(power_button_is_pressed())
    {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0x40);
    }
    else
    {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0x41);
    }
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_record, HOOK_PRIO_DEFAULT);
#endif
/*******************************************************************************
 * Board chipset suspend/resume/shutdown/startup
 *
 */

static void pd_reset_deferred(void)
{
    pd_soft_reset();
}
DECLARE_DEFERRED(pd_reset_deferred);

static void board_chipset_resume(void)
{
    /* re-set cold boot */
    if(want_reboot_ap_at_s3 && (reboot_ap_at_s3_cyclecount>0)
        && reboot_ap_at_s3_delay == 0) {
        reboot_ap_at_s3_cyclecount--;
        reboot_ap_at_s3_delay = reboot_ap_at_s3_delay_backup;
        
        if(!reboot_ap_at_s3_cyclecount) {
            want_reboot_ap_at_s3 = false;
            reboot_ap_at_s3_delay = 0;
        }
    }

    hook_call_deferred(&pd_reset_deferred_data, (500 * MSEC));

    wakeup_cause_record(LOG_ID_WAKEUP_0x04);
    ccprints("%s -> %s", __FILE__, __func__);
    return;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);
 
static void board_chipset_suspend(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_SYS_MISC1);

    hook_call_deferred(&pd_reset_deferred_data, (500 * MSEC));

    if (*mptr & EC_MEMMAP_SYSTEM_ENTER_S3) {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0x03);
        mfg_data_write(MFG_POWER_LAST_STATE_OFFSET, 0x55);
    }

    ccprints("%s -> %s", __FILE__, __func__);
    return;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void shutdown_ID_deferred(void)
{
    shutdown_cause_record(LOG_ID_SHUTDOWN_0x01);
}
DECLARE_DEFERRED(shutdown_ID_deferred);


static void board_chipset_shutdown(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_RESET_FLAG);
    uint8_t *state = host_get_memmap(EC_MEMMAP_SYS_MISC1);

    if(0xAA == (*mptr))
    {
        (*mptr) = 0;
        shutdown_cause_record(LOG_ID_SHUTDOWN_0x42);
        ccprints("EC reboot......");
        system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED);
    }

    pd_comm_enable(0, 0);   /* disable USB-C port 0*/

    mfg_data_write(MFG_POWER_LAST_STATE_OFFSET, 0x55);  /* Record last power state */

    /* S3 to S4/S5 fail off */
    if (*state & EC_MEMMAP_SYSTEM_ENTER_S3) {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0x02);
    } else {
        if (!get_abnormal_shutdown()) {
            hook_call_deferred(&shutdown_ID_deferred_data, 3*SECOND);
        }
    }

    *mptr &= ~(EC_MEMMAP_SYSTEM_REBOOT | EC_MEMMAP_SYSTEM_ENTER_S3);

    ccprints("%s -> %s", __FILE__, __func__);
    return;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
    /* re-set cold boot */
    if(want_reboot_ap_at_g3 && (reboot_ap_at_g3_cyclecount>0)) {
        reboot_ap_at_g3_cyclecount--;
        reboot_ap_at_g3_delay = reboot_ap_at_g3_delay_backup;
        
        if(!reboot_ap_at_g3_cyclecount) {
            want_reboot_ap_at_g3 = false;
            reboot_ap_at_g3_delay = 0;
        }
    }

    pd_comm_enable(0, 1);   /* enable USB-C port 0*/

    mfg_data_write(MFG_POWER_LAST_STATE_OFFSET, 0xAA);  /* Record last power state */

    wakeup_cause_record(LOG_ID_WAKEUP_0x06);
    ccprints("%s -> %s", __FILE__, __func__);
    return;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);


static void board_init_config(void)
{
    uint32_t current_reset_flags;
    gpio_config_module(MODULE_HOST_UART, 0);

    /* save EC reset cause to flash */
    /* ec reset cause reference <./include/reset_flag_desc.inc>*/
    current_reset_flags = system_get_reset_flags();
    
    if (IS_BIT_SET(current_reset_flags, 1)) {
        wakeup_cause_record(LOG_ID_WAKEUP_0x40);
    } else if (IS_BIT_SET(current_reset_flags, 3)) {
        wakeup_cause_record(LOG_ID_WAKEUP_0x41);
        /* shutdown_cause_record(LOG_ID_SHUTDOWN_0x08); */
    } else if (IS_BIT_SET(current_reset_flags, 4)) {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0xFE);
        wakeup_cause_record(LOG_ID_WAKEUP_0x42);
    } else if (IS_BIT_SET(current_reset_flags, 5)) {
        wakeup_cause_record(LOG_ID_WAKEUP_0x43);
    } else if (IS_BIT_SET(current_reset_flags, 11)) {
        wakeup_cause_record(LOG_ID_WAKEUP_0x44);
    }
}
DECLARE_HOOK(HOOK_INIT, board_init_config, HOOK_PRIO_DEFAULT);
#if 0
void cpu_plt_reset_interrupt(enum gpio_signal signal)
{
    int debounce_sample = 0;

    int first_sample = gpio_get_level(signal);
    usleep(10);
    debounce_sample = gpio_get_level(signal);

    if (first_sample == debounce_sample) {
        gpio_set_level(GPIO_EC_PCI_SOCKET_RST_L, debounce_sample);
        gpio_set_level(GPIO_EC_PCI_SSD_RST_L, debounce_sample);
        gpio_set_level(GPIO_EC_LAN_WLAN_RST_L, debounce_sample);
        gpio_set_level(GPIO_EC_TPM_RST_L, debounce_sample);

        ccprints("apu_pcie_reset, level=%d\n", gpio_get_level(GPIO_EC_PLT_RST_L));
        return;
    }

    ccprints("Error: apu_pcie_reset glitch, please check");
    return;
}
#endif

static void cpu_plt_reset(void)
{
    int debounce_sample = 0;

    int first_sample = espi_vw_get_wire(VW_PLTRST_L);
    usleep(10);
    debounce_sample = espi_vw_get_wire(VW_PLTRST_L);

    if (first_sample == debounce_sample) {
        gpio_set_level(GPIO_EC_PCI_SOCKET_RST_L, debounce_sample);
        gpio_set_level(GPIO_EC_PCI_SSD_RST_L, debounce_sample);
        gpio_set_level(GPIO_EC_LAN_WLAN_RST_L, debounce_sample);
        gpio_set_level(GPIO_EC_TPM_RST_L, debounce_sample);

        ccprints("cpu_plt_reset, level=%d", espi_vw_get_wire(VW_PLTRST_L));
        return;
    }

    ccprints("Error: cpu_plt_reset glitch, please check");
    return;
}

DECLARE_HOOK(HOOK_PLT_RESET, cpu_plt_reset, HOOK_PRIO_DEFAULT);


/*******************************************************************************
 * EC firmware version set
 *
 */
static void ec_oem_version_set(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_VERSION_X);

    /* Update ec version to RAM */
    *(mptr+0) = BLD_EC_VERSION_X_HEX;
    *(mptr+1) = BLD_EC_VERSION_YZ_HEX;
    *(mptr+2) = BLD_EC_VERSION_TEST_HEX;

    /* Update board ID to RAM */     
    *(mptr+EC_MEMMAP_GPIO_BOARD_ID) = (uint8_t)system_get_board_version();
    
    /* Update project ID to RAM */
    *(mptr+EC_MEMMAP_GPIO_PROJECT_ID) = (uint8_t)system_get_project_version();
}
DECLARE_HOOK(HOOK_INIT, ec_oem_version_set, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, ec_oem_version_set, HOOK_PRIO_DEFAULT);

/*******************************************************************************
 * phase EVT DVT PVT MP different board to configure.
 * EVT:001 DVT:000 PVT:010 MP:011
 */
static void phase_gpio_init(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_GPIO_BOARD_ID);

    if ((*mptr > PHASE_EVT) || (*mptr == PHASE_DVT)) {
        gpio_set_flags(GPIO_EC_SLP_S4_L, GPIO_OUT_LOW); /* gpio24 EC_SLP_S4_L */
        /*gpio_config_pin(MODULE_PWM, GPIO_POWER_LED, 1);*/ /* gpio80 POWER_LED */
    }
}
DECLARE_HOOK(HOOK_INIT, phase_gpio_init, HOOK_PRIO_DEFAULT);


/******************************************************************************/
/* USB PD functions */
/* Power Delivery and charing functions */

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = RT1715_I2C_ADDR_FLAGS,
		},
		.drv = &rt1715_tcpm_drv,
	},
};

void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_MUX_INT_ODL:
		port = 0;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

void variant_tcpc_init(void)
{
	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_MUX_INT_ODL);
}
/* Called after the baseboard_tcpc_init (via +3) */
DECLARE_HOOK(HOOK_INIT, variant_tcpc_init, HOOK_PRIO_INIT_I2C + 3);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/* PaunGuL do not implement RST_ODL signal */
	if (!gpio_get_level(GPIO_USB_C0_MUX_INT_ODL)) {
		status |= PD_STATUS_TCPC_ALERT_0;
	}

	return status;
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	CPRINTSUSB("Skipping C1 TCPC reset because no battery");
}

void board_set_usb_output_voltage(int mv)
{
    if((POWER_S5==power_get_state()) ||
        (POWER_G3==power_get_state())) {
        gpio_set_level(GPIO_TYPEC_VBUS_CTRL, 1);
		gpio_set_level(GPIO_EC_PORT0_PD0, 0);
        return;
    }
    
	if(mv < 0) {
		/* Turn off output voltage, default LDO to 5V */
		gpio_set_level(GPIO_TYPEC_VBUS_CTRL, 1);
		gpio_set_level(GPIO_EC_PORT0_PD0, 0);
	}else if(mv == 5000) {
		gpio_set_level(GPIO_EC_PORT0_PD0, 0);
		gpio_set_level(GPIO_TYPEC_VBUS_CTRL, 0);
	}else if(mv == 9000) {
		gpio_set_level(GPIO_EC_PORT0_PD0, 1);
		gpio_set_level(GPIO_TYPEC_VBUS_CTRL, 0);
	}

	return;
}

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0] = {
		.usb_port = 0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
};
