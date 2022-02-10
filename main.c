/**
 * main.c ---
 *
 * Copyright (C) 2015-2018 Spreadtrum Communications Inc.
 */
#include <pthread.h>
#include <cutils/properties.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <cutils/android_filesystem_config.h>

#include "modem_control.h"
#include "modem_connect.h"

#include "eventmonitor.h"

#define MAX_CAP_NUM         (CAP_TO_INDEX(CAP_LAST_CAP) + 1)

static int g_boot_mode;

static void switchUserToSystem(void)
{
  struct __user_cap_header_struct header;
  struct __user_cap_data_struct data[MAX_CAP_NUM];
  gid_t groups[] = { AID_SYSTEM, AID_RADIO, AID_SHELL, AID_WAKELOCK};

  MODEM_LOGD("%s", __FUNCTION__);

  if (setgroups(sizeof(groups)/sizeof(gid_t), groups)) {
    MODEM_LOGE("setgroups failed: %s", strerror(errno));
  }

  prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);
  if (setuid(AID_SYSTEM) == -1) {
    MODEM_LOGE("uidset failed: %s", strerror(errno));
  }

  header.version = _LINUX_CAPABILITY_VERSION_3;
  header.pid = 0;
  memset(&data, 0, sizeof(data));

  data[CAP_TO_INDEX(CAP_BLOCK_SUSPEND)].effective |= CAP_TO_MASK(CAP_BLOCK_SUSPEND);
  data[CAP_TO_INDEX(CAP_BLOCK_SUSPEND)].permitted |= CAP_TO_MASK(CAP_BLOCK_SUSPEND);
  data[CAP_TO_INDEX(CAP_CHOWN)].effective |= CAP_TO_MASK(CAP_CHOWN);
  data[CAP_TO_INDEX(CAP_CHOWN)].permitted |= CAP_TO_MASK(CAP_CHOWN);

  if (capset(&header, &data[0]) == -1) {
    MODEM_LOGE("capset failed: %s", strerror(errno));
  }
}

static int modem_ctrl_init_boot_mode(void) {
  int fd = -1, ret = 0;
  char cmdline[CPCMDLINE_SIZE] = {0};

  g_boot_mode = BOOT_MODE_NORMAL;

  fd = open("/proc/cmdline", O_RDONLY);
  if (fd < 0) {
    MODEM_LOGE("/proc/cmdline open error:%s\n", strerror(errno));
    return 0;
  }

  if ((ret = read(fd, cmdline, sizeof(cmdline) - 1)) > 0) {
    cmdline[ret] = '\0';

    MODEM_LOGIF("%s:check calibration!\n", __FUNCTION__);
    if (strstr(cmdline, "calibration=") ||
        strstr(cmdline, "androidboot.mode=cali"))
      g_boot_mode = BOOT_MODE_CALIBRATION; // is calimode

    MODEM_LOGIF("%s:check factorytest!\n", __FUNCTION__);
    if (strstr(cmdline, "=factorytest"))
      g_boot_mode = BOOT_MODE_FACTORY; // is factorytest

    MODEM_LOGIF("%s:check recovery!\n", __FUNCTION__);
    if (strstr(cmdline, "androidboot.mode=recovery"))
      g_boot_mode = BOOT_MODE_RECOVERY; // is recovery
  }

  close(fd);
  return g_boot_mode;
}

int modem_ctrl_get_boot_mode(void)
{
	return g_boot_mode;
}

int main(int argc, char *argv[]) {
#ifdef FEATURE_EXTERNAL_MODEM
  pthread_t t1;
#endif
  pthread_t t2;

  int mode;

  MODEM_LOGD(">>>>>> start modem control main task!\n");

  mode = modem_ctrl_init_boot_mode();
  MODEM_LOGD("%s: mode = %d!", __FUNCTION__, mode);

  if(BOOT_MODE_RECOVERY == mode) {
    MODEM_LOGD("%s: recovery mode, goto dead_loop!", __FUNCTION__);
    goto dead_loop;
  }

  if(BOOT_MODE_NORMAL !=  mode)
    MODEM_LOGD("%s: reserve root user!", __FUNCTION__);
  else
    switchUserToSystem();

  if (0 != modem_ctrl_int_modem_type()) {
    MODEM_LOGE("can't get modem type!\n");
    goto dead_loop;
  }

#ifdef FEATURE_EXTERNAL_MODEM
  modem_event_init();

#ifdef FEATURE_PCIE_RESCAN
  modem_pcie_init();
#endif

  if (0 != pthread_create(&t1, NULL, uevent_monitor_thread, NULL)) {
    MODEM_LOGE("modem uevent monitor thread create error!\n");
    goto dead_loop;
  }
#endif

  /*set up socket connection to clients*/
  if (pthread_create(&t2, NULL, (void*)modem_setup_clients_connect, NULL) < 0)
    MODEM_LOGE("Failed to create modemd listen accept thread");

  if (0 != modem_ctrl_boot_modem())
    MODEM_LOGE("boot modem fail!\n");

dead_loop:
  while (1) {
    sleep(100000);
    MODEM_LOGD(" main thread enter sleep!\n");
  }

  return 0;
}

