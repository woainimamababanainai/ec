/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
#include "battery.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "lpc.h"
#include "ec_commands.h"
#include "tablet_mode.h"
#include "pwm.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"
#include "softwareWatchdog.h"
#include "power_led.h"
#include "flash.h"
#include "fan.h"
#include "thermal.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

/* Last received ACPI command */
static uint8_t __bss_slow acpi_cmd;
/* First byte of data after ACPI command */
static uint8_t __bss_slow acpi_addr;
/* Number of data writes after command */
static int __bss_slow acpi_data_count;

#ifdef CONFIG_DPTF
static int __bss_slow dptf_temp_sensor_id;	/* last sensor ID written */
static int __bss_slow dptf_temp_threshold;	/* last threshold written */

/*
 * Current DPTF profile number.
 * This is by default initialized to 1 if multi-profile DPTF is not supported.
 * If multi-profile DPTF is supported, this is by default initialized to 2 under
 * the assumption that profile #2 corresponds to lower thresholds and is a safer
 * profile to use until board or some EC driver sets the appropriate profile for
 * device mode.
 */
static int current_dptf_profile = DPTF_PROFILE_DEFAULT;

#endif

/*
 * Keep a read cache of four bytes when burst mode is enabled, which is the
 * size of the largest non-string memmap data type.
 */
#define ACPI_READ_CACHE_SIZE 4

/* Start address that indicates read cache is flushed. */
#define ACPI_READ_CACHE_FLUSHED (EC_ACPI_MEM_MAPPED_BEGIN - 1)

/* Calculate size of valid cache based upon end of memmap data. */
#define ACPI_VALID_CACHE_SIZE(addr) (MIN( \
	EC_ACPI_MEM_MAPPED_SIZE + EC_ACPI_MEM_MAPPED_BEGIN - (addr), \
	ACPI_READ_CACHE_SIZE))

/*
 * In burst mode, read the requested memmap data and the data immediately
 * following it into a cache. For future reads in burst mode, try to grab
 * data from the cache. This ensures the continuity of multi-byte reads,
 * which is important when dealing with data types > 8 bits.
 */
static struct {
	int enabled;
	uint8_t start_addr;
	uint8_t data[ACPI_READ_CACHE_SIZE];
} acpi_read_cache;

/*
 * Deferred function to ensure that ACPI burst mode doesn't remain enabled
 * indefinitely.
 */
static void acpi_disable_burst_deferred(void)
{
	acpi_read_cache.enabled = 0;
	lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
	CPUTS("ACPI missed burst disable?");
}
DECLARE_DEFERRED(acpi_disable_burst_deferred);

#ifdef CONFIG_DPTF

static int acpi_dptf_is_profile_valid(int n)
{
#ifdef CONFIG_DPTF_MULTI_PROFILE
	if ((n < DPTF_PROFILE_VALID_FIRST) || (n > DPTF_PROFILE_VALID_LAST))
		return EC_ERROR_INVAL;
#else
	if (n != DPTF_PROFILE_DEFAULT)
		return EC_ERROR_INVAL;
#endif

	return EC_SUCCESS;
}

int acpi_dptf_set_profile_num(int n)
{
	int ret = acpi_dptf_is_profile_valid(n);

	if (ret == EC_SUCCESS) {
		current_dptf_profile = n;
		if (IS_ENABLED(CONFIG_DPTF_MULTI_PROFILE) &&
		    IS_ENABLED(CONFIG_HOSTCMD_EVENTS)) {
			/* Notify kernel to update DPTF profile */
			host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
		}
	}
	return ret;
}

int acpi_dptf_get_profile_num(void)
{
	return current_dptf_profile;
}

#endif

/* Read memmapped data, returns read data or 0xff on error. */
static int acpi_read(uint8_t addr)
{
    uint8_t *memmap_addr = (uint8_t *)(lpc_get_memmap_range() + addr);

    /* Read from cache if enabled (burst mode). */
    if (acpi_read_cache.enabled) {
        /* Fetch to cache on miss. */
        if (acpi_read_cache.start_addr == ACPI_READ_CACHE_FLUSHED ||
            acpi_read_cache.start_addr > addr ||
            addr - acpi_read_cache.start_addr >=
            ACPI_READ_CACHE_SIZE) {
            memcpy(acpi_read_cache.data, memmap_addr, ACPI_VALID_CACHE_SIZE(addr));
            acpi_read_cache.start_addr = addr;
        }
        
        /* Return data from cache. */
        return acpi_read_cache.data[addr - acpi_read_cache.start_addr];
    } else {
        /* Read directly from memmap data. */
        return *memmap_addr;
    }
}

static void acpi_write(uint8_t addr, int w_data)
{
    uint8_t *memmap_addr = (uint8_t *)(lpc_get_memmap_range() + addr);

    *memmap_addr = w_data;
}

/*******************************************************************************
* ec host memory has 256-Byte, remap to HOST IO/900-9FF.
* we set IO/900-9CF for write protection, disable IO/9E0-9FF write protection.
*
* We defined an interface at 9E0-9FF for the BIOS to send custom commands to EC.
*
* This function is called when write EC space 0xE0. BIOS must to write data
* firstly, and then write command to ec.
*
*/
static void oem_bios_to_ec_command(void)
{
    uint8_t *bios_cmd = host_get_memmap(EC_MEMMAP_BIOS_CMD);
    uint8_t *mptr = NULL;
    
    if(0x00 == *bios_cmd)
    {
        return;
    }

    if(0xFF != (*(bios_cmd+0x0F) + *(bios_cmd)))
    {
        CPRINTS("Invalid BIOS command =[0x%02x]", *bios_cmd);
        *(bios_cmd) = 0x00;
        *(bios_cmd + 0x0F) = 0x00;
        *(bios_cmd + 0x01) = 0xFF; /* unknown command */
        return;
    }

    *(bios_cmd + 0x0F) = 0x00;
    *(bios_cmd + 0x01) = 0x00;
    CPRINTS("BIOS command start=[0x%02x], data=[0x%02x]", *bios_cmd, *(bios_cmd+2));

    switch (*bios_cmd) {
    case 0x01 : /* BIOS write ec reset flag*/
        mptr = host_get_memmap(EC_MEMMAP_RESET_FLAG);
        *mptr = 0xAA; /* 0xAA is ec reset flag */
        break;

    case 0x02 : /* power button control */
        mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);
        if (0x01 == *(bios_cmd+2)) {        /* disable */
            (*mptr) |= EC_MEMMAP_POWER_LOCK;
        } else if (0 == *(bios_cmd+2)) {    /* enable */
            (*mptr) &= (~EC_MEMMAP_POWER_LOCK);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x03 : /* system G3 control */
        mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);
        if (0x01 == *(bios_cmd+2)) {        /* disable */
            (*mptr) |= EC_MEMMAP_DISABLE_G3;
        } else if (0x00 == *(bios_cmd+2)) {     /* enable */
            (*mptr) &= (~EC_MEMMAP_DISABLE_G3);
        } else if (0x02 == *(bios_cmd+2)) { /* get G3 state */
            *(bios_cmd + 3) = *mptr & EC_MEMMAP_DISABLE_G3;
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x04 : /* wakeup wdt control */
        if (0x01 == *(bios_cmd+2)) {        /* enable set time */
            setWakeupWDtdata(*(bios_cmd+3) | (*(bios_cmd+4)) << 8);
        } else if(0x02 == *(bios_cmd+2)) {  /* disable */
            ClearWakeupWdtdata();
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x05 : /* shutdown wdt control */
        if (0x01 == *(bios_cmd+2)) {        /* enable */
            g_shutdownWDT.time = *(bios_cmd+3) | (*(bios_cmd+4))<<8;   /* time */
            g_shutdownWDT.countTime = 0;
            g_shutdownWDT.wdtEn = SW_WDT_ENABLE;
            CPRINTS("shutdown WDT Enable time=%d", g_shutdownWDT.time);
        } else if(0x02 == *(bios_cmd+2)) {  /* disable */
            clearShutdownWDtdata();
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;
        
    case 0x06 : /* power LED control */
        if (0x01 == *(bios_cmd+2)) {        /* on */
           powerled_set_state(POWERLED_STATE_ON);
        } else if(0x02 == *(bios_cmd+2)) {  /* off */
            powerled_set_state(POWERLED_STATE_OFF);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x07 : /* LAN wake control */
        mptr = host_get_memmap(EC_MEMMAP_SYS_MISC2);
        if (0x01 == *(bios_cmd+2)) {        /* enable */
            (*mptr) |= EC_MEMMAP_POWER_LAN_WAKE;
            mfg_data_write(MFG_POWER_LAN_WAKE_OFFSET, EC_GENERAL_SIGNES);
        } else if(0x02 == *(bios_cmd+2)) {  /* disable */
            (*mptr) &= (~EC_MEMMAP_POWER_LAN_WAKE);
            mfg_data_write(MFG_POWER_LAN_WAKE_OFFSET, 0x00);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x08 : /* WLAN wake control */
        mptr = host_get_memmap(EC_MEMMAP_SYS_MISC2);
        if (0x01 == *(bios_cmd+2)) {        /* enable */
            (*mptr) |= EC_MEMMAP_POWER_WLAN_WAKE;
            mfg_data_write(MFG_POWER_WLAN_WAKE_OFFSET, EC_GENERAL_SIGNES);
        } else if(0x02 == *(bios_cmd+2)) {  /* disable */
            (*mptr) &= (~EC_MEMMAP_POWER_WLAN_WAKE);
            mfg_data_write(MFG_POWER_WLAN_WAKE_OFFSET, 0x00);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x09 : /* crisis recovery mode control */
        mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);
        if (0x01 == *(bios_cmd+2)) {        /* enter CRISIS mode */
            (*mptr) |= EC_MEMMAP_CRISIS_RECOVERY;
        } else if(0x02 == *(bios_cmd+2)) {  /* exit CRISIS mode */
            (*mptr) &= (~EC_MEMMAP_CRISIS_RECOVERY);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;
        
    case 0x0A : /* Notify EC GraphicCard */
        if (0x01 == *(bios_cmd+2)) {        /* exist */
           thermal_type(THERMAL_WITH_GFX);
        } else if(0x02 == *(bios_cmd+2)) {  /* inexistence */
            thermal_type(THERMAL_UMA);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x0B : /* crate inbreak data */
        mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);
        if (0x01 == *(bios_cmd+2)) {        /* get crisis data */
            *(bios_cmd+3) = get_chassisIntrusion_data();
        } else if(0x02 == *(bios_cmd+2)) {  /* clear crisis data */
            *mptr |= EC_MEMMAP_CRISIS_CLEAR;
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x0C : /* AC recovery state */
        if (0x01 == *(bios_cmd+2)) {        /* AC recovery on */
            mfg_data_write(MFG_AC_RECOVERY_OFFSET, 0x01);
        } else if(0x02 == *(bios_cmd+2)) {  /* AC recovery off */
            mfg_data_write(MFG_AC_RECOVERY_OFFSET, 0x02);
        } else if(0x03 == *(bios_cmd+2)) {  /* AC recovery previous */
            mfg_data_write(MFG_AC_RECOVERY_OFFSET, 0x03);
        }else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x0D : /* wakeup WDT count */
        if (0x01 == *(bios_cmd+2)) {        /* get */
            *(bios_cmd+3) = g_wakeupWDT.timeoutNum;
        } else if(0x02 == *(bios_cmd+2)) {  /* clear */
            g_wakeupWDT.timeoutNum = 0;
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;

    case 0x0E : /* MFG mode control */
        if (0x01 == *(bios_cmd+2)) {        /* enable, 0xFF*/
        #ifdef CONFIG_MFG_MODE_FORBID_WRITE
            *(bios_cmd+1) = 0xFF; /* unknown command */
        #else
            mfg_data_write(MFG_MODE_OFFSET, 0xFF);
        #endif
        } else if(0x02 == *(bios_cmd+2)) {  /* disable, 0xBE*/
           mfg_data_write(MFG_MODE_OFFSET, 0xBE);
        } else if(0x03 == *(bios_cmd+2)) {  /* get */
           *(bios_cmd+3) = mfg_data_read(MFG_MODE_OFFSET);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;
    case 0x0F : /* system status  */
        mptr = host_get_memmap(EC_MEMMAP_SYS_MISC1);
        if (0x01 == *(bios_cmd+2)) {        /* system reboot */
            *mptr |= EC_MEMMAP_SYSTEM_REBOOT;
            FanRebootFlag();
        } else if(0x02 == *(bios_cmd+2)) {  /* System enters S3*/
            *mptr |= EC_MEMMAP_SYSTEM_ENTER_S3;
        } else if(0x03 == *(bios_cmd+2)) {  /* System enters S4 */
            *mptr |= EC_MEMMAP_SYSTEM_ENTER_S4;
        } else if(0x04 == *(bios_cmd+2)) {  /* System enters S5 */
            *mptr |= EC_MEMMAP_SYSTEM_ENTER_S5;
        } else if(0x05 == *(bios_cmd+2)) {
            if (0x01 == *(bios_cmd+3)) {   /* acpi enable */
                *mptr |= EC_MEMMAP_ACPI_MODE;
                hook_notify(HOOK_CHIPSET_ACPI_MODE);
            } else if (0x02 == *(bios_cmd+3)) {    /* acpi disable */
                *mptr &= ~EC_MEMMAP_ACPI_MODE;
            }
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
    break;
    case 0x10: /* Abnormal Power Down Times */
        if (0x01 == *(bios_cmd+2)) {    /* BIOS get abnormal power down times*/
            *(bios_cmd+3) = getAbnormalPowerDownTimes();
        } else if (0x02 == *(bios_cmd+2)) {
            clearAbnormalPowerDownTimes();
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
    break;
    case 0x11: /* Bios boot block damage */
        mptr = host_get_memmap(EC_MEMMAP_SYS_MISC1);
        if (0x01 == *(bios_cmd+2)) {    /* bios boot block no damage*/
            set_area_Damage_flag(0x01);
        } else if (0x02 == *(bios_cmd + 2)) { /* overseas */
            *mptr |= EC_MEMMAP_CHINA_REGION;
            break;
        } else if (0x03 == *(bios_cmd + 2)) { /* china region */
            *mptr &= ~EC_MEMMAP_CHINA_REGION;
            break;
        } else if (0x04 == *(bios_cmd+2)) {
            powerled_set_state_blink(POWERLED_STATE_BLINK, LED_BLINK_TIME_TYPE1);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;
#ifdef NPCX_FAMILY_DT03
    case 0x12: /* Bios notify EC CPU model */
        mptr = host_get_memmap(EC_MEMMAP_CPU_MODEL);
        if (0x01 == *(bios_cmd+2)) {    /* CPU model: i3 */
            *mptr = 0x01;
            set_cpu_model(0x01);
        } else if (0x02 == *(bios_cmd + 2)) { /* CPU model: i5 */
            *mptr = 0x02;
            set_cpu_model(0x02);
        } else if (0x03 == *(bios_cmd + 2)) { /* CPU model: i7 */
            *mptr = 0x03;
            set_cpu_model(0x03);
        } else {
            *(bios_cmd+1) = 0xFF; /* unknown command */
            break;
        }
        break;
#endif
    default :
        *(bios_cmd+1) = 0xFF; /* unknown command */
        break;
    }

    if (0x00 == *(bios_cmd+1)) {
        CPRINTS("BIOS command end  =[0x%02x], data=[0x%02x]", *bios_cmd, *(bios_cmd+2));
        *(bios_cmd+1) = *bios_cmd; /* set status */
    }
    *bios_cmd = 0;
}
/*DECLARE_HOOK(HOOK_MSEC, oem_bios_to_ec_command, HOOK_PRIO_DEFAULT);*/

#ifdef CONFIG_BIOS_CMD_TO_EC
static int console_command_to_ec(int argc, char **argv)
{
    uint8_t *bios_cmd = host_get_memmap(EC_MEMMAP_BIOS_CMD);
    
    if (1 == argc)
    {
        return EC_ERROR_INVAL;
    }
    else
    {
        char *e;
        uint8_t d;
        uint8_t flag;
        uint16_t time;

        if(!strcasecmp(argv[1], "reset_set"))
        {
            *(bios_cmd) = 0x01;
            CPRINTS("set ec reset flasg(0xAA), ec will reset after system shutdown");
        }
        else if(!strcasecmp(argv[1], "psw_ctrl") && (3==argc))
        {
            d = strtoi(argv[2], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = d;
            *(bios_cmd) = 0x02;
            CPRINTS("%s power button to PCH", d?("disable"):("enable"));
        }
        else if(!strcasecmp(argv[1], "g3_ctrl") && (3==argc))
        {
            d = strtoi(argv[2], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = d;
            *(bios_cmd) = 0x03;
            CPRINTS("%s system G3", d?("disable"):("enable"));
        }
        else if(!strcasecmp(argv[1], "wake_wdt_ctrl") && (4==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            time = strtoi(argv[3], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd+3) = time&0xFF;
            *(bios_cmd+4) = (time>>8)&0xFF;
            *(bios_cmd) = 0x04;
            
            CPRINTS("wakeup WDT %s, time=%d",
                (0x01==flag)?("enable"):("disable"), time);
        }
        else if(!strcasecmp(argv[1], "shutdown_wdt_ctrl") && (4==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            time = strtoi(argv[3], &e, 0);
            if (*e)
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd+3) = time&0xFF;
            *(bios_cmd+4) = (time>>8)&0xFF;
            *(bios_cmd) = 0x05;
            
            CPRINTS("shutdown WDT %s, time=%d",
                (0x01==flag)?("enable"):("disable"), time);
        }
        else if(!strcasecmp(argv[1], "powerled_ctrl") && (3==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x06;
        }
        else if(!strcasecmp(argv[1], "lanwake_ctrl") && (3==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x07;
        }
        else if(!strcasecmp(argv[1], "wlanwake_ctrl") && (3==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x08;
        }
        else if(!strcasecmp(argv[1], "crisis_ctrl") && (3==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x09;
        }
        else if(!strcasecmp(argv[1], "inbreak_ctrl") && (3==argc))
        {
            if(!strcasecmp(argv[2], "get"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "cls"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x0B;
        }
        else if(!strcasecmp(argv[1], "recovry_ctrl") && (3==argc))
        {
            if(!strcasecmp(argv[2], "on"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "off"))
                flag = 0x02;
            else if(!strcasecmp(argv[2], "pre"))
                flag = 0x03;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x0C;
        }
        else if(!strcasecmp(argv[1], "wdt_count") && (3==argc))
        {
            if(!strcasecmp(argv[2], "get"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "cls"))
                flag = 0x02;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x0D;
        }
        else if(!strcasecmp(argv[1], "mfg_mode") && (3==argc))
        {
            if(!strcasecmp(argv[2], "en"))
                flag = 0x01;
            else if(!strcasecmp(argv[2], "dis"))
                flag = 0x02;
            else if(!strcasecmp(argv[2], "get"))
                flag = 0x03;
            else
                return EC_ERROR_PARAM2;

            *(bios_cmd+2) = flag;
            *(bios_cmd) = 0x0E;
        }
        else
        {
            return EC_ERROR_PARAM2;
        }
    }

    oem_bios_to_ec_command();
    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bios_cmd, console_command_to_ec,
        "\n[reset_set]\n"
        "[psw_ctrl <1/0>]\n"
        "[g3_ctrl <1/0>]\n"
        "[wake_wdt_ctrl <en/dis> time]\n"
        "[shutdown_wdt_ctrl <en/dis> time]\n"
        "[powerled_ctrl <en/dis>\n"
        "[lanwake_ctrl <en/dis>\n"
        "[wlanwake_ctrl <en/dis>\n"
        "[crisis_ctrl <en/dis>]\n"
        "[inbreak_ctrl <get/cls>]\n"
        "[recovry_ctrl <on/off/pre>]\n"
        "[wdt_count <get/cls>]\n"
        "[mfg_mode <en/dis/get>]\n",
        "Simulate a bios command");
#endif

/*
 * This handles AP writes to the EC via the ACPI I/O port. There are only a few
 * ACPI commands (EC_CMD_ACPI_*), but they are all handled here.
 */
int acpi_ap_to_ec(int is_cmd, uint8_t value, uint8_t *resultptr)
{
    int data = 0;
    int retval = 0;
    int result = 0xff;      /* value for bogus read */

    /* Read command/data; this clears the FRMH status bit. */
    if (is_cmd) {
        acpi_cmd = value;
        acpi_data_count = 0;
    } else {
        data = value;
        /*
        * The first data byte is the ACPI memory address for
        * read/write commands.
        */
        if (!acpi_data_count++)
            acpi_addr = data;
    }

    /* Process complete commands */
    if (acpi_cmd == EC_CMD_ACPI_READ && acpi_data_count == 1) {
        /* ACPI read cmd + addr */
        switch (acpi_addr) {
#ifdef CONFIG_FANS
        case EC_ACPI_MEM_FAN_DUTY:
            result = dptf_get_fan_duty_target();
            break;
#endif
        case EC_ACPI_MEM_CPU_FAN_FAULT:
            result = check_CPU_fan_fault();
            break;

        case EC_ACPI_MEM_SYS_FAN_FAULT:
            result = check_SYS_fan_fault();
            break;

        default:
            result = acpi_read(acpi_addr);
            break;
        }

        /* Send the result byte */
        *resultptr = result;
        retval = 1;

    }
    else if (acpi_cmd == EC_CMD_ACPI_WRITE && acpi_data_count == 2) {
        /* ACPI write cmd + addr + data */
        switch (acpi_addr) {
#ifdef CONFIG_FANS
        case EC_ACPI_MEM_FAN_DUTY:
            dptf_set_fan_duty_target(data);
            break;
#endif
        default:
            acpi_write(acpi_addr, data);
            oem_bios_to_ec_command();
            break;
        }
    }
    else if (acpi_cmd == EC_CMD_ACPI_QUERY_EVENT && !acpi_data_count) {
		/* Clear and return the lowest host event */
		int evt_index = lpc_get_next_host_event();
		CPRINTS("ACPI query = %d", evt_index);
		*resultptr = evt_index;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_ENABLE && !acpi_data_count) {
		/*
		 * TODO: The kernel only enables BURST when doing multi-byte
		 * value reads over the ACPI port. We don't do such reads
		 * when our memmap data can be accessed directly over LPC,
		 * so on LM4, for example, this is dead code. We might want
		 * to add a config to skip this code for certain chips.
		 */
		acpi_read_cache.enabled = 1;
		acpi_read_cache.start_addr = ACPI_READ_CACHE_FLUSHED;

		/* Enter burst mode */
		lpc_set_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);

		/*
		 * Disable from deferred function in case burst mode is enabled
		 * for an extremely long time  (ex. kernel bug / crash).
		 */
		hook_call_deferred(&acpi_disable_burst_deferred_data, 1*SECOND);

		/* ACPI 5.0-12.3.3: Burst ACK */
		*resultptr = 0x90;
		retval = 1;
	} else if (acpi_cmd == EC_CMD_ACPI_BURST_DISABLE && !acpi_data_count) {
		acpi_read_cache.enabled = 0;

		/* Leave burst mode */
		hook_call_deferred(&acpi_disable_burst_deferred_data, -1);
		lpc_clear_acpi_status_mask(EC_LPC_STATUS_BURST_MODE);
	}

	return retval;
}
