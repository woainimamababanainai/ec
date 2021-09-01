/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "mock/tcpci_i2c_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tcpmv2_compliance.h"
#include "usb_tc_sm.h"

int partner_tx_id;

uint32_t rdo = RDO_FIXED(1, 500, 500, 0);
uint32_t pdo = PDO_FIXED(5000, 3000,
			 PDO_FIXED_DUAL_ROLE |
			 PDO_FIXED_DATA_SWAP |
			 PDO_FIXED_COMM_CAP);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_HOST_TCPC,
			.addr_flags = MOCK_TCPCI_I2C_ADDR_FLAGS,
		},
		.drv = &tcpci_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};


void mock_set_cc(enum mock_connect_result cr,
	enum mock_cc_state cc1, enum mock_cc_state cc2)
{
	mock_tcpci_set_reg(TCPC_REG_CC_STATUS,
		TCPC_REG_CC_STATUS_SET(cr, cc1, cc2));
}

void mock_set_role(int drp, enum tcpc_rp_value rp,
	enum tcpc_cc_pull cc1, enum tcpc_cc_pull cc2)
{
	mock_tcpci_set_reg(TCPC_REG_ROLE_CTRL,
		TCPC_REG_ROLE_CTRL_SET(drp, rp, cc1, cc2));
}

static int mock_alert_count;
void mock_set_alert(int alert)
{
	mock_tcpci_set_reg_bits(TCPC_REG_ALERT, alert);
	mock_alert_count = 1;
	schedule_deferred_pd_interrupt(PORT0);
}

uint16_t tcpc_get_alert_status(void)
{
	ccprints("mock_alert_count %d", mock_alert_count);
	if (mock_alert_count > 0) {
		mock_alert_count--;
		return PD_STATUS_TCPC_ALERT_0;
	}
	return 0;
}

bool vboot_allow_usb_pd(void)
{
	return 1;
}

int pd_check_vconn_swap(int port)
{
	return 1;
}

void board_reset_pd_mcu(void) {}


/*****************************************************************************
 * Partner utility functions
 */
static enum pd_data_role partner_data_role;
void partner_set_data_role(enum pd_data_role data_role)
{
	partner_data_role = data_role;
}
enum pd_data_role partner_get_data_role(void)
{
	return partner_data_role;
}

static enum pd_power_role partner_power_role;
void partner_set_power_role(enum pd_power_role power_role)
{
	partner_power_role = power_role;
}
enum pd_power_role partner_get_power_role(void)
{
	return partner_power_role;
}

static enum pd_rev_type partner_pd_rev;
void partner_set_pd_rev(enum pd_rev_type pd_rev)
{
	partner_pd_rev = pd_rev;
}
enum pd_rev_type partner_get_pd_rev(void)
{
	return partner_pd_rev;
}

void partner_send_msg(enum pd_msg_type sop,
		      uint16_t type,
		      uint16_t cnt,
		      uint16_t ext,
		      uint32_t *payload)
{
	uint16_t header;

	partner_tx_id &= 7;
	header = PD_HEADER(type,
			sop == PD_MSG_SOP ? partner_get_power_role()
			: PD_PLUG_FROM_CABLE,
			partner_get_data_role(),
			partner_tx_id,
			cnt,
			partner_get_pd_rev(),
			ext);

	mock_tcpci_receive(sop, header, payload);
	partner_tx_id++;
	mock_set_alert(TCPC_REG_ALERT_RX_STATUS);
}


/*****************************************************************************
 * TCPCI clean power up
 */
int tcpci_startup(void)
{
	/* Should be in low power mode before AP boots. */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_COMMAND),
		TCPC_REG_COMMAND_I2CIDLE, "%d");
	task_wait_event(10 * SECOND);

	hook_notify(HOOK_CHIPSET_STARTUP);
	task_wait_event(5 * MSEC);
	hook_notify(HOOK_CHIPSET_RESUME);

	task_wait_event(10 * SECOND);
	/* Should be in low power mode and DRP auto-toggling with AP in S0. */
	TEST_EQ((mock_tcpci_get_reg(TCPC_REG_ROLE_CTRL)
		 & TCPC_REG_ROLE_CTRL_DRP_MASK),
		TCPC_REG_ROLE_CTRL_DRP_MASK, "%d");
	/* TODO: check previous command was TCPC_REG_COMMAND_LOOK4CONNECTION */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_COMMAND),
		TCPC_REG_COMMAND_I2CIDLE, "%d");

	return EC_SUCCESS;
}

/*****************************************************************************
 * PROC.PD.E1. Bring-up procedure
 */
int proc_pd_e1(enum pd_data_role data_role)
{
	/*
	 * a) The test starts in a disconnected state.
	 */
	mock_tcpci_set_reg(TCPC_REG_EXT_STATUS, TCPC_REG_EXT_STATUS_SAFE0V);
	mock_set_alert(TCPC_REG_ALERT_EXT_STATUS);
	task_wait_event(10 * SECOND);
	TEST_EQ(pd_get_data_role(I2C_PORT_HOST_TCPC),
		PD_ROLE_DISCONNECTED, "%d");

	switch (data_role) {
	case PD_ROLE_UFP:
		/*
		 * b) The tester applies Rp (PD3=1.5A, PD2=3A) and waits for
		 * the UUT attachment.
		 */
		mock_set_cc(MOCK_CC_DUT_IS_SNK,
			    MOCK_CC_SNK_OPEN,
			    (partner_get_pd_rev() == PD_REV30 ?
			    MOCK_CC_SNK_RP_1_5 : MOCK_CC_SNK_RP_3_0));
		mock_set_alert(TCPC_REG_ALERT_CC_STATUS);
		task_wait_event(5 * MSEC);

		partner_set_data_role(PD_ROLE_DFP);
		partner_set_power_role(PD_ROLE_SOURCE);

		/*
		 * c) If Ra is detected, the tester applies Vconn.
		 */

		/*
		 * d) The tester applies Vbus and waits 50 ms.
		 */
		mock_tcpci_set_reg_bits(TCPC_REG_POWER_STATUS,
					TCPC_REG_POWER_STATUS_VBUS_PRES);

		mock_tcpci_clr_reg_bits(TCPC_REG_EXT_STATUS,
					TCPC_REG_EXT_STATUS_SAFE0V);
		mock_set_alert(TCPC_REG_ALERT_EXT_STATUS |
			       TCPC_REG_ALERT_POWER_STATUS);

		task_wait_event(50 * MSEC);

		/*
		 * e) The tester transmits Source Capabilities until reception
		 *    of GoodCrc for tNoResponse max (5.5s). The Source
		 *    Capabilities includes Fixed 5V 3A PDO.
		 */
		partner_send_msg(PD_MSG_SOP, PD_DATA_SOURCE_CAP, 1, 0, &pdo);

		/*
		 * f) The tester waits for the Request from the UUT for
		 *    tSenderResponse max (30 ms).
		 */
		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_REQUEST),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

		/*
		 * g) The tester sends Accept, and when Vbus is stable at the
		 *    target voltage, sends PS_RDY.
		 */
		partner_send_msg(PD_MSG_SOP, PD_CTRL_ACCEPT, 0, 0, NULL);
		task_wait_event(10 * MSEC);
		partner_send_msg(PD_MSG_SOP, PD_CTRL_PS_RDY, 0, 0, NULL);
		task_wait_event(1 * MSEC);
		break;

	case PD_ROLE_DFP:
		/*
		 * b) The tester applies Rd and waits for Vbus for tNoResponse
		 *    max (5.5 s).
		 */
		mock_set_cc(MOCK_CC_DUT_IS_SRC,
			    MOCK_CC_SRC_OPEN,
			    MOCK_CC_SRC_RD);
		mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

		partner_set_data_role(PD_ROLE_UFP);
		partner_set_power_role(PD_ROLE_SINK);

		/*
		 * c) The tester waits Source Capabilities for for tNoResponse
		 *    max (5.5 s).
		 */
		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP,
					      0,
					      PD_DATA_SOURCE_CAP),
			EC_SUCCESS, "%d");

		/*
		 * d) The tester replies GoodCrc on reception of the Source
		 *    Capabilities.
		 */
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);

		/*
		 * e) The tester requests 5V 0.5A.
		 */
		partner_send_msg(PD_MSG_SOP, PD_DATA_REQUEST, 1, 0, &rdo);

		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_ACCEPT, 0),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

		/*
		 * f) The tester waits PS_RDY for tPSSourceOn max (480 ms).
		 */
		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_PS_RDY, 0),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		break;

	case PD_ROLE_DISCONNECTED:
		break;
	}

	TEST_EQ(pd_get_data_role(I2C_PORT_HOST_TCPC), data_role, "%d");
	return EC_SUCCESS;
}

/*****************************************************************************
 * PROC.PD.E3. Wait to Start AMS for DFP(Source) UUT
 */
int proc_pd_e3(void)
{
	/*
	 * PROC.PD.E3. Wait to Start AMS for DFP(Source) UUT:
	 * a) The Tester keeps monitoring the Rp value and if the UUT doesn't
	 * set the value to SinkTXOK if it doesn't have anything to send in 1s,
	 * the test fails. During this period, the Tester replies any message
	 * sent from the UUT with a proper response.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP_PRIME, 0, PD_DATA_VENDOR_DEF),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	partner_send_msg(PD_MSG_SOP_PRIME, PD_CTRL_NOT_SUPPORTED, 0, 0, NULL);

	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_VENDOR_DEF),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	partner_send_msg(PD_MSG_SOP, PD_CTRL_NOT_SUPPORTED, 0, 0, NULL);

	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	partner_send_msg(PD_MSG_SOP, PD_DATA_SOURCE_CAP, 1, 0, &pdo);

	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_GET_SINK_CAP, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	partner_send_msg(PD_MSG_SOP, PD_DATA_SINK_CAP, 1, 0, &pdo);

	task_wait_event(1 * SECOND);
	TEST_EQ(tc_is_attached_src(PORT0), true, "%d");
	TEST_EQ(TCPC_REG_ROLE_CTRL_RP(mock_tcpci_get_reg(TCPC_REG_ROLE_CTRL)),
		SINK_TX_OK, "%d");

	task_wait_event(10 * SECOND);
	return EC_SUCCESS;
}
