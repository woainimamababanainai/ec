/*
 *Copyright 2021 The bitland Authors. All rights reserved.
 *
 * software watchdog for BLD.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "thermal.h"

static uint8_t cpu_model; /* 0x01:i3, 0x02:i5, 0x03:i7 */

#define UMA_SYS_FAN_START_TEMP  36
#define UMA_CPU_FAN_START_TEMP  37
#define GFX_SYS_FAN_START_TEMP  39
#define GFX_CPU_FAN_START_TEMP  40

#define CPU_DTS_PROCHOT_TEMP   98
#define TEMP_MULTIPLE  100 /* TEMP_AMBIENCE_NTC */

enum thermal_fan_mode {
    UMA_THERMAL_SYS_FAN = 0,
    UMA_THERMAL_CPU_FAN,
    GFX_THERMAL_SYS_FAN,
    GFX_THERMAL_CPU_FAN, 
};

enum thermal_level {
    LEVEL1 = 0,
    LEVEL2,
    LEVEL3,
    LEVEL4,
    LEVEL5,
    LEVEL6,
    LEVEL7,
    LEVEL_COUNT
};

struct thermal_params_s {
    uint8_t   level;
    int   rpm_target;
    int   time;
    int   cpuDts;         /* name = "CPU DTS" */
    int   ambiencerNtc;   /* name = "Ambiencer NTC" */
    int   ssd1Ntc;        /* name = "SSD1 NTC" */
    int   pcie16Ntc;      /* name = "PCIE16 NTC" */
    int   cpuNtc;         /* name = "CPU NTC" */
    int   memoryNtc;      /* name = "Memory NTC" */
    int   ssd2Ntc;        /* name = "SSD2 NTC" */
};

struct thermal_params_s g_fanLevel[CONFIG_FANS] = {0};
struct thermal_params_s g_fanRPM[CONFIG_FANS] ={0};
struct thermal_params_s g_fanProtect[TEMP_SENSOR_COUNT] ={0};

struct thermal_level_ags {
    uint8_t    level;
    int        RPM;
    uint16_t   HowTri;
    uint16_t   lowTri;
};

struct thermal_level_s {
    const char *name;
    uint8_t num_pairs;	/* Number of data pairs. */
    const struct thermal_level_ags *data;
};

/*
 * fan table for cpu model is i3
 */

/* UMP sys fan sensor SSD1 NTC*/
const struct thermal_level_ags i3_uma_thermal_sys_fan_ssd1_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      40,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      52,   39},
    {2,     1200,     55,   51},
    {3,     1500,     58,   54},
    {4,     1800,     61,   57},
    {5,     2000,     65,   60},
    {6,     2800,     65,   64}
};
const struct thermal_level_s t_i3_uma_thermal_sys_fan_ssd1_ntc = {
    .name = "SSD1 NTC",
    .num_pairs = ARRAY_SIZE(i3_uma_thermal_sys_fan_ssd1_ntc),
    .data = i3_uma_thermal_sys_fan_ssd1_ntc,
};

/* UMP sys fan sensor SSD2 NTC*/
const struct thermal_level_ags i3_uma_thermal_sys_fan_ssd2_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      40,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      52,   39},
    {2,     1200,     55,   51},
    {3,     1500,     58,   54},
    {4,     1800,     61,   57},
    {5,     2000,     65,   60},
    {6,     2800,     65,   64}
};
const struct thermal_level_s t_i3_uma_thermal_sys_fan_ssd2_ntc = {
    .name = "SSD2 NTC",
    .num_pairs = ARRAY_SIZE(i3_uma_thermal_sys_fan_ssd2_ntc),
    .data = i3_uma_thermal_sys_fan_ssd2_ntc,
};

/* UMP sys fan sensor memory NTC*/
const struct thermal_level_ags i3_uma_thermal_sys_fan_memory_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      39,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      55,   38},
    {2,     1200,     58,   54},
    {3,     1500,     60,   57},
    {4,     1800,     62,   59},
    {5,     2000,     70,   61},
    {6,     2800,     70,   69}
};
const struct thermal_level_s t_i3_uma_thermal_sys_fan_memory_ntc = {
    .name = "Memory NTC",
    .num_pairs = ARRAY_SIZE(i3_uma_thermal_sys_fan_memory_ntc),
    .data = i3_uma_thermal_sys_fan_memory_ntc,
};

/* UMP CPU fan sensor CPU DTS*/
const struct thermal_level_ags  i3_uma_thermal_cpu_fan_cpu_dts[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      35,   UMA_CPU_FAN_START_TEMP},
    {1,     1000,     65,   33},
    {2,     1200,     80,   63},
    {3,     1500,     84,   78},
    {4,     1700,     91,   82},
    {5,     1900,     97,   89},
    {6,     2800,     97,   95}
};
const struct thermal_level_s t_i3_uma_thermal_cpu_fan_cpu_dts = {
    .name = "CPU DTS",
    .num_pairs = ARRAY_SIZE(i3_uma_thermal_cpu_fan_cpu_dts),
    .data = i3_uma_thermal_cpu_fan_cpu_dts,
};

/* UMP CPU fan sensor CPU NTC*/
const struct thermal_level_ags i3_uma_thermal_cpu_fan_cpu_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      35,   UMA_CPU_FAN_START_TEMP},
    {1,     1000,     60,   33},
    {2,     1200,     72,   58},
    {3,     1500,     76,   70},
    {4,     1700,     81,   74},
    {5,     1900,     88,   79},
    {6,     2800,     88,   86}
};
const struct thermal_level_s t_i3_uma_thermal_cpu_fan_cpu_ntc = {
    .name = "CPU NTC",
    .num_pairs = ARRAY_SIZE(i3_uma_thermal_cpu_fan_cpu_ntc),
    .data = i3_uma_thermal_cpu_fan_cpu_ntc,
};


/*
 * fan table for cpu model is i5
 */

/* UMP sys fan sensor SSD1 NTC*/
const struct thermal_level_ags i5_uma_thermal_sys_fan_ssd1_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      40,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      52,   39},
    {2,     1200,     55,   51},
    {3,     1500,     58,   54},
    {4,     1800,     61,   57},
    {5,     2000,     65,   60},
    {6,     2800,     65,   64}
};
const struct thermal_level_s t_i5_uma_thermal_sys_fan_ssd1_ntc = {
    .name = "SSD1 NTC",
    .num_pairs = ARRAY_SIZE(i5_uma_thermal_sys_fan_ssd1_ntc),
    .data = i5_uma_thermal_sys_fan_ssd1_ntc,
};

/* UMP sys fan sensor SSD2 NTC*/
const struct thermal_level_ags i5_uma_thermal_sys_fan_ssd2_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      40,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      52,   39},
    {2,     1200,     55,   51},
    {3,     1500,     58,   54},
    {4,     1800,     61,   57},
    {5,     2000,     65,   60},
    {6,     2800,     65,   64}
};
const struct thermal_level_s t_i5_uma_thermal_sys_fan_ssd2_ntc = {
    .name = "SSD2 NTC",
    .num_pairs = ARRAY_SIZE(i5_uma_thermal_sys_fan_ssd2_ntc),
    .data = i5_uma_thermal_sys_fan_ssd2_ntc,
};

/* UMP sys fan sensor memory NTC*/
const struct thermal_level_ags i5_uma_thermal_sys_fan_memory_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      39,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      55,   38},
    {2,     1200,     58,   54},
    {3,     1500,     60,   57},
    {4,     1800,     62,   59},
    {5,     2000,     70,   61},
    {6,     2800,     70,   69}
};
const struct thermal_level_s t_i5_uma_thermal_sys_fan_memory_ntc = {
    .name = "Memory NTC",
    .num_pairs = ARRAY_SIZE(i5_uma_thermal_sys_fan_memory_ntc),
    .data = i5_uma_thermal_sys_fan_memory_ntc,
};

/* UMP CPU fan sensor CPU DTS*/
const struct thermal_level_ags  i5_uma_thermal_cpu_fan_cpu_dts[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      35,   UMA_CPU_FAN_START_TEMP},
    {1,     1000,     68,   33},
    {2,     1200,     75,   66},
    {3,     1500,     83,   73},
    {4,     1700,     91,   81},
    {5,     1900,     97,   89},
    {6,     2800,     97,   95}
};
const struct thermal_level_s t_i5_uma_thermal_cpu_fan_cpu_dts = {
    .name = "CPU DTS",
    .num_pairs = ARRAY_SIZE(i5_uma_thermal_cpu_fan_cpu_dts),
    .data = i5_uma_thermal_cpu_fan_cpu_dts,
};
        
/* UMP CPU fan sensor CPU NTC*/    
const struct thermal_level_ags i5_uma_thermal_cpu_fan_cpu_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     800,      35,   UMA_CPU_FAN_START_TEMP},
    {1,     1000,     62,   33},
    {2,     1200,     70,   60},
    {3,     1500,     77,   68},
    {4,     1700,     81,   75},
    {5,     1900,     88,   79},
    {6,     2800,     88,   86}
}; 
const struct thermal_level_s t_i5_uma_thermal_cpu_fan_cpu_ntc = {
    .name = "CPU NTC",
    .num_pairs = ARRAY_SIZE(i5_uma_thermal_cpu_fan_cpu_ntc),
    .data = i5_uma_thermal_cpu_fan_cpu_ntc,
};


/*
 * fan table for cpu model is i7
 */

/* UMP sys fan sensor SSD1 NTC*/
const struct thermal_level_ags i7_uma_thermal_sys_fan_ssd1_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      40,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      52,   39},
    {2,     1200,     55,   51},
    {3,     1500,     58,   54},
    {4,     1800,     61,   57},
    {5,     2000,     65,   60},
    {6,     2800,     65,   64}
};
const struct thermal_level_s t_i7_uma_thermal_sys_fan_ssd1_ntc = {
    .name = "SSD1 NTC",
    .num_pairs = ARRAY_SIZE(i7_uma_thermal_sys_fan_ssd1_ntc),
    .data = i7_uma_thermal_sys_fan_ssd1_ntc,
};

/* UMP sys fan sensor SSD2 NTC*/
const struct thermal_level_ags i7_uma_thermal_sys_fan_ssd2_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      40,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      52,   39},
    {2,     1200,     55,   51},
    {3,     1500,     58,   54},
    {4,     1800,     61,   57},
    {5,     2000,     65,   60},
    {6,     2800,     65,   64}
};
const struct thermal_level_s t_i7_uma_thermal_sys_fan_ssd2_ntc = {
    .name = "SSD2 NTC",
    .num_pairs = ARRAY_SIZE(i7_uma_thermal_sys_fan_ssd2_ntc),
    .data = i7_uma_thermal_sys_fan_ssd2_ntc,
};

/* UMP sys fan sensor memory NTC*/
const struct thermal_level_ags i7_uma_thermal_sys_fan_memory_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      39,   UMA_SYS_FAN_START_TEMP},
    {1,     900,      55,   38},
    {2,     1200,     58,   54},
    {3,     1500,     60,   57},
    {4,     1800,     62,   59},
    {5,     2000,     70,   61},
    {6,     2800,     70,   69}
};
const struct thermal_level_s t_i7_uma_thermal_sys_fan_memory_ntc = {
    .name = "Memory NTC",
    .num_pairs = ARRAY_SIZE(i7_uma_thermal_sys_fan_memory_ntc),
    .data = i7_uma_thermal_sys_fan_memory_ntc,
};

/* UMP CPU fan sensor CPU DTS*/
const struct thermal_level_ags  i7_uma_thermal_cpu_fan_cpu_dts[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      35,   UMA_CPU_FAN_START_TEMP},
    {1,     1000,     70,   33},
    {2,     1200,     74,   68},
    {3,     1500,     80,   72},
    {4,     1700,     88,   78},
    {5,     1900,     95,   86},
    {6,     2800,     95,   93}
};
const struct thermal_level_s t_i7_uma_thermal_cpu_fan_cpu_dts = {
    .name = "CPU DTS",
    .num_pairs = ARRAY_SIZE(i7_uma_thermal_cpu_fan_cpu_dts),
    .data = i7_uma_thermal_cpu_fan_cpu_dts,
};

/* UMP CPU fan sensor CPU NTC*/
const struct thermal_level_ags i7_uma_thermal_cpu_fan_cpu_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      35,   UMA_CPU_FAN_START_TEMP},
    {1,     1000,     60,   33},
    {2,     1200,     74,   58},
    {3,     1500,     80,   72},
    {4,     1700,     88,   78},
    {5,     1900,     95,   86},
    {6,     2800,     95,   93}
}; 
const struct thermal_level_s t_i7_uma_thermal_cpu_fan_cpu_ntc = {
    .name = "CPU NTC",
    .num_pairs = ARRAY_SIZE(i7_uma_thermal_cpu_fan_cpu_ntc),
    .data = i7_uma_thermal_cpu_fan_cpu_ntc,
};


/*****************************************************************/
/* GFX sys fan sensor SSD1 NTC*/
const struct thermal_level_ags gfx_thermal_sys_fan_ssd1_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     500,      60,   GFX_SYS_FAN_START_TEMP},
    {1,     800,      62,   52},
    {2,     1000,     65,   56},
    {3,     1300,     67,   59},
    {4,     1500,     71,   61},
    {5,     2800,     71,   64}
};
const struct thermal_level_s t_gfx_thermal_sys_fan_ssd1_ntc = {
    .name = "SSD1 NTC",
    .num_pairs = ARRAY_SIZE(gfx_thermal_sys_fan_ssd1_ntc),
    .data = gfx_thermal_sys_fan_ssd1_ntc,
};

/* GFX sys fan sensor SSD2 NTC*/
const struct thermal_level_ags gfx_thermal_sys_fan_ssd2_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     500,      64,   GFX_SYS_FAN_START_TEMP},
    {1,     800,      65,   62},
    {2,     1000,     66,   63},
    {3,     1300,     72,   64},
    {4,     1500,     78,   69},
    {5,     2800,     78,   76}
};
const struct thermal_level_s t_gfx_thermal_sys_fan_ssd2_ntc = {
    .name = "SSD2 NTC",
    .num_pairs = ARRAY_SIZE(gfx_thermal_sys_fan_ssd2_ntc),
    .data = gfx_thermal_sys_fan_ssd2_ntc,
};

/* GFX sys fan sensor MEMORY NTC*/  
const struct thermal_level_ags gfx_thermal_sys_fan_memory_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     500,      55,   GFX_SYS_FAN_START_TEMP},
    {1,     800,      60,   53},
    {2,     1000,     65,   58},
    {3,     1300,     69,   63},
    {4,     1500,     72,   67},
    {5,     2800,     72,   70}
};
const struct thermal_level_s t_gfx_thermal_sys_fan_memory_ntc = {
    .name = "Memory NTC",
    .num_pairs = ARRAY_SIZE(gfx_thermal_sys_fan_memory_ntc),
    .data = gfx_thermal_sys_fan_memory_ntc,
};

/* GFX sys fan sensor PCIEx16 NTC */  
const struct thermal_level_ags gfx_thermal_sys_fan_pciex16_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     500,      58,   GFX_SYS_FAN_START_TEMP},
    {1,     800,      62,   54},
    {2,     1000,     65,   58},
    {3,     1300,     71,   62},
    {4,     1500,     75,   66},
    {5,     2800,     75,   73}
};
const struct thermal_level_s t_gfx_thermal_sys_fan_pciex16_ntc = {
    .name = "PCIEX16 NTC",
    .num_pairs = ARRAY_SIZE(gfx_thermal_sys_fan_pciex16_ntc),
    .data = gfx_thermal_sys_fan_pciex16_ntc,
};

/* GFX cpu fan CPU DTS */      
const struct thermal_level_ags gfx_thermal_cpu_fan_cpu_dts[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      60,   GFX_CPU_FAN_START_TEMP},
    {1,     1000,     68,   57},
    {2,     1300,     77,   65},
    {3,     1600,     89,   71},
    {4,     1800,     96,   87},
    {5,     2800,     96,   95}
};
const struct thermal_level_s t_gfx_thermal_cpu_fan_cpu_dts = {
    .name = "CPU DTS",
    .num_pairs = ARRAY_SIZE(gfx_thermal_cpu_fan_cpu_dts),
    .data = gfx_thermal_cpu_fan_cpu_dts,
};
            
/* GFX cpu fan CPU NTC */      
const struct thermal_level_ags gfx_thermal_cpu_fan_cpu_ntc[] = {
/* level    RPM        HowTri       lowTri */
    {0,     700,      60,   GFX_CPU_FAN_START_TEMP},
    {1,     1000,     69,   57},
    {2,     1300,     78,   65},
    {3,     1600,     82,   72},
    {4,     1800,     88,   79},
    {5,     2800,     88,   86}
};
const struct thermal_level_s t_gfx_thermal_cpu_fan_cpu_ntc = {
    .name = "CPU NTC ",
    .num_pairs = ARRAY_SIZE(gfx_thermal_cpu_fan_cpu_ntc),
    .data = gfx_thermal_cpu_fan_cpu_ntc,
};

__overridable struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_CPU_DTS] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
        .temp_fan_off = C_TO_K(25),
	    .temp_fan_max = C_TO_K(45)
	},
    [TEMP_SENSOR_AMBIENCE_NTC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
        .temp_fan_off = C_TO_K(10),
	    .temp_fan_max = C_TO_K(40)
	},
	[TEMP_SENSOR_SSD1_NTC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
        .temp_fan_off = C_TO_K(35),
	    .temp_fan_max = C_TO_K(50)
	},
	[TEMP_SENSOR_PCIEX16_NTC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
        .temp_fan_off = C_TO_K(10),
	    .temp_fan_max = C_TO_K(40)
	},
	[TEMP_SENSOR_CPU_NTC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
        .temp_fan_off = C_TO_K(25),
	    .temp_fan_max = C_TO_K(45)
	},
	[TEMP_SENSOR_MEMORY_NTC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
        .temp_fan_off = C_TO_K(35),
	    .temp_fan_max = C_TO_K(50)
	},
    [TEMP_SENSOR_SSD2_NTC] = {
        .temp_host = {
            [EC_TEMP_THRESH_HIGH] = C_TO_K(90),
            [EC_TEMP_THRESH_HALT] = C_TO_K(92),
        },
        .temp_host_release = {
            [EC_TEMP_THRESH_HIGH] = C_TO_K(80),
        },
        .temp_fan_off = C_TO_K(35),
        .temp_fan_max = C_TO_K(50)
        },
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

int thermal_fan_percent(int low, int high, int cur)
{
	if (cur < low)
		return 0;
	if (cur > high)
		return 100;
	return 100 * (cur - low) / (high - low);
}

/* The logic below is hard-coded for only three thresholds: WARN, HIGH, HALT.
 * This is just a validity check to be sure we catch any changes in thermal.h
 */
BUILD_ASSERT(EC_TEMP_THRESH_COUNT == 3);

void set_cpu_model(uint8_t value)
{
    cpu_model = value;
}

static uint8_t get_fan_level(uint16_t temp, uint8_t fan_level, const struct thermal_level_s *fantable)
{
	uint8_t new_level = 0x0;
    const struct thermal_level_ags *data = fantable->data;

	new_level = fan_level;
	if(fan_level < (LEVEL_COUNT - 1))
	{
		if(temp >= data[fan_level].HowTri)
		{
			new_level++;
		}
	}
	if(fan_level > 0)
	{
		if(temp < data[fan_level].lowTri)
		{
			new_level--;
		}
	}
	return new_level;

}

static int get_fan_RPM(uint8_t fan_level, const struct thermal_level_s *fantable)
{
    const struct thermal_level_ags *data = fantable->data;
    return data[fan_level].RPM;
}

static int cpu_fan_start_temp(uint8_t thermalMode)
{
    int temp = 0x0;

    switch(thermalMode) {
        case THERMAL_UMA:
            if (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC) >= UMA_CPU_FAN_START_TEMP) {
                temp = (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC)
                    - UMA_CPU_FAN_START_TEMP) * TEMP_MULTIPLE;
            }
            break;
         case THERMAL_WITH_GFX:
            if (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC) >= GFX_CPU_FAN_START_TEMP) {
                temp = (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC)
                    - GFX_CPU_FAN_START_TEMP) * TEMP_MULTIPLE;
            }
            break;
        default:
            break;
    }
    return temp;
}

int cpu_fan_check_RPM(uint8_t thermalMode)
{
    uint8_t fan = PWM_CH_CPU_FAN;
    int rpm_target = 0x0;
    int temp = 0x0;
    const struct thermal_level_s *fantable_cpu_dts;
    const struct thermal_level_s *fantable_cpu_ntc;

    /* sensor model form the configuration table board.c */
    switch(thermalMode) { 
        case THERMAL_UMA:
            if (0x01 == cpu_model) {
                fantable_cpu_dts = &t_i3_uma_thermal_cpu_fan_cpu_dts;
                fantable_cpu_ntc = &t_i3_uma_thermal_cpu_fan_cpu_ntc;
            } else if (0x02 == cpu_model) {
                fantable_cpu_dts = &t_i5_uma_thermal_cpu_fan_cpu_dts;
                fantable_cpu_ntc = &t_i5_uma_thermal_cpu_fan_cpu_ntc;
            } else {
                fantable_cpu_dts = &t_i7_uma_thermal_cpu_fan_cpu_dts;
                fantable_cpu_ntc = &t_i7_uma_thermal_cpu_fan_cpu_ntc;
            }

            /* cpu fan start status  */ 
            temp = cpu_fan_start_temp(THERMAL_UMA);

            /* cpu fan check CPU DTS */
            g_fanLevel[fan].cpuDts =
                get_fan_level(getTempSensors(TEMP_SENSOR_CPU_DTS),
                    g_fanLevel[fan].cpuDts, fantable_cpu_dts);
            g_fanRPM[fan].cpuDts = get_fan_RPM(g_fanLevel[fan].cpuDts, 
                    fantable_cpu_dts);

            /* cpu fan check CPU NTC */    
            g_fanLevel[fan].cpuNtc =
                get_fan_level(getTempSensors(TEMP_SENSOR_CPU_NTC), 
                    g_fanLevel[fan].cpuNtc, fantable_cpu_ntc);
            g_fanRPM[fan].cpuNtc = get_fan_RPM(g_fanLevel[fan].cpuNtc,
                    fantable_cpu_ntc);


            rpm_target = (g_fanRPM[fan].cpuDts > g_fanRPM[fan].cpuNtc)
                            ?  g_fanRPM[fan].cpuDts : g_fanRPM[fan].cpuNtc;
            rpm_target += temp;
            break;
        case THERMAL_WITH_GFX:
            /* cpu fan start status  */ 
            temp = cpu_fan_start_temp(THERMAL_WITH_GFX);

            /* cpu fan check CPU DTS */      
            g_fanLevel[fan].cpuDts =
                get_fan_level(getTempSensors(TEMP_SENSOR_CPU_DTS), 
                    g_fanLevel[fan].cpuDts, &t_gfx_thermal_cpu_fan_cpu_dts);
            g_fanRPM[fan].cpuDts = get_fan_RPM(g_fanLevel[fan].cpuDts, 
                &t_gfx_thermal_cpu_fan_cpu_dts);

            /* sys fan check CPU NTC */    
            g_fanLevel[fan].cpuNtc =
                get_fan_level(getTempSensors(TEMP_SENSOR_CPU_NTC), 
                    g_fanLevel[fan].cpuNtc, &t_gfx_thermal_cpu_fan_cpu_ntc); 
            g_fanRPM[fan].cpuNtc = get_fan_RPM(g_fanLevel[fan].cpuNtc
                , &t_gfx_thermal_cpu_fan_cpu_ntc);

            rpm_target = (g_fanRPM[fan].cpuDts > g_fanRPM[fan].cpuNtc)
                            ?  g_fanRPM[fan].cpuDts: g_fanRPM[fan].cpuNtc;
            rpm_target += temp;
            break;  
        default:
            break;
    }
    return rpm_target;
}

/* ambience NTC */
static int sys_fan_start_temp(uint8_t thermalMode)
{
    int temp = 0x0;

    switch(thermalMode) { 
        case THERMAL_UMA:
            if (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC) >= UMA_SYS_FAN_START_TEMP) {
                temp = (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC)
                    - UMA_SYS_FAN_START_TEMP) * TEMP_MULTIPLE;
            }
            break;
         case THERMAL_WITH_GFX:
             if (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC) >= GFX_SYS_FAN_START_TEMP) {
                 temp = (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC)
                     - GFX_SYS_FAN_START_TEMP) * TEMP_MULTIPLE;
             }
             break;
        default:
            break;
    }
    return temp;
}

int sys_fan_check_RPM(uint8_t thermalMode)
{
    uint8_t fan = PWM_CH_SYS_FAN;
    int rpm_target = 0x0;
    int temp = 0x0;
    const struct thermal_level_s *fantable_ssd1_ntc;
    const struct thermal_level_s *fantable_ssd2_ntc;
    const struct thermal_level_s *fantable_memory_ntc;

    /* sensor model form the configuration table board.c */
    switch(thermalMode) {
        case THERMAL_UMA:
            if (0x01 == cpu_model) {
                fantable_ssd1_ntc = &t_i3_uma_thermal_sys_fan_ssd1_ntc;
                fantable_ssd2_ntc = &t_i3_uma_thermal_sys_fan_ssd2_ntc;
                fantable_memory_ntc = &t_i3_uma_thermal_sys_fan_memory_ntc;
            } else if (0x02 == cpu_model) {
                fantable_ssd1_ntc = &t_i5_uma_thermal_sys_fan_ssd1_ntc;
                fantable_ssd2_ntc = &t_i5_uma_thermal_sys_fan_ssd2_ntc;
                fantable_memory_ntc = &t_i5_uma_thermal_sys_fan_memory_ntc;
            } else {
                fantable_ssd1_ntc = &t_i7_uma_thermal_sys_fan_ssd1_ntc;
                fantable_ssd2_ntc = &t_i7_uma_thermal_sys_fan_ssd2_ntc;
                fantable_memory_ntc = &t_i7_uma_thermal_sys_fan_memory_ntc;
            }

            /* sys fan start status ambience NTC */
            temp = sys_fan_start_temp(THERMAL_UMA);

            /* sys fan check SSD1 NTC */
            g_fanLevel[fan].ssd1Ntc =
                get_fan_level(getTempSensors(TEMP_SENSOR_SSD1_NTC),
                    g_fanLevel[fan].ssd1Ntc, fantable_ssd1_ntc);
            g_fanRPM[fan].ssd1Ntc = get_fan_RPM(g_fanLevel[fan].ssd1Ntc, 
                fantable_ssd1_ntc);

            /* sys fan check SSD2 NTC */
            g_fanLevel[fan].ssd2Ntc =
                get_fan_level(getTempSensors(TEMP_SENSOR_SSD2_NTC),
                g_fanLevel[fan].ssd2Ntc, fantable_ssd2_ntc);
            g_fanRPM[fan].ssd2Ntc = get_fan_RPM(g_fanLevel[fan].ssd2Ntc, 
                fantable_ssd2_ntc);

            rpm_target = (g_fanRPM[fan].ssd1Ntc > g_fanRPM[fan].ssd2Ntc)
                            ?  g_fanRPM[fan].ssd1Ntc : g_fanRPM[fan].ssd2Ntc;

            /* sys fan check Memory NTC */
            g_fanLevel[fan].memoryNtc =
                get_fan_level(getTempSensors(TEMP_SENSOR_MEMORY_NTC),
                    g_fanLevel[fan].memoryNtc, fantable_memory_ntc);
            g_fanRPM[fan].memoryNtc = get_fan_RPM(g_fanLevel[fan].memoryNtc
                , fantable_memory_ntc);

            rpm_target = (rpm_target > g_fanRPM[fan].memoryNtc)
                            ?  rpm_target : g_fanRPM[fan].memoryNtc;
            rpm_target += temp;
            break;

        case THERMAL_WITH_GFX:
            /* sys fan start status ambience NTC */
            temp = sys_fan_start_temp(THERMAL_WITH_GFX);

            /* sys fan check SSD1 NTC */
            g_fanLevel[fan].ssd1Ntc =
                get_fan_level(getTempSensors(TEMP_SENSOR_SSD1_NTC),
                    g_fanLevel[fan].ssd1Ntc, &t_gfx_thermal_sys_fan_ssd1_ntc);
            g_fanRPM[fan].ssd1Ntc = get_fan_RPM(g_fanLevel[fan].ssd1Ntc,
                &t_gfx_thermal_sys_fan_ssd1_ntc);

            /* sys fan check SSD2 NTC */
            g_fanLevel[fan].ssd2Ntc =
                get_fan_level(getTempSensors(TEMP_SENSOR_SSD2_NTC),
                    g_fanLevel[fan].ssd2Ntc, &t_gfx_thermal_sys_fan_ssd2_ntc);
            g_fanRPM[fan].ssd2Ntc = get_fan_RPM(g_fanLevel[fan].ssd2Ntc,
                &t_gfx_thermal_sys_fan_ssd2_ntc);

            rpm_target = (g_fanRPM[fan].ssd1Ntc > g_fanRPM[fan].ssd2Ntc)
                            ?  g_fanRPM[fan].ssd1Ntc : g_fanRPM[fan].ssd2Ntc;

            /* sys fan check Memory NTC */
            g_fanLevel[fan].memoryNtc =
                get_fan_level(getTempSensors(TEMP_SENSOR_MEMORY_NTC),
                    g_fanLevel[fan].memoryNtc, &t_gfx_thermal_sys_fan_memory_ntc);
            g_fanRPM[fan].memoryNtc = get_fan_RPM(g_fanLevel[fan].memoryNtc, 
                &t_gfx_thermal_sys_fan_memory_ntc);
            
            rpm_target = (rpm_target > g_fanRPM[fan].memoryNtc)
                            ?  rpm_target : g_fanRPM[fan].memoryNtc;

            /* sys fan check pciE16 NTC */
            g_fanLevel[fan].pcie16Ntc =
                get_fan_level(getTempSensors(TEMP_SENSOR_PCIEX16_NTC),
                    g_fanLevel[fan].pcie16Ntc, &t_gfx_thermal_sys_fan_pciex16_ntc);
            g_fanRPM[fan].pcie16Ntc = get_fan_RPM(g_fanLevel[fan].pcie16Ntc, 
                &t_gfx_thermal_sys_fan_pciex16_ntc);


            rpm_target = (rpm_target > g_fanRPM[fan].pcie16Ntc)
                            ?  rpm_target : g_fanRPM[fan].pcie16Ntc;
            rpm_target += temp;
            break;
        default:
            break;
    }
    return rpm_target;
}

/* Device high temperature protection mechanism */
#define TEMP_CPU_DTS_PROTECTION        105
#define TEMP_CPU_NTC_PROTECTION        105
#define TEMP_SSD1_NTC_PROTECTION        90
#define TEMP_SSD2_NTC_PROTECTION        90
#define TEMP_MEMORY_NTC_PROTECTION      90
#define TEMP_AMBIENT_NTC_PROTECTION     70

#define TEMP_PROTECTION_COUNT 5
void temperature_protection_mechanism(void)
{
    #if 0
    /* Device high temperature protection mechanism */
    if (getTempSensors[TEMP_SENSOR_CPU_DTS] > CPU_DTS_PROCHOT_TEMP) {
        gpio_set_level(GPIO_PROCHOT_ODL, 0); /* low Prochot enable */
    } else if (getTempSensors[TEMP_SENSOR_CPU_DTS] < CPU_DTS_PROCHOT_TEMP - 6) {
        gpio_set_level(GPIO_PROCHOT_ODL, 1); /* high Prochot disable */
    }
    #endif
    /* CPU DTS */
    if (getTempSensors(TEMP_SENSOR_CPU_DTS) >= TEMP_CPU_DTS_PROTECTION) {
        g_fanProtect[TEMP_SENSOR_CPU_DTS].time++;
    } else {
        if (g_fanProtect[TEMP_SENSOR_CPU_DTS].time > 0) {
            g_fanProtect[TEMP_SENSOR_CPU_DTS].time--;
        }
    }
    if (g_fanProtect[TEMP_SENSOR_CPU_DTS].time >= TEMP_PROTECTION_COUNT) {
        update_cause_flag(FORCE_POWER_OFF_THERMAL);
        chipset_force_power_off(LOG_ID_SHUTDOWN_0x30);
        g_fanProtect[TEMP_SENSOR_CPU_DTS].time = 0;
    }

    /* CPU NTC */
    if (getTempSensors(TEMP_SENSOR_CPU_NTC) >= TEMP_CPU_NTC_PROTECTION) {
        g_fanProtect[TEMP_SENSOR_CPU_NTC].time++;
    } else {
        if (g_fanProtect[TEMP_SENSOR_CPU_NTC].time > 0) {
            g_fanProtect[TEMP_SENSOR_CPU_NTC].time--;
        }
    }
    if (g_fanProtect[TEMP_SENSOR_CPU_NTC].time >= TEMP_PROTECTION_COUNT) {
        update_cause_flag(FORCE_POWER_OFF_THERMAL);
        chipset_force_power_off(LOG_ID_SHUTDOWN_0x31);
        g_fanProtect[TEMP_SENSOR_CPU_NTC].time = 0;
    }

    /* SSD NTC */
    if (getTempSensors(TEMP_SENSOR_SSD1_NTC) >= TEMP_SSD1_NTC_PROTECTION) {
        g_fanProtect[TEMP_SENSOR_SSD1_NTC].time++;
    } else {
        if (g_fanProtect[TEMP_SENSOR_SSD1_NTC].time > 0) {
            g_fanProtect[TEMP_SENSOR_SSD1_NTC].time--;
        }
    }
    if (g_fanProtect[TEMP_SENSOR_SSD1_NTC].time >= TEMP_PROTECTION_COUNT) {
        update_cause_flag(FORCE_POWER_OFF_THERMAL);
        chipset_force_power_off(LOG_ID_SHUTDOWN_0x38);
        g_fanProtect[TEMP_SENSOR_SSD1_NTC].time = 0;
    }

    /* memory NTC */
    if (getTempSensors(TEMP_SENSOR_MEMORY_NTC) >= TEMP_MEMORY_NTC_PROTECTION) {
        g_fanProtect[TEMP_SENSOR_MEMORY_NTC].time++;
    } else {
        if (g_fanProtect[TEMP_SENSOR_MEMORY_NTC].time > 0) {
            g_fanProtect[TEMP_SENSOR_MEMORY_NTC].time--;
        }
    }
    if (g_fanProtect[TEMP_SENSOR_MEMORY_NTC].time >= TEMP_PROTECTION_COUNT) {
        update_cause_flag(FORCE_POWER_OFF_THERMAL);
        chipset_force_power_off(LOG_ID_SHUTDOWN_0x35);
        g_fanProtect[TEMP_SENSOR_MEMORY_NTC].time = 0;
    }

    /* ambience NTC */
    if (getTempSensors(TEMP_SENSOR_AMBIENCE_NTC) >= TEMP_AMBIENT_NTC_PROTECTION) {
        g_fanProtect[TEMP_SENSOR_AMBIENCE_NTC].time++;
    } else {
        if (g_fanProtect[TEMP_SENSOR_AMBIENCE_NTC].time > 0) {
            g_fanProtect[TEMP_SENSOR_AMBIENCE_NTC].time--;
        }
    }
    if (g_fanProtect[TEMP_SENSOR_AMBIENCE_NTC].time >= TEMP_PROTECTION_COUNT) {
        update_cause_flag(FORCE_POWER_OFF_THERMAL);
        chipset_force_power_off(LOG_ID_SHUTDOWN_0x37);
        g_fanProtect[TEMP_SENSOR_AMBIENCE_NTC].time = 0;
    }

    /* SSD2 NTC */
    if (getTempSensors(TEMP_SENSOR_SSD2_NTC) >= TEMP_SSD2_NTC_PROTECTION) {
        g_fanProtect[TEMP_SENSOR_SSD2_NTC].time++;
    } else {
        if (g_fanProtect[TEMP_SENSOR_SSD2_NTC].time > 0) {
            g_fanProtect[TEMP_SENSOR_SSD2_NTC].time--;
        }
    }
    if (g_fanProtect[TEMP_SENSOR_SSD2_NTC].time >= TEMP_PROTECTION_COUNT) {
        update_cause_flag(FORCE_POWER_OFF_THERMAL);
        chipset_force_power_off(LOG_ID_SHUTDOWN_0x49);
        g_fanProtect[TEMP_SENSOR_SSD2_NTC].time = 0;
    }

}


