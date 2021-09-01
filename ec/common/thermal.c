/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NEW thermal engine module for Chrome EC. This is a completely different
 * implementation from the original version that shipped on Link.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"
#include "power.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

enum thermal_mode g_thermalMode;

static uint8_t Sensorauto = 0;	/* Number of data pairs. */
static int g_tempSensors[TEMP_SENSOR_COUNT] = {0};

int getTempSensors(uint8_t sensorType)
{
    if (sensorType >= TEMP_SENSOR_COUNT) {
        return EC_ERROR_UNKNOWN;
    }
    return g_tempSensors[sensorType];
}

void thermal_type(enum thermal_mode type)
{
    if (type > THERMAL_WITH_GFX) {
        return;
    }

    g_thermalMode = type;
}

static void thermal_control(void)
{
    uint8_t fan, i;
    int tempSensors;
    int rpm_target[CONFIG_FANS] = {0x0};
    uint8_t *mptr = NULL;

    if (!chipset_in_state(CHIPSET_STATE_ON)) {
        return;
    }

    /* go through all the sensors */
    mptr = (uint8_t *)host_get_memmap(EC_MEMMAP_TEMP_SENSOR_AVG);
    for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
        /* read one */

        tempSensors = *(mptr + i);
        if (!Sensorauto) {
            g_tempSensors[i] = tempSensors;
        }
    }

    /* Device high temperature protection mechanism */
     mptr = (uint8_t *)host_get_memmap(EC_MEMMAP_SYS_MISC1);
    if (*mptr & EC_MEMMAP_ACPI_MODE) {
        temperature_protection_mechanism();
    }

    /* cpu thermal control */
    fan = PWM_CH_CPU_FAN;
    rpm_target[fan] = cpu_fan_check_RPM(g_thermalMode);
    if (is_thermal_control_enabled(fan)) {
        fan_set_rpm_target(fan, rpm_target[fan]);
    }

    /* sys thermal control */
    fan = PWM_CH_SYS_FAN;
    rpm_target[fan] = sys_fan_check_RPM(g_thermalMode);
    if (is_thermal_control_enabled(fan)) {
        fan_set_rpm_target(fan, rpm_target[fan]);
    }
}
/* Wait until after the sensors have been read */
DECLARE_HOOK(HOOK_SECOND, thermal_control, HOOK_PRIO_TEMP_SENSOR_DONE + 1);


/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CONSOLE_THERMAL_TEST
static int sensor_count = EC_TEMP_SENSOR_ENTRIES;
static int cc_Sensorinfo(int argc, char **argv)
{
	char leader[20] = "";

    if (!Sensorauto) {
        ccprintf("%sSensorauto: YES\n", leader);  
    } else {
        ccprintf("%sSensorauto: NO\n", leader);  
    }
    
    ccprintf("%sCPU DTS: %4d C\n", leader, g_tempSensors[0]);
    ccprintf("%sAmbiencer NTC: %4d C\n", leader, g_tempSensors[1]);
    ccprintf("%sSSD1 NTC: %4d C\n", leader, g_tempSensors[2]);
    ccprintf("%sPCIE16 NTC: %4d C\n", leader, g_tempSensors[3]);
	ccprintf("%sCPU NTC: %4d C\n", leader, g_tempSensors[4]);
    ccprintf("%sMemory NTC: %4d C\n", leader, g_tempSensors[5]);
    ccprintf("%sSSD2 NTC: %4d C\n", leader, g_tempSensors[6]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(Sensorinfo, cc_Sensorinfo,
			NULL,
			"Print Sensor info");

static int cc_sensorauto(int argc, char **argv)
{
    char *e;
    int input = 0;

    if (sensor_count == 0) {
        ccprintf("sensor count is zero\n");
        return EC_ERROR_INVAL;
    }

    if (argc < 2) {
        ccprintf("fan number is required as the first arg\n");
        return EC_ERROR_PARAM_COUNT;
    }
    input = strtoi(argv[1], &e, 0);
    if (*e || input > 0x01)
        return EC_ERROR_PARAM1;
    argc--;
    argv++;

    if (!input) {
        Sensorauto = 0x00;
    } else {
        Sensorauto = 0x55;
    }
    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sensorauto, cc_sensorauto,
            "{0:auto enable 1:auto disable}",
            "Enable thermal sensor control");

static int cc_sensorset(int argc, char **argv)
{
	int temp = 0;
	char *e;
	int sensor = 0;

	if (sensor_count == 0) {
		ccprintf("sensor count is zero\n");
		return EC_ERROR_INVAL;
	}

	if (sensor_count > 1) {
		if (argc < 2) {
			ccprintf("sensor number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		sensor = strtoi(argv[1], &e, 0);
		if (*e || sensor >= sensor_count)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	temp = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Setting sensor %d temp to %d%%\n", sensor, temp);
    g_tempSensors[sensor] = temp;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sensorset, cc_sensorset,
			"{sensor} percent",
			"Set sensor temp cycle");
#endif

static int command_thermalget(int argc, char **argv)
{
	int i;

	ccprintf("sensor  warn  high  halt   fan_off fan_max   name\n");
	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		ccprintf(" %2d      %3d   %3d    %3d    %3d     %3d     %s\n",
			 i,
			 thermal_params[i].temp_host[EC_TEMP_THRESH_WARN],
			 thermal_params[i].temp_host[EC_TEMP_THRESH_HIGH],
			 thermal_params[i].temp_host[EC_TEMP_THRESH_HALT],
			 thermal_params[i].temp_fan_off,
			 thermal_params[i].temp_fan_max,
			 temp_sensors[i].name);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermalget, command_thermalget,
			NULL,
			"Print thermal parameters (degrees Kelvin)");


static int command_thermalset(int argc, char **argv)
{
	unsigned int n;
	int i, val;
	char *e;

	if (argc < 3 || argc > 7)
		return EC_ERROR_PARAM_COUNT;

	n = (unsigned int)strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	for (i = 2; i < argc; i++) {
		val = strtoi(argv[i], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1 + i - 1;
		if (val < 0)
			continue;
		switch (i) {
		case 2:
			thermal_params[n].temp_host[EC_TEMP_THRESH_WARN] = val;
			break;
		case 3:
			thermal_params[n].temp_host[EC_TEMP_THRESH_HIGH] = val;
			break;
		case 4:
			thermal_params[n].temp_host[EC_TEMP_THRESH_HALT] = val;
			break;
		case 5:
			thermal_params[n].temp_fan_off = val;
			break;
		case 6:
			thermal_params[n].temp_fan_max = val;
			break;
		}
	}

	command_thermalget(0, 0);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermalset, command_thermalset,
			"sensor warn [high [shutdown [fan_off [fan_max]]]]",
			"Set thermal parameters (degrees Kelvin)."
			" Use -1 to skip.");

/*****************************************************************************/
/* Host commands. We'll reuse the host command number, but this is version 1,
 * not version 0. Different structs, different meanings.
 */

static enum ec_status
thermal_command_set_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_set_threshold_v1 *p = args->params;

	if (p->sensor_num >= TEMP_SENSOR_COUNT)
		return EC_RES_INVALID_PARAM;

	thermal_params[p->sensor_num] = p->cfg;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_SET_THRESHOLD,
		     thermal_command_set_threshold,
		     EC_VER_MASK(1));

static enum ec_status
thermal_command_get_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_get_threshold_v1 *p = args->params;
	struct ec_thermal_config *r = args->response;

	if (p->sensor_num >= TEMP_SENSOR_COUNT)
		return EC_RES_INVALID_PARAM;

	*r = thermal_params[p->sensor_num];
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_GET_THRESHOLD,
		     thermal_command_get_threshold,
		     EC_VER_MASK(1));
