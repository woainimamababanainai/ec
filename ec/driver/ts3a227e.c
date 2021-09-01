// SPDX-License-Identifier: GPL-2.0-only
/*
 * TS3A227E Autonomous Audio Accessory Detection and Configuration Switch
 *
 * Copyright (C) 2014 Google, Inc.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"
#include "task.h"
#include "i2c.h"
#include "stdbool.h"
#include "keyboard_protocol.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ##args)

struct ts3a227e {
    bool plugged;
    bool mic_present;
    uint8_t buttons_press;
};

/* Microsecond timestamp. */
static uint64_t history_key_time;
/* history key */
static uint8_t history_key;

#define TS3A227E_NUM_BUTTONS 4
uint8_t data_init[TS3A227E_NUM_BUTTONS] = {0};

#define SND_JACK_BTN_0_P       0x01 /* volume stop press */
#define SND_JACK_BTN_0_R       0x02 /* volume stop release*/
#define SND_JACK_BTN_1_P       0x04 /* reserve */
#define SND_JACK_BTN_1_R       0x08 /* reserve */
#define SND_JACK_BTN_2_P       0x10 /* volume up press */
#define SND_JACK_BTN_2_R       0x20 /* volume up release */
#define SND_JACK_BTN_3_P       0x40 /* volume down press */
#define SND_JACK_BTN_3_R       0x80 /* volume down release */

#define TS3A227E_SLAVE_ADDRESS         0x3B /* TS3A227E slave address */

#define SND_JACK_BTN_0_FIELD  (SND_JACK_BTN_0_P | SND_JACK_BTN_0_R)
#define SND_JACK_BTN_1_FIELD  (SND_JACK_BTN_1_P | SND_JACK_BTN_1_R)
#define SND_JACK_BTN_2_FIELD  (SND_JACK_BTN_2_P | SND_JACK_BTN_2_R)
#define SND_JACK_BTN_3_FIELD  (SND_JACK_BTN_3_P | SND_JACK_BTN_3_R)

/* TS3A227E registers */
#define TS3A227E_REG_DEVICE_ID          0x00
#define TS3A227E_REG_INTERRUPT          0x01
#define TS3A227E_REG_KP_INTERRUPT       0x02
#define TS3A227E_REG_INTERRUPT_DISABLE  0x03
#define TS3A227E_REG_SETTING_1          0x04
#define TS3A227E_REG_SETTING_2          0x05
#define TS3A227E_REG_SETTING_3          0x06
#define TS3A227E_REG_SWITCH_CONTROL_1   0x07
#define TS3A227E_REG_SWITCH_CONTROL_2   0x08
#define TS3A227E_REG_SWITCH_STATUS_1    0x09
#define TS3A227E_REG_SWITCH_STATUS_2    0x0a
#define TS3A227E_REG_ACCESSORY_STATUS   0x0b
#define TS3A227E_REG_ADC_OUTPUT         0x0c
#define TS3A227E_REG_KP_THRESHOLD_1     0x0d
#define TS3A227E_REG_KP_THRESHOLD_2     0x0e
#define TS3A227E_REG_KP_THRESHOLD_3     0x0f

/* TS3A227E_REG_INTERRUPT 0x01 */
#define INS_REM_EVENT 0x01
#define DETECTION_COMPLETE_EVENT 0x02

/* TS3A227E_REG_KP_INTERRUPT 0x02 */
#define PRESS_MASK(idx) (0x01 << (2 * (idx)))
#define RELEASE_MASK(idx) (0x02 << (2 * (idx)))

/* TS3A227E_REG_INTERRUPT_DISABLE 0x03 */
#define INS_REM_INT_DISABLE 0x01
#define DETECTION_COMPLETE_INT_DISABLE 0x02
#define ADC_COMPLETE_INT_DISABLE 0x04
#define INTB_DISABLE 0x08

/* TS3A227E_REG_SETTING_1 0x04 */
#define SOFETWARE_RESET 0x80

/* TS3A227E_REG_SETTING_2 0x05 */
#define KP_ENABLE 0x04

/* TS3A227E_REG_SETTING_3 0x06 */
#define MICBIAS_SETTING_SFT (3)
#define MICBIAS_SETTING_MASK   (0x7 << MICBIAS_SETTING_SFT)
#define MICBIAS_SETTING_VALUE  (0x6 << MICBIAS_SETTING_SFT)

/* TS3A227E_REG_ACCESSORY_STATUS  0x0b */
#define TYPE_3_POLE 0x01
#define TYPE_4_POLE_OMTP 0x02
#define TYPE_4_POLE_STANDARD 0x04
#define JACK_INSERTED 0x08
#define EITHER_MIC_MASK (TYPE_4_POLE_OMTP | TYPE_4_POLE_STANDARD)
#define MUTEX_time  300*MSEC
static void ts3a227e_jack_report(struct ts3a227e *ts3a227e)
{
    uint64_t t;

    if (!ts3a227e->mic_present) {
        return;
    }

    t = get_time().val;
    if (t == 0x0) {
        history_key_time = 0x0;
    }

    /* update volume stop/play key */
    if (SND_JACK_BTN_0_FIELD & ts3a227e->buttons_press) {
        /* key mutex */
        if(history_key != SND_JACK_BTN_0_R) {
            if ((t - history_key_time) < MUTEX_time) {
                return;
            }
        }

        /* update Play/stop key */
        history_key = SND_JACK_BTN_0_R;
        if (ts3a227e->buttons_press & SND_JACK_BTN_0_P) {
            keyboard_update_button(KEYBOARD_BUTTON_VOLUME_PLAY, 1);
            CPRINTS("**********************Play/stop--key0--press");
        } else {
            if (SND_JACK_BTN_0_R & ts3a227e->buttons_press) {
                history_key_time = get_time().val;
                CPRINTS("**********************Play/stop--key0--release ");
                keyboard_update_button(KEYBOARD_BUTTON_VOLUME_PLAY, 0);
            }
        }
    } else if (SND_JACK_BTN_2_FIELD & ts3a227e->buttons_press) {
        /* key mutex */
        if(history_key != SND_JACK_BTN_2_R) {
            if ((t - history_key_time) < MUTEX_time) {
                return;
            }
        }

        /* update volume up key */
        history_key = SND_JACK_BTN_2_R;
        if (SND_JACK_BTN_2_P & ts3a227e->buttons_press) {
            CPRINTS("**********************volume up--key2--press ");
            keyboard_update_button(KEYBOARD_BUTTON_VOLUME_UP, 1);
        } else {
            if (SND_JACK_BTN_2_R & ts3a227e->buttons_press) {
                history_key_time = get_time().val;
                CPRINTS("**********************volume up--key2--release  ");
                keyboard_update_button(KEYBOARD_BUTTON_VOLUME_UP, 0);
            }
        }
    } else if (SND_JACK_BTN_3_FIELD & ts3a227e->buttons_press) {
        /* key mutex */
        if(history_key != SND_JACK_BTN_3_R) {
            if ((t - history_key_time) < MUTEX_time) {
                return;
            }
        }

        /* update volume down key */
        history_key = SND_JACK_BTN_3_R;
        if (SND_JACK_BTN_3_P & ts3a227e->buttons_press) {
            CPRINTS("**********************volume down--key3--press ");
            keyboard_update_button(KEYBOARD_BUTTON_VOLUME_DOWN, 1);
        } else {
            if (SND_JACK_BTN_3_R & ts3a227e->buttons_press ) {
                history_key_time = get_time().val;
                CPRINTS("**********************volume down--key3--release ");
                keyboard_update_button(KEYBOARD_BUTTON_VOLUME_DOWN, 0);
            }
        }
    }
}

static int regmap_update_bits(uint8_t reg, uint8_t mask, uint8_t val)
{
    int ret;
    int orig, tmp;

    ret = i2c_read8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
                reg, &orig);
    if (ret) {
        CPRINTS("ts3a227e, failed to read %d register ret=%d", reg, ret);
        return EC_ERROR_INVAL;
    }

    tmp = orig & ~mask;
    tmp |= val & mask;

    if (tmp !=orig) {
        ret = i2c_write8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
            reg, tmp);
        if (ret) {
            CPRINTS("ts3a227e, failed to write %d register ret=%d",reg, ret);
            return EC_ERROR_INVAL;
        }
    }

    return ret;
}

static void ts3a227e_new_jack_state(struct ts3a227e *ts3a227e, uint8_t acc_reg)
{
    bool plugged, mic_present;

    plugged = !!(acc_reg & JACK_INSERTED);
    mic_present = plugged && !!(acc_reg & EITHER_MIC_MASK);

    ts3a227e->plugged = plugged;

    if (mic_present != ts3a227e->mic_present) {
        ts3a227e->mic_present = mic_present;
        ts3a227e->buttons_press = 0;
        if (mic_present) {
            /* Enable key press detection. */
            regmap_update_bits(TS3A227E_REG_SETTING_2,
                KP_ENABLE, KP_ENABLE);
        }
    }
}


/* TS3A227E INT# pin interrput form gpio.inc */
void audio_ts3a227_interrupt(enum gpio_signal signal)
{
    switch (signal) {
        case GPIO_EC_TS3A227_INT:
            task_wake(TASK_ID_TS3A227E);
            break;
        default:
            break;
    }
}

void headset_volume_task(void *u)
{
    struct ts3a227e *ts3a227e = (struct ts3a227e *)data_init;
    int int_reg, kp_int_reg, acc_reg;
    int ret;

    while (1) {
        if (!chipset_in_state(CHIPSET_STATE_ON)) {
            task_wait_event(-1);
        }

        /* Check for plug/unplug. */
        ret = i2c_read8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
                    TS3A227E_REG_INTERRUPT, &int_reg);
        if (ret) {
            CPRINTS("ts3a227e, failed to clear interrupt ret=%d", ret);
        }

        if (int_reg & (DETECTION_COMPLETE_EVENT | INS_REM_EVENT)) {
            i2c_read8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
                    TS3A227E_REG_ACCESSORY_STATUS, &acc_reg);
            ts3a227e_new_jack_state(ts3a227e, acc_reg);
            CPRINTS("ts3a227e, Front Panel Microphone ising insert %d", acc_reg);
        }
        
        /* Report any key events. */
        ret = i2c_read8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
                    TS3A227E_REG_KP_INTERRUPT, &kp_int_reg);
        if (ret) {
            CPRINTS("ts3a227e, failed to clear key interrupt ret=%d", ret);
            return;
        }
        
        if (kp_int_reg) {
            CPRINTS("ts3a227e, key press inerrupt register 0x%02x", kp_int_reg);
        }

        ts3a227e->buttons_press = kp_int_reg;

        ts3a227e_jack_report(ts3a227e);

        task_wait_event(-1);
    }
}

static void ts3a227e_resume(void)
{
    struct ts3a227e *ts3a227e = (struct ts3a227e *)data_init;
    int ret;
    unsigned int acc_reg;

    /* MICBIAS Setting, 2.65V */
    regmap_update_bits(TS3A227E_REG_SETTING_3, MICBIAS_SETTING_MASK,
        MICBIAS_SETTING_VALUE);

    /* regmap_update_bits(TS3A227E_REG_SETTING_1, BIT(0) | BIT(1) | BIT(2),
        BIT(0)); */

    /* i2c_write8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
                    0x0D, 0x30); */

    /* Enable interrupts except for ADC complete. */
    regmap_update_bits(TS3A227E_REG_INTERRUPT_DISABLE,
        INTB_DISABLE | ADC_COMPLETE_INT_DISABLE,
        ADC_COMPLETE_INT_DISABLE);

    /* Read jack status because chip might not trigger interrupt at boot. */
    ret = i2c_read8(NPCX_I2C_PORT1_0, TS3A227E_SLAVE_ADDRESS,
                TS3A227E_REG_ACCESSORY_STATUS, &acc_reg);
    if (ret) {
        CPRINTS("ts3a227e, init failed to read accessory status register ret=%d", ret);
        return;
    }

    ts3a227e->buttons_press = 0x0;
    ts3a227e->mic_present = 0x0;
    ts3a227e->plugged = 0x0;

    ts3a227e_new_jack_state(ts3a227e, acc_reg);
    /* ts3a227e_jack_report(ts3a227e); */

    gpio_enable_interrupt(GPIO_EC_TS3A227_INT);

    task_wake(TASK_ID_TS3A227E);

    CPRINTS("ts3a227e, Initialization successful");
}

DECLARE_HOOK(HOOK_CHIPSET_RESUME, ts3a227e_resume, HOOK_PRIO_DEFAULT);

static void ts3a227e_suspend(void)
{
    CPRINTS("ts3a227e-dev, suspend disable irq");
    gpio_disable_interrupt(GPIO_EC_TS3A227_INT);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, ts3a227e_suspend, HOOK_PRIO_DEFAULT);


