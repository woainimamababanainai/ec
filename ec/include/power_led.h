/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button LED control for Chrome EC */

#ifndef __CROS_EC_POWER_LED_H
#define __CROS_EC_POWER_LED_H

#include "common.h"

enum powerled_state {
    POWERLED_STATE_OFF,
    POWERLED_STATE_ON,
    POWERLED_STATE_SUSPEND,
    POWERLED_STATE_BLINK,
    POWERLED_STATE_COUNT
};

enum powerled_state_blink {
    LED_BLINK_TIME_TYPE = 0x1,
    LED_BLINK_TIME_TYPE1 = 0x2,
};

#ifdef HAS_TASK_POWERLED

/**
 * Set the power LED
 *
 * @param state		Target state
 */
void powerled_set_state(enum powerled_state state);
void powerled_set_state_blink(enum powerled_state new_state, uint8_t type);
#else

static inline void powerled_set_state(enum powerled_state state) {}
static inline void powerled_set_state_blink(enum powerled_state new_state, uint8_t type) {};

#endif
void set_area_Damage_flag(uint8_t value);

#endif /* __CROS_EC_POWER_LED_H */
