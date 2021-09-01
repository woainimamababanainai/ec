/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button API for Chrome EC */

#ifndef __CROS_EC_POWER_BUTTON_H
#define __CROS_EC_POWER_BUTTON_H

#include "common.h"

enum power_button_state {
	/* Button up; state machine idle */
	PWRBTN_STATE_IDLE = 0,
	/* Button pressed; debouncing done */
	PWRBTN_STATE_PRESSED,
	/* Button down, chipset on; sending initial short pulse */
	PWRBTN_STATE_T0,
	/* Button down, chipset on; delaying until we should reassert signal */
	PWRBTN_STATE_T1,
	/* Button down, signal asserted to chipset */
	PWRBTN_STATE_HELD,
	PWRBTN_STATE_HELD_1,
	/* Force pulse due to lid-open event */
	PWRBTN_STATE_LID_OPEN,
    /* Force pulse due to lan wake event */
    PWRBTN_STATE_LAN_WAKE,
	/* Button released; debouncing done */
	PWRBTN_STATE_RELEASED,
	/* Ignore next button release */
	PWRBTN_STATE_EAT_RELEASE,
	/*
	 * Need to power on system after init, but waiting to find out if
	 * sufficient battery power.
	 */
	PWRBTN_STATE_INIT_ON,
	/* Forced pulse at EC boot due to keyboard controlled reset */
	PWRBTN_STATE_BOOT_KB_RESET,
	/* Power button pressed when chipset was off; stretching pulse */
	PWRBTN_STATE_WAS_OFF,
};

#ifdef NPCX_FAMILY_DT01
extern uint8_t powerbtn_press_4s_flag;
#endif

/**
 * Return non-zero if power button is pressed.
 *
 * Uses the debounced button state, not the raw signal from the GPIO.
 */
int power_button_is_pressed(void);

/**
 * Wait for the power button to be released
 *
 * @param timeout_us Timeout in microseconds, or -1 to wait forever
 * @return EC_SUCCESS if ok, or
 *         EC_ERROR_TIMEOUT if power button failed to release
 */
int power_button_wait_for_release(int timeout_us);

/**
 * Return non-zero if power button signal asserted at hardware input.
 *
 */
int power_button_signal_asserted(void);

/**
 * Interrupt handler for power button.
 *
 * @param signal	Signal which triggered the interrupt.
 */
void power_button_interrupt(enum gpio_signal signal);

/**
 * For x86 systems, force-assert the power button signal to the PCH.
 */
void power_button_pch_press(void);

/**
 * For x86 systems, force-deassert the power button signal to the PCH.
 */
void power_button_pch_release(void);

/**
 * For x86 systems, force a pulse of the power button signal to the PCH.
 */
void power_button_pch_pulse(enum power_button_state state);

/**
 * Returns the time when DSW_PWROK was asserted. It should be customized
 * by each board. See CONFIG_DELAY_DSW_PWROK_TO_PWRBTN for details.
 *
 * @return time in usec when DSW_PWROK was asserted.
 */
int64_t get_time_dsw_pwrok(void);

/**
 * This must be defined when CONFIG_POWER_BUTTON_TO_PCH_CUSTOM is defined. This
 * allows a board to override the default behavior of
 * gpio_set_level(GPIO_PCH_PWRBTN_L, level).
 */
void board_pwrbtn_to_pch(int level);

#ifdef CONFIG_POWER_BUTTON_LOCK_HOST
/*
 * Lock power button form host.
 */
uint8_t get_power_button_lock_flag(void);
#endif

#ifdef CONFIG_LAN_WAKE_SWITCH
uint8_t lan_is_wake(void);
void power_lan_wake_interrupt(enum gpio_signal signal);
void power_wlan_wake_interrupt(enum gpio_signal signal);
uint8_t get_lan_wake_enable(void);
#else
uint8_t get_lan_wake_enable(void);
#endif

#endif  /* __CROS_EC_POWER_BUTTON_H */
