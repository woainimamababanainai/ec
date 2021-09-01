/*
 *Copyright 2021 The bitland Authors. All rights reserved.
 *
 * software watchdog for BLD.
 */

#ifndef __CROS_EC_WMI_H
#define __CROS_EC_WMI_H

/* Startup phase code*/
enum startup_phase_code {
    POST_PHASE0 = 0,
    POST_PHASE1,
    POST_PHASE2,
    POST_PHASE3,
    POST_PHASE4,
    POST_PHASE5,
    POST_PHASE6,

    BIOS_POST = 0x32,
    EXIT_BIOS_TO_OS = 0x32,
    S3_SLEEP_BIOS = 0x32,
    S3_RESUME_BIOS = 0x32,
    S4_SLEEP_BIOS = 0x32,
    S5_SLEEP_BIOS = 0x32,
    OS_RESET_BIOS = 0x32
};

extern void post_last_code(int postcode);
extern void post_last_code_s(void);


#endif

