/*
 *Copyright 2021 The bitland Authors. All rights reserved.
 *
 * software watchdog for BLD.
 */
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "util.h"
#include "uart.h"


/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

struct wmi_dfx_ags {
    uint8_t startType;
    uint8_t forntType;
    uint8_t shutdownType;
    uint8_t sAbnormalcode;
    uint8_t wakeupType;
    uint8_t wAbnormalcode;

    uint16_t forntcode[2];
    uint16_t lastcode[2];
    uint32_t timestamp[2];
};

struct wmi_dfx_ags g_dfxValue = {
    .startType = 0xB0,
    .forntType = 0xB1,
    .shutdownType = 0xE0,
    .wakeupType = 0xE1,
};


#define  wmi_byteid(id)             (id == 0 ? 0xFF : id)
#define  wmi_byteid_s(id, time)     (id == 0 ? 0xFF : time)

#define  wmi_halfwordid(id)            (id == 0 ? 0xFFFF : id)
#define  wmi_halfwordid_s(id, time)    (id == 0 ? 0xFF00 : time)

#define  wmi_wordid(id, time)          (id == 0 ? 0xFFFFFFFF : time)

#define sAbnormalcode(id, code)   (id == 0 ? 0xFF : (code ? 0xEE : 0xCC))
#define wAbnormalcode(id, code)   (id == 0 ? 0xFF : (code ? 0xEE : 0xCC))

/* Last POST was booted last time */
void post_last_code_s(void)
{
    g_dfxValue.forntcode[1] = g_dfxValue.forntcode[0];
    g_dfxValue.lastcode[1] = g_dfxValue.lastcode[0];
    // add timestamp
    g_dfxValue.timestamp[1] = g_dfxValue.timestamp[0];
}

void post_last_code(int postcode)
{
    g_dfxValue.forntcode[0] = g_dfxValue.lastcode[0];
    g_dfxValue.lastcode[0] = postcode;
    // add timestamp
    g_dfxValue.timestamp[0] = NPCX_TTC;
}

static enum ec_status
WMI_get_dfx_log(struct host_cmd_handler_args *args)
{
    uint8_t i;
    struct ec_wmi_get_dfx_log *p = args->response;
    uint32_t *smptr = (uint32_t *)host_get_memmap(EC_MEMMAP_SHUTDOWN_CAUSE);
    uint32_t *wmptr = (uint32_t *)host_get_memmap(EC_MEMMAP_WAKEUP_CAUSE);

    if (p == NULL) {
        return EC_RES_INVALID_COMMAND;
    }

    p->startType = g_dfxValue.startType | 0xFF00;       /* 1~2 byte */

    /* postcode, New information comes before old information */
    for (i = 0; i < 2; i++) {
        p->postCode[i].type = g_dfxValue.forntType
            | wmi_halfwordid_s(g_dfxValue.lastcode[i], 0xCC00);       /* 3~12 byte */
        /* fornt post code */
        p->postCode[i].code0 = wmi_byteid(g_dfxValue.lastcode[i]);
        /* last post code */
        p->postCode[i].code1= wmi_byteid(g_dfxValue.forntcode[i]);
        /* post code timestamp */
        p->postCode[i].time = g_dfxValue.timestamp[i];
    }

    /* shoutdownCase, New information comes before old information */
    for (i = 0; i < 4; i++) {
        g_dfxValue.sAbnormalcode = sAbnormalcode((uint16_t)(*(smptr + i * 2)),*(smptr + i * 2) >> 16);
        p->shutdownCause[i].type = g_dfxValue.shutdownType 
            | wmi_halfwordid_s(*(smptr + i * 2), g_dfxValue.sAbnormalcode << 8);    /* 23~31 byte */
        p->shutdownCause[i].value = wmi_halfwordid(*(smptr + i * 2));
        p->shutdownCause[i].reserve = 0xFF;
        p->shutdownCause[i].time = wmi_wordid(*(smptr + i * 2),*(smptr + i * 2 + 1));
    }

    /* wakeupCause, New information comes before old information */
    for (i = 0; i < 4; i++) {
        g_dfxValue.wAbnormalcode = wAbnormalcode((uint16_t)(*(wmptr + i * 2)), *(wmptr + i * 2) >> 16);
        p->wakeupCause[i].type = g_dfxValue.wakeupType 
            | wmi_halfwordid_s(*(wmptr + i * 2), g_dfxValue.wAbnormalcode << 8);       /* 59~67  byte */
        p->wakeupCause[i].value = wmi_byteid(*(wmptr + i * 2));
        p->wakeupCause[i].reserve = 0xFFFF;
        p->wakeupCause[i].time = wmi_wordid(*(wmptr + i * 2),*(wmptr + i * 2 + 1));
    }

    args->response_size = sizeof(*p);
    CPRINTS("%s -> %s(), response_size=[%d]", __FILE__, __func__, args->response_size);
    return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_DFX_LOG,
            WMI_get_dfx_log,
            EC_VER_MASK(0));

/* WMI get dfx log */
static enum ec_status
WMI_get_cause_log(struct host_cmd_handler_args *args)
{
    struct ec_wmi_get_cause_log *p = args->response;
    uint32_t *smptr = (uint32_t *)host_get_memmap(EC_MEMMAP_SHUTDOWN_CAUSE);
    uint32_t *wmptr = (uint32_t *)host_get_memmap(EC_MEMMAP_WAKEUP_CAUSE);

    if (p == NULL) {
        return EC_RES_INVALID_COMMAND;
    }

    /* shutdownCause */
    g_dfxValue.sAbnormalcode = sAbnormalcode((uint32_t)(*(smptr)), *(smptr) >> 16);
    p->shutdownCause.type = wmi_byteid_s(*smptr, g_dfxValue.sAbnormalcode);       /* 1~8 byte */
    p->shutdownCause.value = wmi_halfwordid(*smptr);
    p->shutdownCause.reserve = 0xFF;
    p->shutdownCause.time = wmi_wordid(*smptr,*(smptr + 1));

    /* wakeupCase */
    g_dfxValue.wAbnormalcode = wAbnormalcode((uint32_t)(*(wmptr)), *(wmptr) >> 16);
    p->wakeupCause.type = wmi_byteid_s(*wmptr, g_dfxValue.wAbnormalcode);       /* 9~16 byte */
    p->wakeupCause.value = wmi_byteid(*wmptr);
    p->wakeupCause.reserve = 0xFFFF;
    p->wakeupCause.time = wmi_wordid(*wmptr,*(wmptr + 1));
    args->response_size = sizeof(*p);

    CPRINTS("%s -> %s(), response_size=[%d]", __FILE__, __func__, args->response_size);
    return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CASE_LOG,
            WMI_get_cause_log,
            EC_VER_MASK(0));

#define LOG_TYPE_DEFAULT_LOG     0
#define LOG_TYPE_ERROE_LOG       1
/* WMI get ec log */
static enum ec_status
WMI_get_ec_log(struct host_cmd_handler_args *args)
{
    const struct ec_wmi_get_ec_log *p = args->params;

    if (p->logType > 1) {
        return EC_RES_INVALID_PARAM;
    }

   if (uart_console_read_buffer_init() != EC_RES_SUCCESS) {
        return EC_RES_OVERFLOW;
    }

    return uart_console_read_buffer(
            CONSOLE_READ_NEXT,
            (char *)args->response,
            args->response_max,
            &args->response_size);
}
DECLARE_HOST_COMMAND(EC_CMD_GET_EC_LOG,
            WMI_get_ec_log,
            EC_VER_MASK(0));
