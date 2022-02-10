/**
 * modem_control.h ---
 *
 * Copyright (C) 2015-2018 Spreadtrum Communications Inc.
 */

#ifndef MODEM_CONTROL_H_
#define MODEM_CONTROL_H_
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <log/log.h>

#ifdef LOG_TAG
#undef LOG_TAG
#define LOG_TAG "MODEM_CTRL"
#endif

// #define MODEM_DEBUG

#ifdef MODEM_DEBUG
#define MODEM_LOGIF ALOGD
#else
#define MODEM_LOGIF(...)
#endif

#define MODEM_LOGE ALOGE
#define MODEM_LOGD ALOGD

#define max(A, B) (((A) > (B)) ? (A) : (B))
#define min(A, B) (((A) < (B)) ? (A) : (B))
#define MODEM_SUCC (0)
#define MODEM_ERR (-1)

#define CPCMDLINE_SIZE (0x1000)

#define TD_MODEM 0x3434
#define W_MODEM 0x5656
#define LTE_MODEM 0x7878
#define NR_MODEM 0x9A9A
#define NO_MODEM 0x0

#define WDOG_BANK "wdtirq"
#define MAX_ASSERT_INFO_LEN 256

#define MINIAP_PANIC "Miniap Panic"
#define MODEM_ALIVE "Modem Alive"
#define PREPARE_RESET "Prepare Reset"
#define MODEM_RESET "Modem Reset"
#define MODEM_ASSERT "Modem Assert"
#define MODEM_BLOCK "Modem Blocked"
#define MODEM_NOT_ALIVE "Modem Assert: modem not alive!"

#define DSP_HUNG "HUNG"

#define MODEM_RADIO_TYPE "ro.vendor.radio.modemtype"
#define ASSERT_DEV_PROP "ro.vendor.modem.assert"
#define DIAG_DEV_PROP "ro.vendor.modem.diag"
#define LOG_DEV_PROP "ro.vendor.modem.log"
#define ALIVE_DEV_PROP "ro.vendor.modem.alive"

#define TTY_DEV_PROP "ro.vendor.modem.tty"
#define PROC_DEV_PROP "ro.vendor.modem.dev"
#define MODEM_RESET_PROP "persist.vendor.sys.modemreset"
#define MODEM_SYSDUMP_PROP "persist.vendor.sysdump"
#define MODEM_SAVE_DUMP_PROP       "persist.vendor.sys.modem.save_dump"

#ifdef FEATURE_EXTERNAL_MODEM
#define MODEM_REBOOT_MODEMONLY "persist.vendor.sys.rebootmodemonly"
#endif

#define PMIC_MONITOR_PATH "/dev/sctl_pm"

enum {
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_CALIBRATION,
  BOOT_MODE_FACTORY,
  BOOT_MODE_RECOVERY
};

enum {
  MODEM_STATE_INIT = 0,
  MODEM_STATE_LOADING,
  MODEM_STATE_BOOTING,
  MODEM_STATE_ALIVE,
  MODEM_STATE_ASSERT,
  MODEM_STATE_BLOCK,
  MODEM_STATE_RESET,
  MODEM_STATE_REBOOT_EXT_MODEM,
  MODEM_STATE_REBOOT_SYS,
};

int modem_ctrl_int_modem_type(void);
int modem_ctrl_get_modem_type(void);
void* modem_ctrl_listen_modem(void *param);
void *modem_ctrl_listen_sp(void *param);
void modem_ctrl_enable_wake_lock(bool bEnable, const char *pos);
void modem_ctrl_enable_busmonitor(bool bEnable);
void modem_ctrl_enable_dmc_mpu(bool bEnable);
int modem_ctrl_parse_cmdline(char *cmdvalue);
int modem_has_been_boot(void);
bool wait_for_modem_alive(void);
int modem_ctrl_boot_modem (void) ;
void *modem_ctrl_listen_clients(void *param);
void modem_ctrl_set_wait_reset_flag(int flag);
void modem_ctrl_reboot_all_system(void);
#ifdef FEATURE_EXTERNAL_MODEM
void modem_ctrl_reboot_external_modem(void);
#endif
int modem_ctrl_get_boot_mode(void);

bool modem_ctrl_get_alive_dev(char *alive_dev, int size);
void modem_ctrl_set_modem_state(int state);
int modem_ctrl_get_modem_state(void);
void modem_ctrl_start_wait_alive_timer(void);
void modem_ctrl_stop_wait_alive_timer(void);

#ifdef FEATURE_EXTERNAL_MODEM
void modem_event_init(void);
#ifdef FEATURE_PCIE_RESCAN
void modem_pcie_init(void);
#endif
void modem_ctrl_set_miniap_panic(int panic);
#endif

#endif  // MODEM_CONTROL_H_

