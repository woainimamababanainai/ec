/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Renoir power sequencing module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "power/rocketlake.h"
#include "power_button.h"
#include "power_led.h"
#include "system.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"
#include "wireless.h"
#include "registers.h"
#include "flash.h"
#include "wmi_port.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

static int forcing_shutdown; /* Forced shutdown in progress? */
static int g_abnormal_shutdown;

/* Record shutdown cause id flag .h */
static uint16_t g_cause_flag;

uint8_t get_abnormal_shutdown(void)
{
    return g_abnormal_shutdown;
}

void set_abnormal_shutdown(uint8_t value)
{
    g_abnormal_shutdown = value;
}

void update_cause_flag(uint16_t value)
{
    g_cause_flag |= value;
}
uint16_t get_cause_flag(void)
{
    return g_cause_flag;
}

void chipset_force_shutdown(uint32_t shutdown_id)
{
    CPRINTS("%s -> %s(), shutdown_id=[0x%02x]", __FILE__, __func__, shutdown_id);

    if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        forcing_shutdown = 1;
        power_button_pch_press();

        shutdown_cause_record(shutdown_id);
    }
}

static void chipset_force_g3(void)
{
    /* In theory, EC should withdraw PWRGD,
     * pull low RSMRST_L, then shut down always power,
     * system will enter G3,
     */

    /* Power-on Led turn off */
    powerled_set_state(POWERLED_STATE_OFF);

    /* trun off S0/S3 power */
    gpio_set_level(GPIO_PWRGD_140MS, 0);
    gpio_set_level(GPIO_EC_PCH_PWRGD, 0);
    gpio_set_level(GPIO_EC_SLP_S3_L, 0);
    gpio_set_level(GPIO_EC_SLP_S4_L, 0);
    gpio_set_level(GPIO_EC_SLP_S5_L, 0);
    gpio_set_level(GPIO_EC_SLP_S3_PQ9309_L, 0);
    gpio_set_level(GPIO_VCCST_PWRGD, 0);

    gpio_set_level(GPIO_EC_PSON_L, 1);
    gpio_set_level(GPIO_PCH_RSMRST_L, 0);

    /* Fingerprint keyboard USB port power always trun on*/
    gpio_set_level(GPIO_USB_FING_BLUE_EN_L, 1);

    /* Fingerprint keyboard USB switch to HC32F460 MCU */
    gpio_set_level(GPIO_EC_TO_USB_SWITCH, 0);

    /* pass through SLP_S3/SLP_S5 to HC32F460 MCU */
    gpio_set_level(GPIO_HC32F460_PB1_SLP3, 0);
    gpio_set_level(GPIO_HC32F460_PB0_SLP5, 0);

    /* turn off USB-A port power */
    gpio_set_level(GPIO_USB_PWR_EN_L, 1);

    /* turn off USB-C port power */
    gpio_set_level(GPIO_TYPEC_VBUS_CTRL, 1);
    gpio_set_level(GPIO_EC_PORT0_PD0, 0);

    /* CPU reset low effective */
    gpio_set_level(GPIO_SYS_RESET_L, 0);

    /* trun off S5 power */
    /* gpio_set_level(GPIO_EC_ALW_EN, 0);
    gpio_set_level(GPIO_PROM19_EN, 0); */
    gpio_set_level(GPIO_EC_1V8_AUX_EN, 0);
    gpio_set_level(GPIO_EC_3V_5V_ALW_EN, 0);
    gpio_set_level(GPIO_EC_3VSBSW, 0);

    /* pull down EC gpio, To prevent leakage*/
    gpio_set_level(GPIO_PROCHOT_ODL, 0);
    gpio_set_level(GPIO_DSW_PWROK_EN, 0);
    gpio_set_level(GPIO_CPU_NMI_L, 0);
    /*gpio_set_level(GPIO_EC_ALERT_L, 0);*/

    gpio_set_level(GPIO_EC_FCH_PWR_BTN_L, 0);

    /* gpio_set_level(GPIO_KBRST_L, 0); */
    /* gpio_set_level(GPIO_F22_VCCIO0_VID0, 0); */
    /* gpio_set_level(GPIO_F23_VCCIO0_VID1, 0); */

    CPRINTS("%s -> %s, Power state in G3", __FILE__, __func__);
}

void chipset_force_power_off(uint32_t shutdown_id)
{
    if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        shutdown_cause_record(shutdown_id);
        CPRINTS("EC force power off......");
        chipset_force_g3();
    }
}

static void thermal_shutdown_cause(void)
{
    /* Required rail went away */
    if (!(get_cause_flag() & FORCE_POWER_OFF_THERMAL)) {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0x08);
    } else {
        update_cause_flag(get_cause_flag() & (~ FORCE_POWER_OFF_THERMAL));
    }
}

/* Can we use SYSRST# to do this? */
void chipset_reset(enum chipset_reset_reason reason)
{
    CPRINTS("%s -> %s : %d", __FILE__, __func__, reason);

    if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        CPRINTS("Can't reset: SOC is off");
        return;
    }

    report_ap_reset(reason);
    
    /*
    * Send a pulse to SYSRST_L to trigger a warm reset.
    */
    /* This Windows Platform do not support chipset_reset,
    * its SYS_RST# pin not connected to EC
    */
    /*gpio_set_level(GPIO_SYS_RESET_L, 0);
    usleep(32 * MSEC);
    gpio_set_level(GPIO_SYS_RESET_L, 1);*/

}

void chipset_throttle_cpu(int throttle)
{
    CPRINTS("%s -> %s(%d)", __FILE__, __func__, throttle);
    if (IS_ENABLED(CONFIG_CPU_PROCHOT_ACTIVE_LOW))
        throttle = !throttle;

    if (chipset_in_state(CHIPSET_STATE_ON))
        gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

#ifdef CONFIG_HOSTCMD_ESPI
void chipset_handle_espi_reset_assert(void)
{
    /*
    * eSPI_Reset# pin being asserted without RSMRST# being asserted
    * means there is an unexpected power loss (global reset event).
    * In this case, check if the shutdown is forced by the EC (due
    * to battery, thermal, or console command). The forced shutdown
    * initiates a power button press that we need to release.
    *
    * NOTE: S5_PGOOD input is passed through to the RSMRST# output to
    * the AP.
    */
    if ((power_get_signals() & IN_PGOOD_S5) && forcing_shutdown) {
        power_button_pch_release();
        forcing_shutdown = 0;
    }
}
#endif
enum power_state power_chipset_init(void)
{
    CPRINTS("%s -> %s: power_signal=0x%x", __FILE__, __func__, power_get_signals());

    /* PowerOn init Turn off Power Led */
    powerled_set_state(POWERLED_STATE_OFF);

    /* Pause in S5 when shutting down. */
    power_set_pause_in_s5(1);

    if (!system_jumped_to_this_image()) {
        CPRINTS("chipset init power to G3, current image is RO, no jump");
        return POWER_G3;
    }
    
    /*
    * We are here as RW. We need to handle the following cases:
    *
    * 1. Late sysjump by software sync. AP is in S0.
    * 2. Shutting down in recovery mode then sysjump by EFS2. AP is in S5
    *    and expected to sequence down.
    * 3. Rebooting from recovery mode then sysjump by EFS2. AP is in S5
    *    and expected to sequence up.
    * 4. RO jumps to RW from main() by EFS2. (a.k.a. power on reset, cold
    *    reset). AP is in G3.
    */
    if ((power_get_signals() & IN_PGOOD_S0) == IN_PGOOD_S0) {
        /* case #1. Disable idle task deep sleep when in S0. */
        disable_sleep(SLEEP_MASK_AP_RUN);
        CPRINTS("chipset init power to S0");
        return POWER_S0;
    }
    if (power_get_signals() & IN_PGOOD_S5) {
        /* case #2 & #3 */
        CPRINTS("chipset init power to S5");
        return POWER_S5;
    }
    
    /* case #4 */
    chipset_force_g3();
    CPRINTS("chipset init power to G3");
    return POWER_G3;
}

static void s5_to_s0_deferred(void)
{
    /* switch FingerPrint USB connection to FCH */
    gpio_set_level(GPIO_EC_TO_USB_SWITCH, 1);
}
DECLARE_DEFERRED(s5_to_s0_deferred);

static void s0_to_s5_deferred(void)
{
    /* turn on fingerprint USB port power */
    gpio_set_level(GPIO_USB_FING_BLUE_EN_L, 1);
}
DECLARE_DEFERRED(s0_to_s5_deferred);

enum power_state power_handle_state(enum power_state state)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_GPIO_BOARD_ID);

    if (state == POWER_S5 && forcing_shutdown) {
        power_button_pch_release();
        forcing_shutdown = 0;
    }

    switch (state) {
    case POWER_G3:
        break;

    case POWER_G3S5:
        if (!gpio_get_level(GPIO_3V3_SB_PGOOD)) {
            CPRINTS("Power 3V3_SB_PGOOD error!");
            return POWER_G3;
        }

        /* Exit SOC G3 */
        gpio_set_level(GPIO_EC_3V_5V_ALW_EN, 1);
        gpio_set_level(GPIO_EC_3VSBSW, 1);
        msleep(10);
        gpio_set_level(GPIO_DSW_PWROK_EN, 1);
        /* PCH send SLP_SUS# delay time(t > 95ms) max wait 2s */
        if (power_wait_signals(IN_SLP_SUS_N)) {
            CPRINTS("Wait power PCH SLP SUS time out!");
            return POWER_S5G3;
        }

        gpio_set_level(GPIO_EC_1V8_AUX_EN, 1);

        gpio_set_level(GPIO_CPU_NMI_L, 1);
        gpio_set_level(GPIO_USB_FING_BLUE_EN_L, 1);

        gpio_set_level(GPIO_PROCHOT_ODL, 1);

        gpio_set_level(GPIO_EC_FCH_PWR_BTN_L, 1);
        gpio_set_level(GPIO_EC_TO_USB_SWITCH, 0);
        gpio_set_level(GPIO_HC32F460_PB1_SLP3, 0);
        gpio_set_level(GPIO_HC32F460_PB0_SLP5, 0);

        msleep(10);
        /* chiset_task pause for wait signal */
        if (power_wait_signals(IN_PGOOD_S5)) {
            chipset_force_g3();
            shutdown_cause_record(LOG_ID_SHUTDOWN_0x08);
            return POWER_G3;
        }

        /* Power sequence doc ask for 10ms delay before pull high PCH_RSMRST_L.
         * ec pull high PCH_RSMRST, CPU will start.
         */
        msleep(10);
        gpio_set_level(GPIO_PCH_RSMRST_L, 1);

        /* Call hooks enable EC_LAN_WAKE/EC_WLAN_WAKE interrupt */
        hook_notify(HOOK_CHIPSET_PRE_INIT);

        CPRINTS("%s -> %s, Power state G3->S5", __FILE__, __func__);

        return POWER_S5;

    case POWER_S5:
        if (!power_has_signals(IN_PGOOD_S5)) {
            /* Required rail went away */
            thermal_shutdown_cause(); /* Record LOG_ID_SHUTDOWN_0x08 */
            return POWER_S5G3;
        } else if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 1) {
            /* PCH SLP_S5 turn on, Power up to next state */
            return POWER_S5S3;
        }
        break;

    case POWER_S5S3:
        if (!power_has_signals(IN_PGOOD_S5)) {
            thermal_shutdown_cause(); /* Record LOG_ID_SHUTDOWN_0x08 */
            /* Required rail went away */
            return POWER_S5G3;
        }

        /* hook enable notify fan pwm start */
        hook_notify(HOOK_CHIPSET_12V_ENABLE);

        /* Enable PSON#, low active */
        gpio_set_level(GPIO_EC_PSON_L, 0);

        /* turn on USB-A port power, this signal is low active*/
        gpio_set_level(GPIO_USB_PWR_EN_L, 0);

        /* notify HC32F460 power state S0 */
        gpio_set_level(GPIO_HC32F460_PB1_SLP3, 1);
        gpio_set_level(GPIO_HC32F460_PB0_SLP5, 1);
        
        /* Call hooks now that rails are up */
        hook_notify(HOOK_CHIPSET_STARTUP);

        CPRINTS("%s -> %s, Power state S5->S3", __FILE__, __func__);
        return POWER_S3;

    case POWER_S3:
        if (!power_has_signals(IN_PGOOD_S5)) {
            thermal_shutdown_cause(); /* Record LOG_ID_SHUTDOWN_0x08 */
            /* Required rail went away */
            return POWER_S5G3;
        } else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {

            /* hook enable notify fan pwm start */
            hook_notify(HOOK_CHIPSET_12V_ENABLE);

            /* Enable PSON#, low active */
            gpio_set_level(GPIO_EC_PSON_L, 0);

            /* EC pass through SLP_S5*/
            gpio_set_level(GPIO_EC_SLP_S5_L, 1);
            /* EC pass through SLP_S4*/
            if ((*mptr > PHASE_EVT) || (*mptr == PHASE_DVT)) {
                gpio_set_level(GPIO_EC_SLP_S4_L, 1);
            }

            if(power_wait_voltage()) {
                CPRINTS("error: power wait 12V timeout");
                shutdown_cause_record(LOG_ID_SHUTDOWN_0x46);
                return POWER_S5G3;
            }

            gpio_set_level(GPIO_EC_SLP_S3_L, 1);
            msleep(10);
            gpio_set_level(GPIO_EC_SLP_S3_PQ9309_L, 1);
            msleep(10);
            gpio_set_level(GPIO_VCCST_PWRGD, 1);
            /*gpio_set_level(GPIO_F22_VCCIO0_VID0, 1);
            gpio_set_level(GPIO_F23_VCCIO0_VID1, 1);*/
            /* PCH SLP_S3 turn on, Power up to next state */
            return POWER_S3S0;
        } else if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 0) {
            /* PCH SLP_S5 turn off, Power down to next state */
            return POWER_S3S5;
        }
        break;

    case POWER_S3S0:
        if (!power_has_signals(IN_PGOOD_S5)) {
            thermal_shutdown_cause(); /* Record LOG_ID_SHUTDOWN_0x08 */
            /* Required rail went away */
            return POWER_S5G3;
        }

        /* chiset_task pause for wait signal */
        if (power_wait_signals(IN_PGOOD_ALL_CORE)) {
            CPRINTS("power wait ALL_CORE timeout, atx=%d, vcore_en=%d, vrmpwrgd=%d",
                gpio_get_level(GPIO_ATX_PG),
                gpio_get_level(GPIO_VCORE_EN),
                gpio_get_level(GPIO_VRMPWRGD));
            shutdown_cause_record(LOG_ID_SHUTDOWN_0x47);
            return POWER_S5G3;
        }
        
        CPRINTS("power wait ALL_CORE done, atx=%d, vcore_en=%d, vrmpwrgd=%d",
                gpio_get_level(GPIO_ATX_PG),
                gpio_get_level(GPIO_VCORE_EN),
                gpio_get_level(GPIO_VRMPWRGD));

        /* Power-on Led turn on*/
        powerled_set_state(POWERLED_STATE_ON);

        /* Power sequence doc ask for 10ms delay before pull high GPIO_EC_PCH_PWRGD.
         */
        msleep(10);
        gpio_set_level(GPIO_EC_PCH_PWRGD, 1);

        gpio_set_level(GPIO_SYS_RESET_L, 1);

        /* Power sequence doc ask for 140ms delay before pull high GPIO_PWRGD_140MS.
         */
        msleep(140);
        gpio_set_level(GPIO_PWRGD_140MS, 1);

        /* Enable wireless, whether need? */
        //wireless_set_state(WIRELESS_ON);

        /* clear abnormal shutdown flag */
        g_abnormal_shutdown = 0;

        /* Call hooks now that rails are up */
        hook_notify(HOOK_CHIPSET_RESUME);

        /*
         * Disable idle task deep sleep. This means that the low
         * power idle task will not go into deep sleep while in S0.
         */
        disable_sleep(SLEEP_MASK_AP_RUN);

        CPRINTS("%s -> %s, Power state S3->S0", __FILE__, __func__);

        hook_call_deferred(&s5_to_s0_deferred_data, (600 * MSEC));
        return POWER_S0;

    case POWER_S0:
    if (!power_has_signals(IN_PGOOD_S5)) {
        thermal_shutdown_cause(); /* Record LOG_ID_SHUTDOWN_0x08 */
        ccprintf("ERROR: system Alw PG Abnormal\n");
        /* Required rail went away */
        return POWER_S5G3;
    } else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
        /* PCH SLP_S3 turn off, Power down to next state */
        return POWER_S0S3;
    }
    break;

    case POWER_S0S3:
        /* Power-on Led suspend*/
        powerled_set_state(POWERLED_STATE_SUSPEND);

        /* Suspend wireless, whether need?*/
        //wireless_set_state(WIRELESS_SUSPEND);

        /* withdraw PSON#, low active */
        gpio_set_level(GPIO_EC_PSON_L, 1);

        /* withdraw SYS_RST_L, low active */
        /* gpio_set_level(GPIO_SYS_RST_L, 0); */

        /* withdraw PWRGD_140MS */
        gpio_set_level(GPIO_PWRGD_140MS, 0);

        /* withdraw EC_FCH_PWRGD */
        gpio_set_level(GPIO_EC_PCH_PWRGD, 0);

        /* EC pass through VCCST_PWRGD*/
        gpio_set_level(GPIO_VCCST_PWRGD, 0);

        /* EC pass through SLP_S3*/
        gpio_set_level(GPIO_EC_SLP_S3_L, 0);
        gpio_set_level(GPIO_EC_SLP_S3_PQ9309_L, 0);

        /* gpio_set_level(GPIO_F22_VCCIO0_VID0, 0); */
        /* gpio_set_level(GPIO_F23_VCCIO0_VID1, 0); */

        /* Call hooks before we remove power rails */
        hook_notify(HOOK_CHIPSET_SUSPEND);

        /*
         * Enable idle task deep sleep. Allow the low power idle task
         * to go into deep sleep in S3 or lower.
         */
        enable_sleep(SLEEP_MASK_AP_RUN);

        CPRINTS("%s -> %s, Power state S0->S3", __FILE__, __func__);
        return POWER_S3;

    case POWER_S3S5:
        /* Power-on Led turn off */
        powerled_set_state(POWERLED_STATE_OFF);
        
        /* Call hooks before we remove power rails */
        hook_notify(HOOK_CHIPSET_SHUTDOWN);

        /* Disable wireless, whether need? */
        //wireless_set_state(WIRELESS_OFF);

        /* withdraw USB_PWR_EN_L */
        gpio_set_level(GPIO_USB_PWR_EN_L, 1);

        /* withdraw fingerprint USB port power,turn on after 200m*/
        gpio_set_level(GPIO_USB_FING_BLUE_EN_L, 0);
        hook_call_deferred(&s0_to_s5_deferred_data, (200 * MSEC));
        
        /* notify HC32F460 power state S5 */
        gpio_set_level(GPIO_HC32F460_PB0_SLP5, 0);
        gpio_set_level(GPIO_HC32F460_PB1_SLP3, 0);
    
        /* switch FingerPrint USB connection to MCU */
        gpio_set_level(GPIO_EC_TO_USB_SWITCH, 0);

        /* EC pass through SLP_S4*/
        if ((*mptr > PHASE_EVT) || (*mptr == PHASE_DVT)) {
            gpio_set_level(GPIO_EC_SLP_S4_L, 0);
        }
        /* EC pass through SLP_S5*/
        msleep(40);
        gpio_set_level(GPIO_EC_SLP_S5_L, 0);

        /* EC pass through VCCST_PWRGD*/
        /* gpio_set_level(GPIO_VCCST_PWRGD, 0); */


        /* Call hooks after we remove power rails */
        hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

        CPRINTS("%s -> %s, Power state S3->S5", __FILE__, __func__);
        return POWER_S5;

    case POWER_S5G3:
        chipset_force_g3();
#ifdef CONFIG_WMI_PORT
        post_last_code_s();
#endif

        CPRINTS("%s -> %s, Power state S5->G3", __FILE__, __func__);
        return POWER_G3;

    default:
        break;
    }

    return state;
}

/* initialize, when LAN/WLAN wake enable, need to exit G3, keep S5 */
static void lan_wake_init_exit_G3(void)
{
    if (get_lan_wake_enable()) {
        chipset_exit_hard_off();
    }
}
DECLARE_HOOK(HOOK_INIT, lan_wake_init_exit_G3, HOOK_PRIO_INIT_LAN_WAKE);

/*****************************************************************************/
/*****************************************************************************/
uint8_t g_PowerButtonFactoryTest = 0;

static void set_Power_Button_flag(void)
{
    g_PowerButtonFactoryTest = 0x01;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, set_Power_Button_flag, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, set_Power_Button_flag, HOOK_PRIO_DEFAULT);

/* Host commands */
static enum ec_status
power_button_factory_test(struct host_cmd_handler_args *args)
{
    const struct ec_params_powerbtn_Test *p = args->params;
    struct ec_response_powerbtn_Test *r = args->response;

    /* No need to obtain information, set r->role = 0xff */
    r->role = 0xff;
    if(0x01 == p->role) {  /* clear power button flag */
        g_PowerButtonFactoryTest = 0;
    }else if (0x02 == p->role){  /* get power button flag */
        r->role = g_PowerButtonFactoryTest;
    } else {
        return EC_RES_INVALID_PARAM;
    }

    args->response_size = sizeof(*r);
    return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_POWERBTN_TEST,
		     power_button_factory_test,
		     EC_VER_MASK(0));

/* Host commands */
static enum ec_status
switch_fingerprint_usb_connection(struct host_cmd_handler_args *args)
{
	const struct ec_params_fingerprint *p = args->params;
	struct ec_response_fingerprint *r = args->response;

	/* No need to obtain information, set r->role = 0xff */
	r->role = 0xff;
    if(0 == p->role) {
        /* notify HC32F460 power state S5 */
        gpio_set_level(GPIO_HC32F460_PB0_SLP5, 0);
        gpio_set_level(GPIO_HC32F460_PB1_SLP3, 0);
        /* set 0 switch to MCU */
        gpio_set_level(GPIO_EC_TO_USB_SWITCH, 0);
    }else if (1 == p->role){
        /* notify HC32F460 power state S0 */
        gpio_set_level(GPIO_HC32F460_PB0_SLP5, 1);
        gpio_set_level(GPIO_HC32F460_PB1_SLP3, 1);
        /* set 1 switch to CPU */
        gpio_set_level(GPIO_EC_TO_USB_SWITCH, 1);
    }else if (0xaa == p->role){ /* 0xaa to get fingerprint info */
        r->role = gpio_get_level(GPIO_EC_TO_USB_SWITCH);
    }else {
        return EC_RES_INVALID_PARAM;
    }
	
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_FINGERPRINT,
		     switch_fingerprint_usb_connection,
		     EC_VER_MASK(0));


