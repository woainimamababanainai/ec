/*
 *Copyright 2021 The bitland Authors. All rights reserved.
 *
 * software watchdog for BLD.
 */
 
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "ec_commands.h"
#include "softwareWatchdog.h"
#include <stddef.h>
#include "chipset.h"
#include "system.h"
#include "power.h"
#include "power_button.h"
#include "flash.h"


/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

ec_wakeup_WDT g_wakeupWDT = {0};
ec_shutdown_WDT g_shutdownWDT = {0};

struct chassis_Intrusion {
    uint8_t   chassisIntrusionData;
    uint8_t   chassisWriteFlashData;
};

struct chassis_Intrusion  pdata = {
    .chassisIntrusionData = 0, .chassisWriteFlashData = 0
};

uint8_t g_WdtForceingShutdown = 0;

static enum ec_status
host_command_WDT(struct host_cmd_handler_args *args)
{
    const struct ec_external_WDT *g_wdtPackage = args->params;

    if (g_wdtPackage == NULL) {
       return EC_RES_INVALID_COMMAND;
    }
    CPRINTS("host_command_WDT: type=%d flag1=%d time=%d\n", g_wdtPackage->type, 
        g_wdtPackage->flag1, g_wdtPackage->time);
    switch(g_wdtPackage->type) {
        case 1:
            if (g_wdtPackage->flag1 == 0x01) {
                setWakeupWDtdata(g_wdtPackage->time);
            } else if (g_wdtPackage->flag1 == 0x02) {
                ClearWakeupWdtdata();
            }
            break;
        case 2:
            if (g_wdtPackage->flag1 == 0x01) {
                g_shutdownWDT.wdtEn = SW_WDT_ENABLE;
                g_shutdownWDT.time = g_wdtPackage->time;
            } else if (g_wdtPackage->flag1 == 0x02) {
                clearShutdownWDtdata();
            }
            break;
        default:
            break;        
    }
    
    return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_EXTERNAL_WDT,
        host_command_WDT,
        EC_VER_MASK(0)); 


/* BIOS post start, it need to set WDT data */
void ClearWakeupWdtdata(void)
{
    if (g_WdtForceingShutdown) {
        return;
    }

    g_wakeupWDT.wdtEn = SW_WDT_DISENABLE;
    g_wakeupWDT.timeoutNum = 0;
    CPRINTS("========Wakeup WDT disable, it need to clear WDt data zero");
}
/* BIOS post start, it need to set WDT data Feed watchdog */
void setWakeupWDtdata(uint16_t time)
{
    uint8_t minTime = 0x0F;

    if (g_WdtForceingShutdown) {
        return;
    }

    g_wakeupWDT.wdtEn = SW_WDT_ENABLE;
    g_wakeupWDT.time = time;
    if (g_wakeupWDT.time < minTime) {
        g_wakeupWDT.time = minTime;
    }
    CPRINTS("========wakeup WDT Enable time=%d", g_wakeupWDT.time);
}

/* Wake Up WDT Service time base:1S */
void WakeUpWDtService(void)
{
    if(chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        g_wakeupWDT.wdtEn = SW_WDT_DISENABLE;
    }

    if ((!g_wakeupWDT.time) &&  chipset_in_state(CHIPSET_STATE_ON)) {
        /* TODO: force shutdown and then power on */
        g_wakeupWDT.wdtEn = SW_WDT_DISENABLE;
        g_wakeupWDT.timeoutNum ++;
        g_WdtForceingShutdown = 1;
        chipset_force_shutdown(LOG_ID_SHUTDOWN_0x09);
        CPRINTS("========Wakeup WDT: force Shutdown Num=%d", g_wakeupWDT.timeoutNum);
    } else {
        g_wakeupWDT.time--;
    }

    if (g_wakeupWDT.timeoutNum >= TIMEOUT_NUM1) {
        g_wakeupWDT.wdtEn = SW_WDT_DISENABLE;
        g_wakeupWDT.timeoutNum = 0;
        g_WdtForceingShutdown = 0;
    }
}
static void power_On_Machine_deferred(void)
{
    CPRINTS("========Wakeup WDT: power on Num=%d", g_wakeupWDT.timeoutNum);
    power_button_pch_pulse(PWRBTN_STATE_LID_OPEN);
}
DECLARE_DEFERRED(power_On_Machine_deferred);

static void WakeUpWdtPowerOn(void)
{
    if(chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
         /* WDT delay power on */
        g_WdtForceingShutdown = 0;
        hook_call_deferred(&power_On_Machine_deferred_data, 5 * SECOND);
    }
}

/* shutdown WDT disable, it need to clear WDt data zero
 * BIOS notify ec to disable shutdown WDT, when BIOS is post start.
 * ec nead to disable shutdown WDT, when system is s3,s4 and s5 state.
 */
void clearShutdownWDtdata(void)
{
    g_shutdownWDT.wdtEn = SW_WDT_DISENABLE;
    CPRINTS("Shutdown WDT disable, it need to clear WDt data zero");
}

/* shutdown WDT Service time base:1S */
void ShutdownWDtService(void)
{
    g_shutdownWDT.countTime++;

    if (chipset_in_state(CHIPSET_STATE_SUSPEND)
        || chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
        clearShutdownWDtdata();
    }

    if (g_shutdownWDT.countTime >= g_shutdownWDT.time) {
        g_shutdownWDT.countTime = 0x00;
        /* TODO: trigger NMI or force shutdown */
        if(chipset_in_state(CHIPSET_STATE_ON) ) {
    #ifdef  CONFIG_FINAL_RELEASE
            /* force shutdwon when release*/
            chipset_force_shutdown(LOG_ID_SHUTDOWN_0x44);
            CPRINTS("Shutdown WDT timeout(%dsec), force shutdwon", g_shutdownWDT.time);
    #else
            /* trigger BSOD when development*/
            g_shutdownWDT.wdtEn = SW_WDT_DISENABLE;
        #if (defined(NPCX_FAMILY_DT01) || defined(NPCX_FAMILY_DT02))
            gpio_set_level(GPIO_PCH_SMI_L, 0);
            msleep(300);
            gpio_set_level(GPIO_PCH_SMI_L, 1);
        #elif defined(NPCX_FAMILY_DT03)
            gpio_set_level(GPIO_CPU_NMI_L, 0);
            msleep(300);
            gpio_set_level(GPIO_CPU_NMI_L, 1);
        #else
        #endif
        shutdown_cause_record(LOG_ID_SHUTDOWN_0xD0);
        CPRINTS("Shutdown WDT timeout(%dsec), trigger BSOD when development", g_shutdownWDT.time);
    #endif
        }
    }
}

static void system_sw_wdt_service(void)
{
    /* wakeup software WDT */
    if (g_wakeupWDT.wdtEn == SW_WDT_ENABLE) {
       WakeUpWDtService();
    }
    if(g_WdtForceingShutdown) {
        WakeUpWdtPowerOn();
    }

    /* shutdown software WDT */
    if (g_shutdownWDT.wdtEn == SW_WDT_ENABLE) {
        ShutdownWDtService();
    }
}
DECLARE_HOOK(HOOK_SECOND, system_sw_wdt_service, HOOK_PRIO_INIT_CHIPSET);

uint8_t get_chassisIntrusion_data(void)
{
   return pdata.chassisIntrusionData;
}

void set_chassisIntrusion_data(uint8_t data)
{
    pdata.chassisIntrusionData = data;
}

/* set clear crisis Intrusion host to ec*/
void clear_chassisIntrusion(void)
{
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);

    /* clear crisis Intrusion data */
    if (!(*mptr & EC_MEMMAP_CRISIS_CLEAR)) {
        return;
    }

    *mptr &= ~((uint8_t)EC_MEMMAP_CRISIS_CLEAR);

    if (gpio_get_level(GPIO_EC_GPIO0_CASE_OPEN_L)) {

        pdata.chassisIntrusionData = 0x00;
        mfg_data_write(MFG_CHASSIS_INTRUSION_DATA_OFFSET,
            pdata.chassisIntrusionData);

        gpio_set_level(GPIO_EC_CASE_OPEN_CLR, 1);
        msleep(5);
        gpio_set_level(GPIO_EC_CASE_OPEN_CLR, 0);
    }
}

/*
 * The Fash value of the first boot read is 0xff.
 * For the first time boot pdata->chassisIntrusiondata = 0xff.
 * For the first time boot chassisIntrusionMode is open.
 * For the first time boot BIOS notify ec to clear chassis intrusion.
 */
static void Chassis_Intrusion_service(void)
{
    /* enter crisis Intrusion mode */
    if (pdata.chassisIntrusionData != 0x01) {
        if (gpio_get_level(GPIO_EC_GPIO0_CASE_OPEN_L)) {
            /* get crisis recovery data */
            pdata.chassisIntrusionData = 0x01;
        } else {
            pdata.chassisIntrusionData = 0x00;
        }
        if (pdata.chassisWriteFlashData 
            != pdata.chassisIntrusionData) {
            pdata.chassisWriteFlashData = pdata.chassisIntrusionData;
            mfg_data_write(MFG_CHASSIS_INTRUSION_DATA_OFFSET,
                pdata.chassisIntrusionData);
        }
    }

    /* clear chassis intrusion */
    clear_chassisIntrusion();
}

DECLARE_HOOK(HOOK_MSEC, Chassis_Intrusion_service, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CONSOLE_CHASSIS_TEST
static int cc_chassisinfo(int argc, char **argv)
{
    char leader[20] = "";
    uint8_t *mptr = host_get_memmap(EC_MEMMAP_POWER_FLAG1);

    ccprintf("%sChassisIntrusionMode: %2d C\n", leader,
            pdata.chassisIntrusionMode);

    ccprintf("%sChassisIntrusionData: %2d C\n", leader,
            pdata.chassisIntrusionData);

    ccprintf("%sGPIO_EC_CASE_OPEN_CLR status: %2d C\n", leader,
        gpio_get_level(GPIO_EC_CASE_OPEN_CLR));

    ccprintf("%sGPIO_EC_GPIO0_CASE_OPEN_L status: %2d C\n", leader,
        gpio_get_level(GPIO_EC_GPIO0_CASE_OPEN_L));

    ccprintf("%sEC_MEMMAP_POWER_FLAG1: %2d C\n", leader, *mptr);
    
    return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chassisinfo, cc_chassisinfo,
            NULL,
            "Print Sensor info");
#endif

