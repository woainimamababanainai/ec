/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "timer.h"
#include "chipset.h"
#include "util.h"

#define TEMP_SENSORS_AVERAGE_COUNT 10

int temp_sensors_avg_s[TEMP_SENSOR_COUNT][TEMP_SENSORS_AVERAGE_COUNT] = {0};
int temp_sensors_avg[TEMP_SENSOR_COUNT] = {0};

uint8_t g_tempSensorCount = 0;

int temp_sensor_read(enum temp_sensor_id id, int *temp_ptr)
{
	const struct temp_sensor_t *sensor;

	if (id < 0 || id >= TEMP_SENSOR_COUNT)
		return EC_ERROR_INVAL;
	sensor = temp_sensors + id;

	return sensor->read(sensor->idx, temp_ptr);
}

static void update_mapped_memory(void)
{
    int i, t;
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_TEMP_SENSOR);

    for (i = 0; i < TEMP_SENSOR_COUNT; i++, mptr++) {
        switch (temp_sensor_read(i, &t)) {
        case EC_ERROR_NOT_POWERED:
            *mptr = EC_TEMP_SENSOR_NOT_POWERED;
            break;
        case EC_ERROR_NOT_CALIBRATED:
            *mptr = EC_TEMP_SENSOR_NOT_CALIBRATED;
            break;
        case EC_SUCCESS:
            *mptr = K_TO_C(t);
            break;
        default:
            *mptr = EC_TEMP_SENSOR_ERROR;
        }
    }
}

static void temp_sensor_average(void)
{
    int i, j;
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_TEMP_SENSOR);
    uint8_t *mptravg = host_get_memmap(EC_MEMMAP_TEMP_SENSOR_AVG);

    /* G3、S5 state, clear temps*/
    if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
            *(mptravg + i) = 0x0;
            *(mptr + i) = 0x0;
            for (j = 0; j < TEMP_SENSORS_AVERAGE_COUNT; j++) {
                temp_sensors_avg_s[i][j] = 0x0;
            }
        }
        return;
    }

    update_mapped_memory();
    for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
        temp_sensors_avg[i] =  0;
        temp_sensors_avg_s[i][g_tempSensorCount] = *(mptr + i);
        for (j = 0; j < TEMP_SENSORS_AVERAGE_COUNT; j++) {
            temp_sensors_avg[i] += temp_sensors_avg_s[i][j];
        }
        *(mptravg + i) = temp_sensors_avg[i] / TEMP_SENSORS_AVERAGE_COUNT;
    }

    g_tempSensorCount++;
    if (g_tempSensorCount >= TEMP_SENSORS_AVERAGE_COUNT) {
        g_tempSensorCount = 0;
    }
}
/* Run after other TEMP tasks, so sensors will have updated first. */
DECLARE_HOOK(HOOK_SECOND, temp_sensor_average, HOOK_PRIO_TEMP_SENSOR_DONE);


static void temp_sensor_init(void)
{
	int i;
	uint8_t *base;

	/*
	 * Initialize memory-mapped data so that if a temperature value is read
	 * before we actually poll the sensors, we don't return an impossible
	 * or out-of-range value.
	 */
	base = host_get_memmap(EC_MEMMAP_TEMP_SENSOR);
	for (i = 0; i < EC_TEMP_SENSOR_ENTRIES; ++i) {
	    base[i] = EC_TEMP_SENSOR_NOT_PRESENT;
	}
}

DECLARE_HOOK(HOOK_INIT, temp_sensor_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

static int command_temps(int argc, char **argv)
{
	int t, i;
	int rv, rv1 = EC_SUCCESS;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		ccprintf("  %-20s: ", temp_sensors[i].name);
		rv = temp_sensor_read(i, &t);
		if (rv)
			rv1 = rv;

		switch (rv) {
		case EC_SUCCESS:
			ccprintf("%d K = %d C", t, K_TO_C(t));
#ifdef CONFIG_THROTTLE_AP
			if (thermal_params[i].temp_fan_off &&
			    thermal_params[i].temp_fan_max)
				ccprintf("  %d%%",
					 thermal_fan_percent(
						 thermal_params[i].temp_fan_off,
						 thermal_params[i].temp_fan_max,
						 t));
#endif
			ccprintf("\n");
			break;
		case EC_ERROR_NOT_POWERED:
			ccprintf("Not powered\n");
			break;
		case EC_ERROR_NOT_CALIBRATED:
			ccprintf("Not calibrated\n");
			break;
		default:
			ccprintf("Error %d\n", rv);
		}
	}

	return rv1;
}
DECLARE_CONSOLE_COMMAND(temps, command_temps,
			NULL,
			"Print temp sensors");

/*****************************************************************************/
/* Host commands */

enum ec_status temp_sensor_command_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_temp_sensor_get_info *p = args->params;
	struct ec_response_temp_sensor_get_info *r = args->response;
	int id = p->id;

	if (id >= TEMP_SENSOR_COUNT)
		return EC_RES_ERROR;

	strzcpy(r->sensor_name, temp_sensors[id].name, sizeof(r->sensor_name));
	r->sensor_type = temp_sensors[id].type;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TEMP_SENSOR_GET_INFO,
		     temp_sensor_command_get_info,
		     EC_VER_MASK(0));
