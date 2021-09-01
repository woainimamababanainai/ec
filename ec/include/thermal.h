/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine module for Chrome EC */

#ifndef __CROS_EC_THERMAL_H
#define __CROS_EC_THERMAL_H

/* The thermal configuration for a single temp sensor is defined here. */
#include "ec_commands.h"

enum thermal_mode {
    THERMAL_UMA = 0,
    THERMAL_WITH_GFX,
};

/* We need to to hold a config for each board's sensors. Not const, so we can
 * tweak it at run-time if we have to.
 */
extern struct ec_thermal_config thermal_params[];

/* Helper function to compute percent cooling */
int thermal_fan_percent(int low, int high, int cur);

/* Allow board custom fan control. Called after reading temperature sensors.
 *
 * @param fan Fan ID to control (0 to CONFIG_FANS)
 * @param tmp Array of temperatures (C) for each temperature sensor (size
 *            TEMP_SENSOR_COUNT)
 */
void board_override_fan_control(int fan, int *tmp);
void thermal_type(enum thermal_mode type);
int getTempSensors(uint8_t sensorType);
void temperature_protection_mechanism(void);
int cpu_fan_check_RPM(uint8_t thermalMode);
int sys_fan_check_RPM(uint8_t thermalMode);

#ifdef NPCX_FAMILY_DT03
void set_cpu_model(uint8_t value);
#endif

#endif  /* __CROS_EC_THERMAL_H */
