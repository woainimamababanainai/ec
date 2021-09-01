#ifndef __POWER_ROCKETLAKE_H
#define __POWER_ROCKETLAKE_H

enum power_signal {
    X86_SLP_SUS_N,      /* PCH  -> SLP_SUS_L */
    SYSTEM_ALW_PG,      /* ALW power googd */
    X86_SLP_S3_N,       /* PCH  -> SLP_S3_L */
    X86_SLP_S4_N,       /* PCH  -> SLP_S4_L */
    /* X86_SLP_S5_N, */ /* PCH  -> SLP_S5_L */
    ATX_PG,
    VCORE_EN,
    VRMPWRGD,
    /* Number of X86 signals */
    POWER_SIGNAL_COUNT
};

/* +3.3V_SB_PGOOD for G3, DSW PWROK enable delay 10ms */
/* #define IN_3V3_SB_PGOOD     POWER_SIGNAL_MASK(V3V3_SB_PGOOD) */

/* EC receives SLP_SUS#, and send EC_1.8V_AUX_EN to platform */
/* #define IN_SLP_SUS_L     POWER_SIGNAL_MASK(SLP_SUS_L) */

#define IN_SYSTEM_ALW_PG    POWER_SIGNAL_MASK(SYSTEM_ALW_PG)
#define IN_ATX_PG       POWER_SIGNAL_MASK(ATX_PG)
#define IN_VCORE_EN     POWER_SIGNAL_MASK(VCORE_EN)
#define IN_VRMPWRGD     POWER_SIGNAL_MASK(VRMPWRGD)
#define IN_SLP_SUS_N        POWER_SIGNAL_MASK(X86_SLP_SUS_N)
#define IN_SLP_S3_N     POWER_SIGNAL_MASK(X86_SLP_S3_N)
#define IN_SLP_S4_N     POWER_SIGNAL_MASK(X86_SLP_S4_N)

#define IN_ALL_PM_SLP_DEASSERTED (IN_SLP_S3_N | IN_SLP_S4_N)

/* All core and non-core power */
#define IN_PGOOD_ALL_CORE (IN_ATX_PG | IN_VCORE_EN | IN_VRMPWRGD)

/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON (IN_SYSTEM_ALW_PG)

/* Rails required for S5 */
#define IN_PGOOD_S5 (IN_PGOOD_ALWAYS_ON)

/* Rails required for S3 */
#define IN_PGOOD_S3 (IN_PGOOD_ALWAYS_ON)

/* Rails required for S0 */
#define IN_PGOOD_S0 (IN_PGOOD_ALL_CORE | IN_PGOOD_ALWAYS_ON)

/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_S0 | IN_ALL_PM_SLP_DEASSERTED)

void update_cause_flag(uint16_t value);
void set_abnormal_shutdown(uint8_t value);
uint8_t get_abnormal_shutdown(void);

#endif
