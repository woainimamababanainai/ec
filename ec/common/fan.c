/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Basic Chrome OS fan control */

#include "assert.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "system.h"
#include "util.h"

#if !(DEBUG_FAN)
#define CPRINTS(...)
#else
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)
#endif

/* Fan status data structure */
struct fan_parameter {
    /* booting check rpm_actual */
    int rpm_actual[FAN_CH_COUNT];
    /* booting fan_fault flag */
    uint8_t fan_fault[FAN_CH_COUNT];
};
static struct fan_parameter g_fan_parameter;

#define FAN_REBOOT_CPU_CHECK   BIT(0)
#define FAN_REBOOT_SYS_CHECK   BIT(1)
uint8_t g_fanRebootFlag = FAN_REBOOT_CPU_CHECK | FAN_REBOOT_SYS_CHECK;

#define FAN_THERMAL_CPU_START   BIT(0)
#define FAN_THERMAL_SYS_START   BIT(1)
uint8_t g_fanThermalStart = FAN_THERMAL_CPU_START | FAN_THERMAL_SYS_START;

/* True if we're listening to the thermal control task. False if we're setting
 * things manually. */
static int thermal_control_enabled[CONFIG_FANS];

int is_thermal_control_enabled(int idx)
{
	return thermal_control_enabled[idx];
}

#ifdef CONFIG_FAN_UPDATE_PERIOD
/* Should we ignore the fans for a while? */
static int fan_update_counter[CONFIG_FANS];
#endif

/*
 * Number of fans.
 *
 * Use fan_get_count and fan_set_count to access it. It should be set only
 * before HOOK_INIT/HOOK_PRIO_DEFAULT.
 */
static int fan_count = CONFIG_FANS;

int fan_get_count(void)
{
	return fan_count;
}

void fan_set_count(int count)
{
	/* You can only decrease the count. */
	assert(count <= CONFIG_FANS);
	fan_count = count;
}

#ifndef CONFIG_FAN_RPM_CUSTOM
/* This is the default implementation. It's only called over [0,100].
 * Convert the percentage to a target RPM. We can't simply scale all
 * the way down to zero because most fans won't turn that slowly, so
 * we'll map [1,100] => [FAN_MIN,FAN_MAX], and [0] => "off".
*/
int fan_percent_to_rpm(int fan, int pct)
{
	int rpm, max, min;

	if (!pct) {
		rpm = 0;
	} else {
		min = fans[fan].rpm->rpm_min;
		max = fans[fan].rpm->rpm_max;
		rpm = ((pct - 1) * max + (100 - pct) * min) / 99;
	}

	return rpm;
}
#endif	/* CONFIG_FAN_RPM_CUSTOM */

/* The thermal task will only call this function with pct in [0,100]. */
test_mockable void fan_set_percent_needed(int fan, int pct)
{
	int actual_rpm, new_rpm;

	if (!is_thermal_control_enabled(fan))
		return;

#ifdef CONFIG_FAN_UPDATE_PERIOD
	/* Only set each fan every so often, to avoid rapid changes. */
	fan_update_counter[fan] %= CONFIG_FAN_UPDATE_PERIOD;
	if (fan_update_counter[fan]++)
		return;
#endif

	new_rpm = fan_percent_to_rpm(fan, pct);
	actual_rpm = fan_get_rpm_actual(FAN_CH(fan));

	/* If we want to turn and the fans are currently significantly below
	 * the minimum turning speed, we should turn at least as fast as the
	 * necessary start speed instead. */
	if (new_rpm &&
	    actual_rpm < fans[fan].rpm->rpm_min * 9 / 10 &&
	    new_rpm < fans[fan].rpm->rpm_start)
		new_rpm = fans[fan].rpm->rpm_start;

	fan_set_rpm_target(FAN_CH(fan), new_rpm);
}

static void set_enabled(int fan, int enable)
{
	fan_set_enabled(FAN_CH(fan), enable);

	if (fans[fan].conf->enable_gpio >= 0)
		gpio_set_level(fans[fan].conf->enable_gpio, enable);
}

test_export_static void set_thermal_control_enabled(int fan, int enable)
{
	thermal_control_enabled[fan] = enable;

	/* If controlling the fan, need it in RPM-control mode */
	if (enable) {
		fan_set_rpm_mode(FAN_CH(fan), 1);
    } else {
        fan_set_rpm_mode(FAN_CH(fan), 0); 
    }
}

static void set_duty_cycle(int fan, int percent)
{
	/* Move the fan to manual control */
	fan_set_rpm_mode(FAN_CH(fan), 0);

	/* enable the fan when non-zero duty */
	set_enabled(fan, (percent > 0) ? 1 : 0);

	/* Disable thermal engine automatic fan control. */
	set_thermal_control_enabled(fan, 0);

	/* Set the duty cycle */
	fan_set_duty(FAN_CH(fan), percent);
}

uint8_t get_fan_fault(uint8_t fan)
{
    return g_fan_parameter.fan_fault[fan];
}

/*****************************************************************************/
/* Console commands */

static int cc_fanauto(int argc, char **argv)
{
	char *e;
	int fan = 0;

	if (fan_count > 1) {
		if (argc < 2) {
			ccprintf("fan number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		fan = strtoi(argv[1], &e, 0);
		if (*e || fan >= fan_count)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	set_thermal_control_enabled(fan, 1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanauto, cc_fanauto,
			"{fan}",
			"Enable thermal fan control");

/* Return 0 for off, 1 for on, -1 for unknown */
static int is_powered(int fan)
{
	int is_pgood = -1;

	/* If we have an enable output, see if it's on or off. */
	if (fans[fan].conf->enable_gpio >= 0)
		is_pgood = gpio_get_level(fans[fan].conf->enable_gpio);
	/* If we have a pgood input, it overrides any enable output. */
	if (fans[fan].conf->pgood_gpio >= 0)
		is_pgood = gpio_get_level(fans[fan].conf->pgood_gpio);

	return is_pgood;
}

static int cc_faninfo(int argc, char **argv)
{
	static const char * const human_status[] = {
		"not spinning", "changing", "locked", "fault", "frustrated"
	};
	int tmp, is_pgood;
	int fan;
	char leader[20] = "";
	for (fan = 0; fan < fan_count; fan++) {
		if (fan_count > 1)
			snprintf(leader, sizeof(leader), "Fan %d ", fan);
		if (fan)
			ccprintf("\n");
		ccprintf("%sActual: %4d rpm\n", leader,
			 fan_get_rpm_actual(FAN_CH(fan)));
		ccprintf("%sTarget: %4d rpm\n", leader,
			 fan_get_rpm_target(FAN_CH(fan)));
		ccprintf("%sDuty:   %d%%\n", leader,
			 fan_get_duty(FAN_CH(fan)));
        if (g_fan_parameter.fan_fault[fan] == FAN_STATUS_FAULT) {
            tmp = g_fan_parameter.fan_fault[fan];
        } else {
            tmp = fan_get_status(FAN_CH(fan));
        }
		ccprintf("%sStatus: %d (%s)\n", leader,
			 tmp, human_status[tmp]);
		ccprintf("%sMode:   %s\n", leader,
			 fan_get_rpm_mode(FAN_CH(fan)) ? "rpm" : "duty");
		ccprintf("%sAuto:   %s\n", leader,
			 is_thermal_control_enabled(fan) ? "yes" : "no");
		ccprintf("%sEnable: %s\n", leader,
			 fan_get_enabled(FAN_CH(fan)) ? "yes" : "no");
        ccprintf("%sFault: %s\n", leader,
			 get_fan_fault(FAN_CH(fan)) ? "yes" : "no");
		is_pgood = is_powered(fan);
		if (is_pgood >= 0)
			ccprintf("%sPower:  %s\n", leader,
				 is_pgood ? "yes" : "no");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(faninfo, cc_faninfo,
			NULL,
			"Print fan info");

static int cc_fanset(int argc, char **argv)
{
	int rpm;
	char *e;
	int fan = 0;

	if (fan_count == 0) {
		ccprintf("Fan count is zero\n");
		return EC_ERROR_INVAL;
	}

	if (fan_count > 1) {
		if (argc < 2) {
			ccprintf("fan number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		fan = strtoi(argv[1], &e, 0);
		if (*e || fan >= fan_count)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	rpm = strtoi(argv[2], &e, 0);
	if (*e == '%') {		/* Wait, that's a percentage */
		ccprintf("Fan rpm given as %d%%\n", rpm);
		if (rpm < 0)
			rpm = 0;
		else if (rpm > 100)
			rpm = 100;
		rpm = fan_percent_to_rpm(fan, rpm);
	} else if (*e) {
		return EC_ERROR_PARAM1;
	}

	/* Move the fan to automatic control */
	fan_set_rpm_mode(FAN_CH(fan), 1);

	/* enable the fan when non-zero rpm */
	set_enabled(fan, (rpm > 0) ? 1 : 0);

	/* Disable thermal engine automatic fan control. */
	set_thermal_control_enabled(fan, 0);

	fan_set_rpm_target(FAN_CH(fan), rpm);

	ccprintf("Setting fan %d rpm target to %d\n", fan, rpm);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanset, cc_fanset,
			"{fan} (rpm | pct%)",
			"Set fan speed");

static int cc_fanduty(int argc, char **argv)
{
	int percent = 0;
	char *e;
	int fan = 0;

	if (fan_count == 0) {
		ccprintf("Fan count is zero\n");
		return EC_ERROR_INVAL;
	}

	if (fan_count > 1) {
		if (argc < 2) {
			ccprintf("fan number is required as the first arg\n");
			return EC_ERROR_PARAM_COUNT;
		}
		fan = strtoi(argv[1], &e, 0);
		if (*e || fan >= fan_count)
			return EC_ERROR_PARAM1;
		argc--;
		argv++;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	percent = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Setting fan %d duty cycle to %d%%\n", fan, percent);
	set_duty_cycle(fan, percent);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fanduty, cc_fanduty,
			"{fan} percent",
			"Set fan duty cycle");

/*****************************************************************************/
/* DPTF interface functions */

/* 0-100% if in duty mode. -1 if not */
int dptf_get_fan_duty_target(void)
{
	int fan = 0;				/* TODO(crosbug.com/p/23803) */

	if (fan_count == 0)
		return -1;

	if (is_thermal_control_enabled(fan) || fan_get_rpm_mode(FAN_CH(fan)))
		return -1;

	return fan_get_duty(FAN_CH(fan));
}

/* 0-100% sets duty, out of range means let the EC drive */
void dptf_set_fan_duty_target(int pct)
{
	int fan;

	if (pct < 0 || pct > 100) {
		/* TODO(crosbug.com/p/23803) */
		for (fan = 0; fan < fan_count; fan++)
			set_thermal_control_enabled(fan, 1);
	} else {
		/* TODO(crosbug.com/p/23803) */
		for (fan = 0; fan < fan_count; fan++)
			set_duty_cycle(fan, pct);
	}
}

/*****************************************************************************/
/* Host commands */

static enum ec_status
hc_pwm_get_fan_target_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_get_fan_rpm *p = args->params;
	struct ec_response_pwm_get_fan_rpm *r = args->response;
	int fan;

	fan = p->fan_idx;
	if (fan >= fan_count)
		return EC_RES_ERROR;

	/* TODO(crosbug.com/p/23803) */
	r->rpm = fan_get_rpm_target(FAN_CH(fan));
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_TARGET_RPM,
		     hc_pwm_get_fan_target_rpm,
		     EC_VER_MASK(0));

static enum ec_status
hc_pwm_set_fan_target_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_target_rpm_v1 *p_v1 = args->params;
	const struct ec_params_pwm_set_fan_target_rpm_v0 *p_v0 = args->params;
	int fan;

	if (args->version == 0) {
		for (fan = 0; fan < fan_count; fan++) {
			/* enable the fan if rpm is non-zero */
			set_enabled(fan, (p_v0->rpm > 0) ? 1 : 0);

			set_thermal_control_enabled(fan, 0);
			fan_set_rpm_mode(FAN_CH(fan), 1);
			fan_set_rpm_target(FAN_CH(fan), p_v0->rpm);
		}

		return EC_RES_SUCCESS;
	}

	fan = p_v1->fan_idx;
	if (fan >= fan_count)
		return EC_RES_ERROR;

	/* enable the fan if rpm is non-zero */
	set_enabled(fan, (p_v1->rpm > 0) ? 1 :0);

	set_thermal_control_enabled(fan, 0);
	fan_set_rpm_mode(FAN_CH(fan), 1);
	fan_set_rpm_target(FAN_CH(fan), p_v1->rpm);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_TARGET_RPM,
		     hc_pwm_set_fan_target_rpm,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static enum ec_status hc_pwm_set_fan_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_fan_duty_v1 *p_v1 = args->params;
	const struct ec_params_pwm_set_fan_duty_v0 *p_v0 = args->params;
	int fan;

	if (args->version == 0) {
		for (fan = 0; fan < fan_count; fan++)
			set_duty_cycle(fan, p_v0->percent);

		return EC_RES_SUCCESS;
	}

	fan = p_v1->fan_idx;
	if (fan >= fan_count)
		return EC_RES_ERROR;

	set_duty_cycle(fan, p_v1->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_FAN_DUTY,
		     hc_pwm_set_fan_duty,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static enum ec_status
hc_thermal_auto_fan_ctrl(struct host_cmd_handler_args *args)
{
	int fan;
	const struct ec_params_auto_fan_ctrl_v1 *p_v1 = args->params;

	if (args->version == 0) {
		for (fan = 0; fan < fan_count; fan++)
			set_thermal_control_enabled(fan, 1);

		return EC_RES_SUCCESS;
	}

	fan = p_v1->fan_idx;
	if (fan >= fan_count)
		return EC_RES_ERROR;

	set_thermal_control_enabled(fan, 1);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_AUTO_FAN_CTRL,
		     hc_thermal_auto_fan_ctrl,
		     EC_VER_MASK(0)|EC_VER_MASK(1));


/*****************************************************************************/
/* Hooks */

/* We only have a limited number of memory-mapped slots to report fan speed to
 * the AP. If we have more fans than that, some will be inaccessible. But
 * if we're using that many fans, we probably have bigger problems.
 */
BUILD_ASSERT(CONFIG_FANS <= EC_FAN_SPEED_ENTRIES);

#define PWMFAN_SYSJUMP_TAG 0x5046  /* "PF" */
#define PWM_HOOK_VERSION 1
/* Saved PWM state across sysjumps */
struct pwm_fan_state {
	/* TODO(crosbug.com/p/23530): Still treating all fans as one. */
	uint16_t rpm;
	uint8_t flag;	/* FAN_STATE_FLAG_* */
};

/* For struct pwm_fan_state.flag */
#define FAN_STATE_FLAG_ENABLED	BIT(0)
#define FAN_STATE_FLAG_THERMAL	BIT(1)

void FanRebootFlag(void)
{
    g_fanRebootFlag = FAN_REBOOT_CPU_CHECK | FAN_REBOOT_SYS_CHECK;
    g_fanThermalStart = FAN_THERMAL_CPU_START | FAN_THERMAL_SYS_START;
}

static void pwm_fan_init(void)
{
	uint16_t *mapped;
	int i;
	int fan;

	if (fan_count == 0)
		return;

	for (fan = 0; fan < fan_count; fan++)
		fan_channel_setup(FAN_CH(fan), fans[fan].conf->flags);

	/* Initialize memory-mapped data */
	mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN_RPM);
	for (i = 0; i < EC_FAN_SPEED_ENTRIES; i++)
		mapped[i] = EC_FAN_SPEED_NOT_PRESENT;
}
DECLARE_HOOK(HOOK_INIT, pwm_fan_init, HOOK_PRIO_DEFAULT);

static void update_fan_mapped(void)
{
    uint16_t *mapped_rpm = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN_RPM);
    uint8_t *mapped_fault = (uint8_t *)host_get_memmap(EC_MEMMAP_CPU_FAN_STATUS);

	uint16_t rpm;
	int stalled = 0;
	int fan;

	for (fan = 0; fan < fan_count; fan++) {
        *(mapped_fault + fan) = g_fan_parameter.fan_fault[fan];
        if (FAN_STATUS_FAULT == g_fan_parameter.fan_fault[fan]) {
            *(mapped_rpm + fan) = 0x0;
            continue;
        }
        
		if (fan_is_stalled(FAN_CH(fan))) {
			rpm = EC_FAN_SPEED_STALLED;
			stalled = 1;
			/*cprints(CC_PWM, "Fan %d stalled!", fan);*/
		} else {
			rpm = fan_get_rpm_actual(FAN_CH(fan));
		}
        
        *(mapped_rpm + fan) = rpm;
	}

	/*
	 * Issue warning.  As we have thermal shutdown
	 * protection, issuing warning here should be enough.
	 */
	if (stalled)
		host_set_single_event(EC_HOST_EVENT_THERMAL);
}
DECLARE_HOOK(HOOK_SECOND, update_fan_mapped, HOOK_PRIO_DEFAULT);

void pwm_fan_control(int fan, int enable)
{
	/* TODO(crosbug.com/p/23530): Still treating all fans as one. */
    set_thermal_control_enabled(fan, enable);
    fan_set_rpm_target(FAN_CH(fan), enable ?
        fan_percent_to_rpm(FAN_CH(fan), CONFIG_FAN_FAULT_CHECK_SPEED) : 0);
    set_enabled(fan, enable);
}

static void pwm_fan_stop(void)
{
   int fan;
	/*
	 * There is no need to cool CPU in S3 or S5. We currently don't
	 * have fans for battery or charger chip. Battery systems will
	 * control charge current based on its own temperature readings.
	 * Thus, we do not need to keep fans running in S3 or S5.
	 *
	 * Even with a fan on charging system, it's questionable to run
	 * a fan in S3/S5. Under an extreme heat condition, spinning a
	 * fan would create more heat as it draws current from a
	 * battery and heat would come from ambient air instead of CPU.
	 *
	 * Thermal control may be already disabled if DPTF is used.
	 */
    for (fan = 0; fan < fan_count; fan++) {
        pwm_fan_control(fan, 0); /* crosbug.com/p/8097 */
    }
    FanRebootFlag();
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwm_fan_stop, HOOK_PRIO_DEFAULT); /* s0-s3 */
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, pwm_fan_stop, HOOK_PRIO_DEFAULT); /* s0-s5/s4 */

uint8_t check_CPU_fan_fault(void)
{
    uint8_t ch;

    ch = PWM_CH_CPU_FAN;
    g_fan_parameter.fan_fault[ch] = 0x0;
    
    /* Fan in duty mode still want rpm_actual being updated. */
    g_fan_parameter.rpm_actual[ch] = fan_get_rpm_actual(ch);
    ccprints("PWM_CH_CPU_FAN***************%d", g_fan_parameter.rpm_actual[PWM_CH_CPU_FAN]);

    /* Upate fan fault status to ram */
    if (g_fan_parameter.rpm_actual[ch] < FAN_DUTY_50_RPM) {
        g_fan_parameter.fan_fault[ch] = FAN_STATUS_FAULT;
        ccprints("Check fan fault, chanel: %s is fault", ch ? "SYs fan" : "CPU fan");
    }
    return g_fan_parameter.fan_fault[ch];
}

uint8_t check_SYS_fan_fault(void)
{
    uint8_t ch;

    ch = PWM_CH_SYS_FAN;
    g_fan_parameter.fan_fault[ch] = 0x0;
    
    /* Fan in duty mode still want rpm_actual being updated. */
    g_fan_parameter.rpm_actual[ch] = fan_get_rpm_actual(ch);
    ccprints("PWM_CH_SYS_FAN***************%d", g_fan_parameter.rpm_actual[PWM_CH_SYS_FAN]);

    /* Upate fan fault status to ram */
    if (g_fan_parameter.rpm_actual[ch] < FAN_DUTY_50_RPM) {
        g_fan_parameter.fan_fault[ch] = FAN_STATUS_FAULT;
        ccprints("Check fan fault, chanel: %s is fault", ch ? "SYs fan" : "CPU fan");
    }

    return g_fan_parameter.fan_fault[ch];
}

static void pwm_fan_start(void)
{
    uint8_t ch;

    if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        return;
    }
    /* power on check fan fault */
    ccprints("S5->S0/S3 PWM fan start......");
    for (ch = 0; ch < fan_count; ch++) {
        g_fan_parameter.fan_fault[ch] = 0x0;
        /* s5 -> s0 check fan fault? start set fan duty 50%. */
        set_duty_cycle(ch, CONFIG_FAN_FAULT_CHECK_SPEED);
    }
}
DECLARE_HOOK(HOOK_CHIPSET_12V_ENABLE, pwm_fan_start, HOOK_PRIO_INIT_PWM);

void Reboot_pwm_fan_control(uint8_t ch)
{
    set_thermal_control_enabled(ch, 0x0); /* disable thermal control */
    fan_set_rpm_mode(ch, 0x1);  /* rpm mode */
    fan_set_rpm_target(ch, FAN_SET_RPM_TARGET);
    ccprints("reboot %s PWM fan start......", ch ? "SYs fan" : "CPU fan");
}

void thermal_control_service(void)
{
    uint8_t ch;
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_SYS_MISC1);

    if (!chipset_in_state(CHIPSET_STATE_ON)) {
        g_fanThermalStart = FAN_THERMAL_CPU_START | FAN_THERMAL_SYS_START;
        return;
    }

    for (ch = 0; ch < fan_count; ch++) {
        /* reboot check fan fault? start set fan 1200 rpm. */
        if (*mptr & EC_MEMMAP_SYSTEM_REBOOT) {
            if ((ch == PWM_CH_CPU_FAN) && (g_fanRebootFlag & FAN_REBOOT_CPU_CHECK)) {
                g_fan_parameter.fan_fault[ch] = 0x0;
                g_fanRebootFlag &= ~FAN_REBOOT_CPU_CHECK;
                Reboot_pwm_fan_control(ch);
            }
            if ((ch == PWM_CH_SYS_FAN) && (g_fanRebootFlag & FAN_REBOOT_SYS_CHECK)) {
                g_fan_parameter.fan_fault[ch] = 0x0;
                g_fanRebootFlag &= ~FAN_REBOOT_SYS_CHECK;
                Reboot_pwm_fan_control(ch);
            }
        }

        /* fan fault? yes, turn off fan */
        if(g_fan_parameter.fan_fault[ch] == FAN_STATUS_FAULT) {
            pwm_fan_control(ch, 0);
            continue;
        }

        /* enable thermal control */
        if (*mptr & EC_MEMMAP_ACPI_MODE) {
            if ((ch == 0) && (g_fanThermalStart & FAN_THERMAL_CPU_START)) {
                g_fanThermalStart &= ~FAN_THERMAL_CPU_START;
                set_thermal_control_enabled(ch, 0x01);
            }
            if ((ch == 1) && (g_fanThermalStart & FAN_THERMAL_SYS_START)) {
                g_fanThermalStart &= ~FAN_THERMAL_SYS_START;
                set_thermal_control_enabled(ch, 0x01);
            }
        }
    }
}
DECLARE_HOOK(HOOK_TICK, thermal_control_service, HOOK_PRIO_DEFAULT);

/* s0-s5 and System reboot will clear fan falut flag */
void acpiModeEnableClear(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_SYS_MISC1);

    *mptr &= ~(EC_MEMMAP_SYSTEM_REBOOT | EC_MEMMAP_SYSTEM_ENTER_S3
        | EC_MEMMAP_SYSTEM_ENTER_S4 | EC_MEMMAP_SYSTEM_ENTER_S5);

    g_fanRebootFlag = FAN_REBOOT_CPU_CHECK | FAN_REBOOT_SYS_CHECK;
}
DECLARE_HOOK(HOOK_CHIPSET_ACPI_MODE, acpiModeEnableClear, HOOK_PRIO_TEMP_SENSOR_DONE);

