/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "charger/sy21612.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"
#include "usb_bb.h"
#include "usb_pd.h"
#include "util.h"
#include "version.h"
#include "power.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_COMM_CAP | PDO_FIXED_USB_SUSPEND)


/* Voltage indexes for the PDOs */
enum volt_idx {
	PDO_IDX_5V  = 0,
	PDO_IDX_9V  = 1,
	/* TODO: add PPS support */
	PDO_IDX_COUNT
};

/* PDOs */
const uint32_t pd_src_pdo[] = {
	[PDO_IDX_5V]  = PDO_FIXED(5000,  2000, PDO_FIXED_FLAGS),
    /* For the other PDOs, If Bits 31..30 are 00b, Bits 29..22 are set to 0*/
	[PDO_IDX_9V]  = PDO_FIXED(9000,  2000, 0),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
BUILD_ASSERT(ARRAY_SIZE(pd_src_pdo) == PDO_IDX_COUNT);

/* S3 PDOs */
const uint32_t pd_src_pdo_s3[] = {
    [PDO_IDX_5V]  = PDO_FIXED(5000,  2000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt_s3 = ARRAY_SIZE(pd_src_pdo_s3);

int pd_get_board_pdo(const uint32_t **src_pdo)
{
    if(POWER_S0 == power_get_state()) {
        *src_pdo = pd_src_pdo;
        return pd_src_pdo_cnt;
    } else {
        *src_pdo = pd_src_pdo_s3;
        return pd_src_pdo_cnt_s3;
    }
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* No battery, nothing to do */
	CPRINTS("pd_set_input_current_limit %d, %d",
			max_ma, supply_voltage);
	return;
}

int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	CPRINTS("pd_is_valid_input_voltage %d", mv);
	return 1;
}

void pd_transition_voltage(int idx)
{
	/* TODO: discharge, PPS */
	switch (idx - 1) {
	case PDO_IDX_9V:
		CPRINTS("pd_transition_voltage to 9v");
		board_set_usb_output_voltage(9000);
		break;
	case PDO_IDX_5V:
	default:
		CPRINTS("pd_transition_volatage to 5v");
		board_set_usb_output_voltage(5000);
		break;
	}
}

int pd_set_power_supply_ready(int port)
{
	CPRINTS("pd_set_power_supply_read, 5v");
	board_set_usb_output_voltage(5000);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	CPRINTS("pd_power_supply_reset, shutdown voltage");
	board_set_usb_output_voltage(-1);
}

/*
int pd_snk_is_vbus_provided(int port)
{
	return 1;
}
*/

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* Always refuse power swap */
	return 0;
}

int pd_check_data_swap(int port,
		       enum pd_data_role data_role)
{
	/* We can swap to UFP */
	return 0;
}

void pd_execute_data_swap(int port,
			  enum pd_data_role data_role)
{
	/* do nothing */
}

void pd_check_pr_role(int port,
		      enum pd_power_role pr_role,
		      int flags)
{
    CPRINTS("pd_check_pr_role, pr_role(%s), flags=(0x%X)",
        (PD_ROLE_SINK==pr_role)?("Sink"):("Source"), flags);
}

/* TODO: to be checked */
void pd_check_dr_role(int port,
		      enum pd_data_role dr_role,
		      int flags)
{
    CPRINTS("pd_check_dr_role, dr_role(%s), flags(0x%X)",
        (PD_ROLE_UFP==dr_role)?("UFP"):("DFP"), flags);

    /* If UFP(USB Device), try to switch to DFP(USB Host) */
    if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_UFP)
		pd_request_data_swap(port);
}

/* Just for compile when ALT_MODE not supprot */
uint16_t pd_get_identity_vid(int port)
{
	return 0x1234;
}

