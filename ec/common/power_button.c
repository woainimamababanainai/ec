/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button module for Chrome EC */

#include "button.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "chipset.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

/* By default the power button is active low */
#ifndef CONFIG_POWER_BUTTON_FLAGS
#define CONFIG_POWER_BUTTON_FLAGS 0
#endif

#if (defined(NPCX_FAMILY_DT01) || defined(NPCX_FAMILY_DT03))
/* Power Button Factory Test Flag */
extern uint8_t g_PowerButtonFactoryTest;
#endif

static int debounced_power_pressed;	/* Debounced power button state */
static int simulate_power_pressed;
static volatile int power_button_is_stable = 1;

static const struct button_config power_button = {
	.name = "power button",
	.gpio = GPIO_POWER_BUTTON_L,
	.debounce_us = BUTTON_DEBOUNCE_US,
	.flags = CONFIG_POWER_BUTTON_FLAGS,
};

int power_button_signal_asserted(void)
{
	return !!(gpio_get_level(power_button.gpio)
		== (power_button.flags & BUTTON_FLAG_ACTIVE_HIGH) ? 1 : 0);
}

/**
 * Get raw power button signal state.
 *
 * @return 1 if power button is pressed, 0 if not pressed.
 */
static int raw_power_button_pressed(void)
{
	if (simulate_power_pressed)
		return 1;

#ifdef CONFIG_POWER_BUTTON_LOCK_HOST
        if (get_power_button_lock_flag() & EC_MEMMAP_POWER_LOCK) {
            return 0;
        }
#endif

#ifndef CONFIG_POWER_BUTTON_IGNORE_LID
	/*
	 * Always indicate power button released if the lid is closed.
	 * This prevents waking the system if the device is squashed enough to
	 * press the power button through the closed lid.
	 */
	if (!lid_is_open())
		return 0;
#endif

	return power_button_signal_asserted();
}

int power_button_is_pressed(void)
{
	return debounced_power_pressed;
}

int power_button_wait_for_release(int timeout_us)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + timeout_us;

	while (!power_button_is_stable || power_button_is_pressed()) {
		now = get_time();
		if (timeout_us >= 0 && timestamp_expired(deadline, &now)) {
			CPRINTS("%s not released in time", power_button.name);
			return EC_ERROR_TIMEOUT;
		}
		/*
		 * We use task_wait_event() instead of usleep() here. It will
		 * be woken up immediately if the power button is debouned and
		 * changed. However, it is not guaranteed, like the cases that
		 * the power button is debounced but not changed, or the power
		 * button has not been debounced.
		 */
		task_wait_event(MIN(power_button.debounce_us,
				    deadline.val - now.val));
	}

	CPRINTS("%s released in time", power_button.name);
	return EC_SUCCESS;
}

/**
 * Handle power button initialization.
 */
static void power_button_init(void)
{
	if (raw_power_button_pressed())
		debounced_power_pressed = 1;

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(power_button.gpio);
}
DECLARE_HOOK(HOOK_INIT, power_button_init, HOOK_PRIO_INIT_POWER_BUTTON);

#ifdef CONFIG_POWER_BUTTON_INIT_IDLE
/*
 * Set/clear AP_IDLE flag. It's set when the system gracefully shuts down and
 * it's cleared when the system boots up. The result is the system tries to
 * go back to the previous state upon AC plug-in. If the system uncleanly
 * shuts down, it boots immediately. If the system shuts down gracefully,
 * it'll stay at S5 and wait for power button press.
 */
static void pb_chipset_startup(void)
{
	chip_save_reset_flags(chip_read_reset_flags() & ~EC_RESET_FLAG_AP_IDLE);
	system_clear_reset_flags(EC_RESET_FLAG_AP_IDLE);
	CPRINTS("Cleared AP_IDLE flag");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pb_chipset_startup, HOOK_PRIO_DEFAULT);

static void pb_chipset_shutdown(void)
{
	chip_save_reset_flags(chip_read_reset_flags() | EC_RESET_FLAG_AP_IDLE);
	system_set_reset_flags(EC_RESET_FLAG_AP_IDLE);
	CPRINTS("Saved AP_IDLE flag");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pb_chipset_shutdown,
	     /*
	      * Slightly higher than handle_pending_reboot because
	      * it may clear AP_IDLE flag.
	      */
	     HOOK_PRIO_DEFAULT - 1);
#endif

/**
 * Handle debounced power button changing state.
 */
static void power_button_change_deferred(void)
{
	const int new_pressed = raw_power_button_pressed();

	/* Re-enable keyboard scanning if power button is no longer pressed */
	if (!new_pressed)
		keyboard_scan_enable(1, KB_SCAN_DISABLE_POWER_BUTTON);

	/* If power button hasn't changed state, nothing to do */
	if (new_pressed == debounced_power_pressed) {
		power_button_is_stable = 1;
		return;
	}

	debounced_power_pressed = new_pressed;
	power_button_is_stable = 1;

	CPRINTS("%s %s",
		power_button.name, new_pressed ? "pressed" : "released");

	/* Call hooks */
	hook_notify(HOOK_POWER_BUTTON_CHANGE);

	/* Notify host if power button has been pressed */
	if (new_pressed)
		host_set_single_event(EC_HOST_EVENT_POWER_BUTTON);
}
DECLARE_DEFERRED(power_button_change_deferred);

void power_button_interrupt(enum gpio_signal signal)
{
	/*
	 * If power button is pressed, disable the matrix scan as soon as
	 * possible to reduce the risk of false-reboot triggered by those keys
	 * on the same column with refresh key.
	 */
	if (raw_power_button_pressed())
		keyboard_scan_enable(0, KB_SCAN_DISABLE_POWER_BUTTON);

#if (defined(NPCX_FAMILY_DT01) || defined(NPCX_FAMILY_DT03))
    power_button_is_stable = 0;
    if(chipset_in_state(CHIPSET_STATE_ON) && g_PowerButtonFactoryTest != 0) {
        hook_call_deferred(&power_button_change_deferred_data,
            (600 * MSEC));
    } else {
        hook_call_deferred(&power_button_change_deferred_data,
            power_button.debounce_us);
    }
#else
	/* Reset power button debounce time */
	power_button_is_stable = 0;
	hook_call_deferred(&power_button_change_deferred_data,
			   power_button.debounce_us);
#endif
}

/*****************************************************************************/
/* Console commands */

static int command_powerbtn(int argc, char **argv)
{
	int ms = 200;  /* Press duration in ms */
	char *e;

	if (argc > 1) {
		ms = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	ccprintf("Simulating %d ms %s press.\n", ms, power_button.name);
	simulate_power_pressed = 1;
	power_button_is_stable = 0;
	hook_call_deferred(&power_button_change_deferred_data, 0);

	if (ms > 0)
		msleep(ms);

	ccprintf("Simulating %s release.\n", power_button.name);
	simulate_power_pressed = 0;
	power_button_is_stable = 0;
	hook_call_deferred(&power_button_change_deferred_data, 0);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn,
			"[msec]",
			"Simulate power button press");

#ifdef CONFIG_LAN_WAKE_SWITCH

#define POWER_LAN_DEBOUNCE_US    (5 * MSEC)
#define POWER_WLAN_DEBOUNCE_US   (5 * MSEC)
static uint8_t debounced_lan_wake[2];

/*
 * Handle lan/wlan is wake enable.
 */
uint8_t get_lan_wake_enable(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_SYS_MISC2);

    #ifdef NPCX_FAMILY_DT01
    if(0xaa == powerbtn_press_4s_flag) {
        return 0;
    }
    #endif

    if (*mptr & (EC_MEMMAP_POWER_LAN_WAKE | EC_MEMMAP_POWER_WLAN_WAKE)) {
        return 1;
    }

    return 0;
}

/*
 * Handle lan/wlan is wake.
 */
uint8_t lan_is_wake(void)
{

    if (!get_lan_wake_enable()) {
        return 0;
    }

    if (debounced_lan_wake[0] || debounced_lan_wake[1]) {
        debounced_lan_wake[0] = 0x00;
        debounced_lan_wake[1] = 0x00;
        CPRINTS("lan/wlan wake up");
        return 1;
    }

    CPRINTS("error: lan/wlan wake up!");
    return 0;
}

/*
 * Handle debounced lan wake pulse.
 */
static void power_lan_wake_change_deferred(void)
{
    if (!gpio_get_level(GPIO_EC_LAN_WAKE_L)) {
        CPRINTS("power lan is wake");
        debounced_lan_wake[0] = 0x01;
        hook_notify(HOOK_LAN_WAKE);
    }
}
DECLARE_DEFERRED(power_lan_wake_change_deferred);

/*
 * Handle debounced wlan wake pulse.
 */
static void power_wlan_wake_change_deferred(void)
{
    if (!gpio_get_level(GPIO_EC_WLAN_WAKE_L)) {
        CPRINTS("power wlan is wake");
        debounced_lan_wake[1] = 0x01;
        hook_notify(HOOK_LAN_WAKE);
    }
}
DECLARE_DEFERRED(power_wlan_wake_change_deferred);


/*
 * lan/wlan switch initialization code
 */
static void power_lan_wake_init(void)
{
    debounced_lan_wake[0] = 0x00;
    debounced_lan_wake[1] = 0x00;

    /* Enable interrupts, now that we've initialized */
    if (gpio_get_level(GPIO_EC_LAN_WAKE_L)) {
        gpio_enable_interrupt(GPIO_EC_LAN_WAKE_L);
    } else {
        CPRINTS("error: power lan wake init!");
    }

    if (gpio_get_level(GPIO_EC_WLAN_WAKE_L)) {
        gpio_enable_interrupt(GPIO_EC_WLAN_WAKE_L);
    } else {
        CPRINTS("error: power wlan wake init!");
    }
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, power_lan_wake_init, HOOK_PRIO_DEFAULT);

void power_lan_wake_interrupt(enum gpio_signal signal)
{
    /* lan_wake pulse debounce time */
    hook_call_deferred(&power_lan_wake_change_deferred_data, POWER_LAN_DEBOUNCE_US);
}

void power_wlan_wake_interrupt(enum gpio_signal signal)
{
    /* wlan_wake pulse debounce time */
    hook_call_deferred(&power_wlan_wake_change_deferred_data, POWER_WLAN_DEBOUNCE_US);
}

/*****************************************************************************/
/* Console commands */
static int command_powerbtn_lan(int argc, char **argv)
{
    debounced_lan_wake[0] = 0x01;
    hook_notify(HOOK_LAN_WAKE);
    ccprintf("Console command, lan/wlan wake up form s3/s4/s5 state.");
    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtnlan, command_powerbtn_lan,
            NULL,
            "Simulate lan wake pch powerbtn");
#else
uint8_t get_lan_wake_enable(void)
{
    return 0;
}
#endif

