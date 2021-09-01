/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button state machine for x86 platforms */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "flash.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

/*
 * x86 chipsets have a hardware timer on the power button input which causes
 * them to reset when the button is pressed for more than 4 seconds.  This is
 * problematic for Chrome OS, which needs more time than that to transition
 * through the lock and logout screens.  So when the system is on, we need to
 * stretch the power button signal so that the chipset will hard-reboot after 8
 * seconds instead of 4.
 *
 * When the button is pressed, we initially send a short pulse (t0); this
 * allows the chipset to process its initial power button interrupt and do
 * things like wake from suspend.  We then deassert the power button signal to
 * the chipset for (t1 = 4 sec - t0), which keeps the chipset from starting its
 * hard reset timer.  If the power button is still pressed after this period,
 * we again assert the power button signal for the remainder of the press
 * duration.  Since (t0+t1) causes a 4-second offset, the hard reset timeout in
 * the chipset triggers after 8 seconds as desired.
 *
 *   PWRBTN#   ---                      ----
 *     to EC     |______________________|
 *
 *
 *   PWRBTN#   ---  ---------           ----
 *    to PCH     |__|       |___________|
 *                t0    t1    held down
 *
 *   scan code   |                      |
 *    to host    v                      v
 *     @S0   make code             break code
 */
#define PWRBTN_DELAY_T0    (32 * MSEC)  /* 32ms (PCH requires >16ms) */
/* #define PWRBTN_DELAY_T1    (4 * SECOND - PWRBTN_DELAY_T0)*/  /* 4 secs - t0 */

/* modify 8sec force shutdown to 4sec*/
#define PWRBTN_DELAY_T1    (64 * MSEC - PWRBTN_DELAY_T0)

#define PWRBTN_DELAY_T2    (3700*MSEC)
#define PWRBTN_DELAY_T3    (6300*MSEC)

/*
 * Length of time to stretch initial power button press to give chipset a
 * chance to wake up (~100ms) and react to the press (~16ms).  Also used as
 * pulse length for simulated power button presses when the system is off.
 */
#define PWRBTN_INITIAL_US  (200 * MSEC)


static enum power_button_state pwrbtn_state = PWRBTN_STATE_IDLE;

static const char * const state_names[] = {
    "idle",
    "pressed",
    "t0",
    "t1",
    "held_4s",
    "held_10s",
    "lid-open",
    "lan-wake",
    "released",
    "eat-release",
    "init-on",
    "recovery",
    "was-off",
};

#ifdef NPCX_FAMILY_DT01
/* 0xaa: powerbtn press 4s */
uint8_t powerbtn_press_4s_flag;
#endif

/*
 * Time for next state transition of power button state machine, or 0 if the
 * state doesn't have a timeout.
 */
static uint64_t tnext_state;

/*
 * Record the time when power button task starts. It can be used by any code
 * path that needs to compare the current time with power button task start time
 * to identify any timeouts e.g. PB state machine checks current time to
 * identify if it should wait more for charger and battery to be initialized. In
 * case of recovery using buttons (where the user could be holding the buttons
 * for >30seconds), it is not right to compare current time with the time when
 * EC was reset since the tasks would not have started. Hence, this variable is
 * being added to record the time at which power button task starts.
 */
static uint64_t tpb_task_start;

/*
 * Determines whether to execute power button pulse (t0 stage)
 */
static int power_button_pulse_enabled = 1;

static void set_pwrbtn_to_pch(int high, int init)
{
	/*
	 * If the battery is discharging and low enough we'd shut down the
	 * system, don't press the power button. Also, don't press the
	 * power button if the battery is charging but the battery level
	 * is too low.
	 */
#ifdef CONFIG_CHARGER
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) && !high &&
	   (charge_want_shutdown() || charge_prevent_power_on(!init))) {
		CPRINTS("PB PCH pwrbtn ignored due to battery level");
		high = 1;
	}
#endif
	if (IS_ENABLED(CONFIG_POWER_BUTTON_TO_PCH_CUSTOM))
		board_pwrbtn_to_pch(high);
	else {
        gpio_set_level(GPIO_PCH_PWRBTN_L, high);
        CPRINTS("PB PCH pwrbtn=%s", high ? "HIGH" : "LOW");
    }
}

void power_button_pch_press(void)
{
	CPRINTS("PB PCH force press");

	/* Assert power button signal to PCH */
	if (!power_button_is_pressed())
		set_pwrbtn_to_pch(0, 0);
}

void power_button_pch_release(void)
{
	CPRINTS("PB PCH force release");

	/* Deassert power button signal to PCH */
	set_pwrbtn_to_pch(1, 0);

	/*
	 * If power button is actually pressed, eat the next release so we
	 * don't send an extra release.
	 */
	if (power_button_is_pressed())
		pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
	else
		pwrbtn_state = PWRBTN_STATE_IDLE;
}

void power_button_pch_pulse(enum power_button_state state)
{
	CPRINTS("PB PCH pulse");

	chipset_exit_hard_off();
	set_pwrbtn_to_pch(0, 0);
	pwrbtn_state = state;
	tnext_state = get_time().val + PWRBTN_INITIAL_US;
	task_wake(TASK_ID_POWERBTN);
}

#ifdef CONFIG_POWER_BUTTON_LOCK_HOST
/*
 * Lock power button form host.
 */
uint8_t get_power_button_lock_flag(void)
{
    uint8_t memValue = 0;
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);

    memValue = *mptr;
    CPRINTS("power button is %s", memValue ? "lock" : "not lock");
    return memValue;
}
#endif

/**
 * Handle debounced power button down.
 */
static void power_button_pressed(uint64_t tnow)
{
	CPRINTS("PB pressed");
	pwrbtn_state = PWRBTN_STATE_PRESSED;
	tnext_state = tnow;
}

/**
 * Handle debounced power button up.
 */
static void power_button_released(uint64_t tnow)
{
	CPRINTS("PB released");
	pwrbtn_state = PWRBTN_STATE_RELEASED;
	tnext_state = tnow;
}

/*
 * Set initial power on auto control
 */
static void auto_power_on_control(void)
{
    uint8_t ac_recovery_state;
    uint8_t power_last_state;
    uint8_t mfg_mode;

#ifdef CONFIG_SYSTEM_RESET_DELAY
        system_get_bbram(SYSTEM_BBRAM_IDX_SYSTEM_RESET, &mfg_mode);
        CPRINTS("==============SYSTEM_BBRAM_IDX_SYSTEM_RESET = %X", mfg_mode);

        if (mfg_mode == EC_GENERAL_SIGNES) {
            mfg_mode = 0;
            system_get_bbram(SYSTEM_BBRAM_IDX_EC_RESET, &mfg_mode);
            CPRINTS("==============SYSTEM_BBRAM_IDX_EC_RESET = %X", mfg_mode);
            if (mfg_mode == EC_GENERAL_SIGNES) {
                pwrbtn_state = PWRBTN_STATE_IDLE;    /* power off */
                system_set_bbram(SYSTEM_BBRAM_IDX_EC_RESET, 0x00);
            } else {
               pwrbtn_state = PWRBTN_STATE_INIT_ON;    /* power on */
            }
            system_set_bbram(SYSTEM_BBRAM_IDX_SYSTEM_RESET, 0x00);
            return;
        }
#endif

        mfg_mode = mfg_data_read(MFG_MODE_OFFSET);
        ac_recovery_state = mfg_data_read(MFG_AC_RECOVERY_OFFSET);
        power_last_state = mfg_data_read(MFG_POWER_LAST_STATE_OFFSET);

        CPRINTS("MFG Mode=%X, AC Recovery state=%X, Last state=%X",
                mfg_mode, ac_recovery_state, power_last_state);
    
        if (0xFF == mfg_mode) { /* 0xFF:MFG mode, 0xBE:Release Mode*/
            pwrbtn_state = PWRBTN_STATE_INIT_ON;    /* power on */
            wakeup_cause_record(LOG_ID_WAKEUP_0xFC);
        } else if (0x01 == ac_recovery_state) {
            pwrbtn_state = PWRBTN_STATE_INIT_ON;    /* power on */
            wakeup_cause_record(LOG_ID_WAKEUP_0xFC);
        } else if (0x02 == ac_recovery_state) {
            pwrbtn_state = PWRBTN_STATE_IDLE;       /* power off */
        } else if (0x03 == ac_recovery_state) {
            if(0x55 == power_last_state) {          /* previous state is power off */
                pwrbtn_state = PWRBTN_STATE_IDLE;
            } else {                                /* previous state is power on */
                wakeup_cause_record(LOG_ID_WAKEUP_0xFC);
                pwrbtn_state = PWRBTN_STATE_INIT_ON;
            }
        } else {
            pwrbtn_state = PWRBTN_STATE_INIT_ON;
        }
}

/**
 * Set initial power button state.
 */
static void set_initial_pwrbtn_state(void)
{
    uint32_t reset_flags = system_get_reset_flags();

	if (system_jumped_to_this_image() &&
	    chipset_in_state(CHIPSET_STATE_ON)) {
		/*
		 * Jumped to this image while the chipset was already on, so
		 * simply reflect the actual power button state unless power
		 * button pulse is disabled. If power button SMI pulse is
		 * enabled, then it should be honored, else setting power
		 * button to PCH could lead to x86 platform shutting down. If
		 * power button is still held by the time control reaches
		 * state_machine(), it would take the appropriate action there.
		 */
		if (power_button_is_pressed() && power_button_pulse_enabled) {
			CPRINTS("PB init-jumped-held");
			set_pwrbtn_to_pch(0, 0);
		} else {
			CPRINTS("PB init-jumped");
		}
		return;
	} else if ((reset_flags & EC_RESET_FLAG_AP_OFF) ||
		   (keyboard_scan_get_boot_keys() == BOOT_KEY_DOWN_ARROW)) {
		/* Clear AP_OFF so that it won't be carried over to RW. */
		system_clear_reset_flags(EC_RESET_FLAG_AP_OFF);
		/*
		 * Reset triggered by keyboard-controlled reset, and down-arrow
		 * was held down.  Or reset flags request AP off.
		 *
		 * Leave the main processor off.  This is a fail-safe
		 * combination for debugging failures booting the main
		 * processor.
		 *
		 * Don't let the PCH see that the power button was pressed.
		 * Otherwise, it might power on.
		 */
		CPRINTS("PB init-off");
		power_button_pch_release();
		return;
	} else if (reset_flags & EC_RESET_FLAG_AP_IDLE) {
		system_clear_reset_flags(EC_RESET_FLAG_AP_IDLE);
		pwrbtn_state = PWRBTN_STATE_IDLE;
		CPRINTS("PB idle");
		return;
	}

    /* Set initial power on auto control */
    auto_power_on_control();

    CPRINTS("PB %s", pwrbtn_state == PWRBTN_STATE_INIT_ON ? "init-on" : "idle");
}

/**
 * Power button state machine.
 *
 * @param tnow		Current time from usec counter
 */
static void state_machine(uint64_t tnow)
{
	/* Not the time to move onto next state */
	if (tnow < tnext_state)
		return;

	/* States last forever unless otherwise specified */
	tnext_state = 0;

	switch (pwrbtn_state) {
	case PWRBTN_STATE_PRESSED:
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/*
			 * Chipset is off, so wake the chipset and send it a
			 * long enough pulse to wake up.  After that we'll
			 * reflect the true power button state.  If we don't
			 * stretch the pulse here, the user may release the
			 * power button before the chipset finishes waking from
			 * hard off state.
			 */
			chipset_exit_hard_off();
			tnext_state = tnow + PWRBTN_INITIAL_US;
			pwrbtn_state = PWRBTN_STATE_WAS_OFF;
			set_pwrbtn_to_pch(0, 0);
		} else {
			if (power_button_pulse_enabled) {
				/* Chipset is on, so send the chipset a pulse */
				tnext_state = tnow + PWRBTN_DELAY_T0;
				pwrbtn_state = PWRBTN_STATE_T1;
				set_pwrbtn_to_pch(0, 0);
			} else {
				tnext_state = tnow + PWRBTN_DELAY_T1;
				pwrbtn_state = PWRBTN_STATE_T1;
			}
		}
		break;
	case PWRBTN_STATE_T0:
		tnext_state = tnow + PWRBTN_DELAY_T1;
		pwrbtn_state = PWRBTN_STATE_T1;
		set_pwrbtn_to_pch(1, 0);
		break;
	case PWRBTN_STATE_T1:
		/*
		 * If the chipset is already off, don't tell it the power
		 * button is down; it'll just cause the chipset to turn on
		 * again.
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			CPRINTS("PB chipset already off");
		else
			set_pwrbtn_to_pch(0, 0);

        tnext_state = tnow + PWRBTN_DELAY_T2;
		pwrbtn_state = PWRBTN_STATE_HELD;
		break;
	case PWRBTN_STATE_RELEASED:
	case PWRBTN_STATE_LID_OPEN:
		set_pwrbtn_to_pch(1, 0);
		pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
    case PWRBTN_STATE_LAN_WAKE:
        set_pwrbtn_to_pch(1, 0);
        pwrbtn_state = PWRBTN_STATE_IDLE;
        break;
	case PWRBTN_STATE_INIT_ON:

		/*
		 * Before attempting to power the system on, we need to allow
		 * time for charger, battery and USB-C PD initialization to be
		 * ready to supply sufficient power. Check every 100
		 * milliseconds, and give up CONFIG_POWER_BUTTON_INIT_TIMEOUT
		 * seconds after the PB task was started. Here, it is
		 * important to check the current time against PB task start
		 * time to prevent unnecessary timeouts happening in recovery
		 * case where the tasks could start as late as 30 seconds
		 * after EC reset.
		 */

		if (!IS_ENABLED(CONFIG_CHARGER) || charge_prevent_power_on(0)) {
			if (tnow >
				(tpb_task_start +
				 CONFIG_POWER_BUTTON_INIT_TIMEOUT * SECOND)) {
				pwrbtn_state = PWRBTN_STATE_IDLE;
				break;
			}

			if (IS_ENABLED(CONFIG_CHARGER)) {
				tnext_state = tnow + 100 * MSEC;
				break;
			}
		}

		/*
		 * Power the system on if possible.  Gating due to insufficient
		 * battery is handled inside set_pwrbtn_to_pch().
		 */
		chipset_exit_hard_off();
#ifdef CONFIG_DELAY_DSW_PWROK_TO_PWRBTN
		/* Check if power button is ready. If not, we'll come back. */
		if (get_time().val - get_time_dsw_pwrok() <
				CONFIG_DSW_PWROK_TO_PWRBTN_US) {
			tnext_state = get_time_dsw_pwrok() +
					CONFIG_DSW_PWROK_TO_PWRBTN_US;
			break;
		}
#endif

		set_pwrbtn_to_pch(0, 1);
		tnext_state = get_time().val + PWRBTN_INITIAL_US;
		pwrbtn_state = PWRBTN_STATE_BOOT_KB_RESET;
		break;

	case PWRBTN_STATE_BOOT_KB_RESET:
		/* Initial forced pulse is done.  Ignore the actual power
		 * button until it's released, so that holding down the
		 * recovery combination doesn't cause the chipset to shut back
		 * down. */
		set_pwrbtn_to_pch(1, 0);
		if (power_button_is_pressed())
			pwrbtn_state = PWRBTN_STATE_EAT_RELEASE;
		else
			pwrbtn_state = PWRBTN_STATE_IDLE;
		break;
	case PWRBTN_STATE_WAS_OFF:
		/* Done stretching initial power button signal, so show the
		 * true power button state to the PCH. */
		if (power_button_is_pressed()) {
			/* User is still holding the power button */
			pwrbtn_state = PWRBTN_STATE_HELD;
		} else {
			/* Stop stretching the power button press */
			power_button_released(tnow);
		}
		break;
	case PWRBTN_STATE_IDLE:
        break;
	case PWRBTN_STATE_HELD:
        if (tnow > tnext_state) {
            #ifdef NPCX_FAMILY_DT01
            powerbtn_press_4s_flag = 0xaa;
            #endif
            shutdown_cause_record(LOG_ID_SHUTDOWN_0x06);
            tnext_state = tnow + PWRBTN_DELAY_T3;
		    pwrbtn_state = PWRBTN_STATE_HELD_1;
        }
        break;
    case PWRBTN_STATE_HELD_1:
        if (tnow > tnext_state) {
            shutdown_cause_record(LOG_ID_SHUTDOWN_0x07);
            system_reset(SYSTEM_RESET_10_SHUT_DOWN);
        }
	case PWRBTN_STATE_EAT_RELEASE:
		/* Do nothing */
		break;
	}
}

void power_button_task(void *u)
{
	uint64_t t;
	uint64_t tsleep;

	/*
	 * Record the time when the task starts so that the state machine can
	 * use this to identify any timeouts.
	 */
	tpb_task_start = get_time().val;

	while (1) {
		t = get_time().val;

		/* Update state machine */
		CPRINTS("PB task %d = %s", pwrbtn_state,
			state_names[pwrbtn_state]);

		state_machine(t);

		/* Sleep until our next timeout */
		tsleep = -1;
		if (tnext_state && tnext_state < tsleep)
			tsleep = tnext_state;
		t = get_time().val;
		if (tsleep > t) {
			unsigned d = tsleep == -1 ? -1 : (unsigned)(tsleep - t);
			/*
			 * (Yes, the conversion from uint64_t to unsigned could
			 * theoretically overflow if we wanted to sleep for
			 * more than 2^32 us, but our timeouts are small enough
			 * that can't happen - and even if it did, we'd just go
			 * back to sleep after deciding that we woke up too
			 * early.)
			 */
			CPRINTS("PB task %d = %s, wait %d", pwrbtn_state,
			state_names[pwrbtn_state], d);
			task_wait_event(d);
		}
	}
}

/*****************************************************************************/
/* Hooks */

static void powerbtn_x86_init(void)
{
	set_initial_pwrbtn_state();
}
DECLARE_HOOK(HOOK_INIT, powerbtn_x86_init, HOOK_PRIO_DEFAULT+1);

#ifdef CONFIG_LID_SWITCH
/**
 * Handle switch changes based on lid event.
 */
static void powerbtn_x86_lid_change(void)
{
	/* If chipset is off, pulse the power button on lid open to wake it. */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF)
	    && pwrbtn_state != PWRBTN_STATE_INIT_ON)
		power_button_pch_pulse(PWRBTN_STATE_LID_OPEN);
}
DECLARE_HOOK(HOOK_LID_CHANGE, powerbtn_x86_lid_change, HOOK_PRIO_DEFAULT);
#endif

#ifdef CONFIG_LAN_WAKE_SWITCH
/*
 * Handle switch changes based on lan/wlan wake event.
 */
static void powerbtn_x86_lan_wake(void)
{
    /* If chipset is Suspend (S3), pulse the power button on lan/wlan to wake it. */
    if (lan_is_wake() && pwrbtn_state != PWRBTN_STATE_INIT_ON
        && (chipset_in_state(CHIPSET_STATE_SUSPEND) 
        || chipset_in_state(CHIPSET_STATE_SOFT_OFF))) {
        power_button_pch_pulse(PWRBTN_STATE_LAN_WAKE);
        CPRINTS("powerbtn x86 lan/wlan wake up, when system is s0 state.");
    }
}
DECLARE_HOOK(HOOK_LAN_WAKE, powerbtn_x86_lan_wake, HOOK_PRIO_DEFAULT);
#endif

/**
 * Handle debounced power button changing state.
 */
static void powerbtn_x86_changed(void)
{
    if (pwrbtn_state == PWRBTN_STATE_BOOT_KB_RESET ||
        pwrbtn_state == PWRBTN_STATE_INIT_ON ||
        pwrbtn_state == PWRBTN_STATE_LID_OPEN ||
        pwrbtn_state == PWRBTN_STATE_LAN_WAKE ||
        pwrbtn_state == PWRBTN_STATE_WAS_OFF) {
		/* Ignore all power button changes during an initial pulse */
		CPRINTS("PB ignoring change");
		return;
	}

	if (power_button_is_pressed()) {
		/* Power button pressed */
		power_button_pressed(get_time().val);
	} else {
		/* Power button released */
		if (pwrbtn_state == PWRBTN_STATE_EAT_RELEASE) {
			/*
			 * Ignore the first power button release if we already
			 * told the PCH the power button was released.
			 */
			CPRINTS("PB ignoring release");
			pwrbtn_state = PWRBTN_STATE_IDLE;
			return;
		}

		power_button_released(get_time().val);
	}

	/* Wake the power button task */
	task_wake(TASK_ID_POWERBTN);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, powerbtn_x86_changed, HOOK_PRIO_DEFAULT);

/**
 * Handle configuring the power button behavior through a host command
 */
static enum ec_status hc_config_powerbtn_x86(struct host_cmd_handler_args *args)
{
	const struct ec_params_config_power_button *p = args->params;

	power_button_pulse_enabled =
		!!(p->flags & EC_POWER_BUTTON_ENABLE_PULSE);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CONFIG_POWER_BUTTON, hc_config_powerbtn_x86,
		     EC_VER_MASK(0));


/*
 * Currently, the only reason why we disable power button pulse is to allow
 * detachable menu on AP to use power button for selection purpose without
 * triggering SMI. Thus, re-enable the pulse any time there is a chipset
 * state transition event.
 */
static void power_button_pulse_setting_reset(void)
{
	power_button_pulse_enabled = 1;
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, power_button_pulse_setting_reset,
	     HOOK_PRIO_DEFAULT);

#define POWER_BUTTON_SYSJUMP_TAG		0x5042 /* PB */
#define POWER_BUTTON_HOOK_VERSION		1

static void power_button_pulse_setting_restore_state(void)
{
	const int *state;
	int version, size;

	state = (const int *)system_get_jump_tag(POWER_BUTTON_SYSJUMP_TAG,
						 &version, &size);

	if (state && (version == POWER_BUTTON_HOOK_VERSION) &&
	    (size == sizeof(power_button_pulse_enabled)))
		power_button_pulse_enabled = *state;
}
DECLARE_HOOK(HOOK_INIT, power_button_pulse_setting_restore_state,
	     HOOK_PRIO_INIT_POWER_BUTTON + 1);

static void power_button_pulse_setting_preserve_state(void)
{
	system_add_jump_tag(POWER_BUTTON_SYSJUMP_TAG,
			    POWER_BUTTON_HOOK_VERSION,
			    sizeof(power_button_pulse_enabled),
			    &power_button_pulse_enabled);
}
DECLARE_HOOK(HOOK_SYSJUMP, power_button_pulse_setting_preserve_state,
	     HOOK_PRIO_DEFAULT);
