/*
 * modem_event.c - procees the mdc event
 *
 *
 *  Copyright (C) 2019 spreadtrum Communications Inc.
 *
 *  History:
 *  2019-03-015 wenping.zhou
 *  Initial version.
 *
 */
#include "modem_control.h"
#include "eventmonitor.h"
#include "modem_load.h"
#include "modem_connect.h"

#ifdef FEATURE_PCIE_RESCAN
#include "modem_pcie_control.h"
#endif

/* status */
enum {
	MDM_POWER_OFF = 0,
	MDM_POWER_ON,
	MDM_WARM_RESET,
	MDM_COLD_RESET,
	MDM_CRASH_CP,
	MDM_CP_CRASH,
	MDM_CP_POWER_OFF
};

#define MCD_SUBSYSTEM "modem_ctrl"

static void modem_event_panic_process(void)
{
  int cnt;
  char buf[40];
  int pre_state;

  MODEM_LOGD("panic event!\n");
  modem_ctrl_set_miniap_panic(1);

  /* notify clients */
  snprintf(buf, sizeof(buf), "%s: %s", MODEM_ASSERT, MINIAP_PANIC);

  /* modem block will cause minipa panic, so skip it. */
  pre_state = modem_ctrl_get_modem_state();
  modem_ctrl_set_modem_state(MODEM_STATE_ASSERT);

  if (MODEM_STATE_BLOCK != pre_state) {
    cnt = modem_write_data_to_clients(buf, strlen(buf));
    MODEM_LOGD("write to modemd len = %d,str=%s\n", cnt, buf);
  }
  /* rescan ep device */
#ifdef FEATURE_PCIE_RESCAN
  modem_rescan_ep_device();
#endif
}

void modem_event_triger(BaseUEventInfo *info, void *data)
{
  MODEM_LOGIF("modem_event: action=%s, event=%d",
    info->action, info->modem_stat);

  if (strcmp(info->action, "change"))
    return;

  switch (info->modem_stat) {
  case MDM_POWER_OFF:
  case MDM_POWER_ON:
    break;

  case MDM_WARM_RESET:
    MODEM_LOGD("recv WARM RESET.");
#ifdef FEATURE_EXTERNAL_MODEM
    load_spl_img();
#endif
    break;

  case MDM_COLD_RESET:
    break;

  case MDM_CRASH_CP:
    break;

  case MDM_CP_CRASH:
    modem_event_panic_process();
    break;

  default:
    break;
  }
}

void modem_event_init(void)
{
  modem_event_register(MCD_SUBSYSTEM, modem_event_triger, NULL);
}

