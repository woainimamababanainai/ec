#ifndef __POWER_RENOIR_H
#define __POWER_RENOIR_H

enum power_signal {
	SYSTEM_ALW_PG,		/* ALW power googd */
	X86_SLP_S3_N,		/* PCH  -> SLP_S3_L */
	X86_SLP_S5_N,		/* PCH  -> SLP_S5_L */
	ATX_PG,
	VCORE_EN,
	VRMPWRGD,
	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

#define IN_SYSTEM_ALW_PG	POWER_SIGNAL_MASK(SYSTEM_ALW_PG)
#define IN_ATX_PG		POWER_SIGNAL_MASK(ATX_PG)
#define IN_VCORE_EN		POWER_SIGNAL_MASK(VCORE_EN)
#define IN_VRMPWRGD		POWER_SIGNAL_MASK(VRMPWRGD)
#define IN_SLP_S3_N		POWER_SIGNAL_MASK(X86_SLP_S3_N)
#define IN_SLP_S5_N		POWER_SIGNAL_MASK(X86_SLP_S5_N)

#define IN_ALL_PM_SLP_DEASSERTED (IN_SLP_S3_N | IN_SLP_S5_N)

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

void set_abnormal_shutdown(uint8_t value);
uint8_t get_abnormal_shutdown(void);
void update_cause_flag(uint16_t value);

#endif
