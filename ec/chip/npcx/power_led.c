/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Keyboard power button LED state machine.
 *
 * This sets up TIM_POWER_LED to drive the power button LED so that the duty
 * cycle can range from 0-100%. When the lid is closed or turned off, then the
 * PWM is disabled and the GPIO is reconfigured to minimize leakage voltage.
 *
 * In suspend mode, duty cycle transitions progressively slower from 0%
 * to 100%, and progressively faster from 100% back down to 0%. This
 * results in a breathing effect. It takes about 2sec for a full cycle.
 */

#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "power_led.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "ec_commands.h"
#include "host_command.h"
#include "flash.h"

#define LED_STATE_TIMEOUT_MIN   (10 * MSEC)   /* Minimum of 15ms per step */
#define LED_ON_HOLD_TIME        (600 * MSEC)  /* Hold on for 500ms*/
#define LED_OFF_HOLD_TIME       (2600 * MSEC) /* Hold off for 2600ms*/
#define LED_STEP_PERCENT        2   /* Incremental value of each step */

#define LED_BLINK_TIME          (200 * MSEC) /* hold for 200ms at on/off */
#define LED_BLINK_TIME_1        (2 * SECOND) /* hold for 2s at on/off */
static int g_ledBlinkTime = LED_BLINK_TIME;

static enum powerled_state led_state = POWERLED_STATE_ON;
static int power_led_percent = 100;
static uint8_t areaDamage = 0;

void powerled_set_state(enum powerled_state new_state)
{
    led_state = new_state;

    /* Wake up the task */
    task_wake(TASK_ID_POWERLED);
}

void powerled_set_state_blink(enum powerled_state new_state, uint8_t type)
{
    led_state = new_state;

    /* led blink times choose */
    switch (type) {
        case LED_BLINK_TIME_TYPE:
            g_ledBlinkTime = LED_BLINK_TIME;
            break;
        case LED_BLINK_TIME_TYPE1:
            g_ledBlinkTime = LED_BLINK_TIME_1;
            break;
        default :
            break;
    }
    /* Wake up the task */
    task_wake(TASK_ID_POWERLED);
}

static void power_led_set_duty(int percent)
{
    ASSERT((percent >= 0) && (percent <= 100));
    power_led_percent = percent;
    pwm_set_duty(PWM_CH_POWER_LED, percent);
}

static void power_led_use_pwm(void)
{
    pwm_enable(PWM_CH_POWER_LED, 1);
}

static void power_led_manual_off(void)
{
    pwm_enable(PWM_CH_POWER_LED, 0);

    /*
    * Reconfigure GPIO as a floating input. Alternatively we could
    * configure it as an open-drain output and set it to high impedance,
    * but reconfiguring as an input had better results in testing.
    */
    gpio_config_module(MODULE_POWER_LED, 0);
}

/**
 * Return the timeout period (in us) for the current step.
 */
static int power_led_step(void)
{
    int state_timeout = 0;
    static enum { DOWN = -1, UP = 1 } dir = UP;

    if (0 == power_led_percent) {
        dir = UP;
        state_timeout = LED_OFF_HOLD_TIME;
    } else if (100 == power_led_percent) {
        dir = DOWN;
        state_timeout = LED_ON_HOLD_TIME;
    } else {
        /*
        * Decreases timeout as duty cycle percentage approaches
        * 0%, increase as it approaches 100%.
        */
        state_timeout = LED_STATE_TIMEOUT_MIN +
                LED_STATE_TIMEOUT_MIN * (power_led_percent / 33);
    }

    /*
    * The next duty cycle will take effect after the timeout has
    * elapsed for this duty cycle and the power LED task calls this
    * function again.
    */
    power_led_set_duty(power_led_percent);
    power_led_percent += dir * LED_STEP_PERCENT;

    return state_timeout;
}

static int power_led_blink(void)
{
    if (0 == power_led_percent) {
        power_led_percent = 100;
    } else if (100 == power_led_percent) {
        power_led_percent = 0;
    }

    power_led_set_duty(power_led_percent);
    
    return g_ledBlinkTime;
}

void set_area_Damage_flag(uint8_t value)
{
    areaDamage = value;
}
/* bios_boot_block_damage */
static void area_damage_deferred(void)
{
    if (!areaDamage) {
        shutdown_cause_record(LOG_ID_SHUTDOWN_0xD1);
        powerled_set_state_blink(POWERLED_STATE_BLINK, LED_BLINK_TIME_TYPE);
    }
}
DECLARE_DEFERRED(area_damage_deferred);

static void area_damage_power_led_blink(void)
{
    areaDamage = 0;
    hook_call_deferred(&area_damage_deferred_data, (5000 * MSEC));
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, area_damage_power_led_blink, HOOK_PRIO_DEFAULT);

void power_led_task(void *u)
{
    while (1) {
        int state_timeout = -1;

        switch (led_state) {
        case POWERLED_STATE_ON:
            /*
            * "ON" implies driving the LED using the PWM with a
            * duty duty cycle of 100%. This produces a softer
            * brightness than setting the GPIO to solid ON.
            */
            power_led_use_pwm();
            power_led_set_duty(100);
            state_timeout = -1;
            break;
        case POWERLED_STATE_OFF:
            /* Reconfigure GPIO to disable the LED */
            power_led_manual_off();
            state_timeout = -1;
            break;
        case POWERLED_STATE_SUSPEND:
            /* Drive using PWM with variable duty cycle */
            power_led_use_pwm();
            state_timeout = power_led_step();
            break;
        case POWERLED_STATE_BLINK:
            power_led_use_pwm();
            state_timeout = power_led_blink();
            break;
        default:
            break;
        }

        task_wait_event(state_timeout);
    }
}

#ifdef CONFIG_CMD_POWERLED
static int command_powerled(int argc, char **argv)
{
    enum powerled_state state;

    if (argc != 2)
        return EC_ERROR_INVAL;

    if (!strcasecmp(argv[1], "off"))
        state = POWERLED_STATE_OFF;
    else if (!strcasecmp(argv[1], "on"))
        state = POWERLED_STATE_ON;
    else if (!strcasecmp(argv[1], "suspend"))
        state = POWERLED_STATE_SUSPEND;
    else if (!strcasecmp(argv[1], "blink"))
        state = POWERLED_STATE_BLINK;
    else
        return EC_ERROR_INVAL;

    if (state == POWERLED_STATE_BLINK) {
        powerled_set_state_blink(state, LED_BLINK_TIME_TYPE);
    } else {
        powerled_set_state(state);
    }

    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerled, command_powerled,
        "[off | on | suspend | blink]",
        "Change power LED state");
#endif
