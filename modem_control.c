/**
 * modem_control.c ---
 *
 * Copyright (C) 2015-2018 Spreadtrum Communications Inc.
 */
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <cutils/properties.h>
#include <signal.h>
#include <sys/time.h>

#include "modem_control.h"
#include "modem_connect.h"
#include "modem_load.h"

#define BM_DEV "/dev/sprd_bm"
#define DMC_MPU "/dev/dmc_mpu"

enum sci_bm_cmd_index {
  BM_STATE = 0x0,
  BM_CHANNELS,
  BM_AXI_DEBUG_SET,
  BM_AHB_DEBUG_SET,
  BM_PERFORM_SET,
  BM_PERFORM_UNSET,
  BM_OCCUR,
  BM_CONTINUE_SET,
  BM_CONTINUE_UNSET,
  BM_DFS_SET,
  BM_DFS_UNSET,
  BM_PANIC_SET,
  BM_PANIC_UNSET,
  BM_BW_CNT_START,
  BM_BW_CNT_STOP,
  BM_BW_CNT_RESUME,
  BM_BW_CNT,
  BM_BW_CNT_CLR,
  BM_DBG_INT_CLR,
  BM_DBG_INT_SET,
  BM_CMD_MAX,
};

static int g_modem_type;
static int wait_modem_reset;
static int g_modem_state;

static struct itimerval s_wait_alive_timer;

static void modem_ctrl_try_read_modem_alive(void)
{
  struct timeval timeout;
  fd_set rfds;
  int ret, cnt, count, fd;
  char buf[128];
  char alive_dev[128];
  bool has_alive_prop;

  /* if have alive prop, don't read mode alive here, will read in modem listen function. */
  has_alive_prop = modem_ctrl_get_alive_dev(alive_dev, sizeof(alive_dev));
  if (has_alive_prop)
    return;

  while(cnt++ < 600) {
    fd = open(alive_dev, O_RDONLY);
    usleep(100 * 1000);
  }

  if (fd < 0) {
    MODEM_LOGE("open %s  , errno: %s", alive_dev, strerror(errno));
    return;
  }

  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  cnt = 0;

  MODEM_LOGD("try read from %s\n", alive_dev);

  while (cnt++ < 60 && s_wait_alive_timer.it_value.tv_sec) {
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    do {
      ret = select(fd + 1, &rfds, NULL, NULL, &timeout);
    } while (ret == -1 && errno == EINTR);
    if (ret < 0) {
      MODEM_LOGD("%s: select error: %s", __FUNCTION__, strerror(errno));
      sleep(1);
      continue;
    } else if (ret == 0) {
      continue;
    } else {
      if (!s_wait_alive_timer.it_value.tv_sec)
        break;

      count = read(fd, buf, sizeof(buf) - 1);
      if (count <= 0) {
        MODEM_LOGE("%s: read %d return %d, error: %s", __FUNCTION__, fd, count,
                   strerror(errno));
        sleep(1);
        continue;
      }
      buf[count] = '\0';
      MODEM_LOGD("%s: read response %s from %d", __FUNCTION__, buf, fd);
      if (strstr(buf, MODEM_ALIVE)) {
        modem_ctrl_stop_wait_alive_timer();
        modem_ctrl_set_modem_state(MODEM_STATE_ALIVE);
        modem_write_data_to_clients(MODEM_ALIVE, strlen(MODEM_ALIVE));
        break;
      }
    }
  }
  close(fd);
}

void modem_ctrl_start_wait_alive_timer(void)
{
    int ret;
    bool has_alive_prop;
    char alive_dev[256];

    MODEM_LOGD("%s: ", __FUNCTION__);
    memset(&s_wait_alive_timer, 0, sizeof(s_wait_alive_timer));
    s_wait_alive_timer.it_value.tv_sec = 60;
    ret = setitimer(ITIMER_REAL, &s_wait_alive_timer, NULL);
    if (ret)
        MODEM_LOGE("start alive timer err, %s\n", strerror(errno));

    modem_ctrl_try_read_modem_alive();
}

void modem_ctrl_stop_wait_alive_timer(void)
{
    int ret;

    memset(&s_wait_alive_timer, 0, sizeof(s_wait_alive_timer));
    ret = setitimer(ITIMER_REAL, &s_wait_alive_timer, NULL);
    if (ret)
        MODEM_LOGE("stop alive timer err, %s\n", strerror(errno));

    MODEM_LOGD("%s: ", __FUNCTION__);
}

#ifdef FEATURE_EXTERNAL_MODEM
static int b_miniap_panic = 0;

void modem_ctrl_set_miniap_panic(int panic) {
  b_miniap_panic = panic;
}
#endif

void modem_ctrl_set_wait_reset_flag(int flag) {
  wait_modem_reset = flag;
}

int modem_ctrl_int_modem_type(void) {
  char modem_type[PROPERTY_VALUE_MAX];

  /*get modem mode from property*/
  property_get(MODEM_RADIO_TYPE, modem_type, "not_find");

  MODEM_LOGD("%s: modem type is %s", __FUNCTION__, modem_type);

  if (strcmp("not_find", modem_type) == 0) {
    MODEM_LOGE("%s: %s %s", __FUNCTION__, MODEM_RADIO_TYPE, "not_find");
    return -1;
  }

  if (0 == strcmp("t", modem_type)) {
    g_modem_type = TD_MODEM;
  } else if (0 == strcmp("w", modem_type)) {
    g_modem_type = W_MODEM;
  } else if (0 == strcmp("l", modem_type)) {
    g_modem_type = LTE_MODEM;
  } else if (0 == strcmp("nr", modem_type)) {
    g_modem_type = NR_MODEM;
  }
  else {
    g_modem_type = NO_MODEM;
  }

  return 0;
}

int modem_ctrl_get_modem_type(void) {
  return g_modem_type;
}

void modem_ctrl_enable_wake_lock(bool bEnable, const char *pos) {
  int ret, fd;
  char *lock_path;

  MODEM_LOGD("%s: wake lock bEnable = %d!", pos, bEnable);

  if (bEnable) {
    lock_path = "/sys/power/wake_lock";
  }
  else {
    lock_path = "/sys/power/wake_unlock";
  }

  fd = open(lock_path, O_RDWR);
  if (fd < 0) {
    MODEM_LOGE("open %s failed, error: %s", lock_path, strerror(errno));
    return;
  }

  ret = write(fd, "modem_control", sizeof("modem_control"));
  if (ret < 0) {
    MODEM_LOGE("write %s failed, error: %s", lock_path, strerror(errno));
  }

  close(fd);
}

void modem_ctrl_enable_busmonitor(bool bEnable) {
  int fd;
  int param;
  int cmd;
  static int b_failed = 0;

  /* some device unsupport, if failed, just return */
  if (b_failed) return;

  fd = open(BM_DEV, O_RDWR);
  if (fd < 0) {
    b_failed = 1;
    if (errno != ENOENT && errno != ENODEV)
      MODEM_LOGE("%s: %s failed, error: %s", __FUNCTION__, BM_DEV,
                 strerror(errno));
    return;
  }

  cmd = bEnable ? BM_DBG_INT_SET : BM_DBG_INT_CLR;
  ioctl(fd, cmd, &param);

  MODEM_LOGD("%s: bEnable = %d, cmd = %d", __FUNCTION__, bEnable, cmd);
  close(fd);
}

void modem_ctrl_enable_dmc_mpu(bool bEnable) {
  int fd;
  int param;
  int cmd;

  static int b_failed = 0;

  /* some device unsupport, if failed, just return */
  if (b_failed) return;

  fd = open(DMC_MPU, O_RDWR);
  if (fd < 0) {
    b_failed = 1;
    if (errno != ENOENT && errno != ENODEV)
      MODEM_LOGE("%s: %s failed, error: %s", __FUNCTION__, DMC_MPU,
                 strerror(errno));
    return;
  }

  cmd = bEnable;
  ioctl(fd, cmd, &param);

  MODEM_LOGD("%s: bEnable = %d, cmd = %d", __FUNCTION__, bEnable, cmd);
  close(fd);
}

static int modem_ctrl_open_dev(char *path) {
  int fd = -1;

retry:
  fd = open(path, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    if (errno == EINTR || errno == EAGAIN)
      goto retry;
    else
      return -1;
  }
  return fd;
}

static void modem_ctrl_read_empty_log(int fd) {
  char buf[2048] = {0};
  int ret = 0;
  int count = 0;
  struct timeval timeout;
  fd_set rfds;

  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);

  for (;;) {
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    do {
      ret = select(fd + 1, &rfds, NULL, NULL, &timeout);
    } while (ret == -1 && errno == EINTR);
    if (ret < 0) {
      MODEM_LOGIF("select error: %s", strerror(errno));
      break;
    } else if (ret == 0) {
      MODEM_LOGD(" time out, read log over!");
      break;
    } else {
      MODEM_LOGIF("one time read log start");
      do {
        count = read(fd, buf, sizeof(buf));
        MODEM_LOGIF("read log count = %d", count);
      } while (count > 0);
      if (count < 0) break;
    }
  }
}

static void prepare_reset_modem(void) {
  int w_cnt, log_fd, diag_fd;
  char diag_chan[PROPERTY_VALUE_MAX] = {0};
  char log_chan[PROPERTY_VALUE_MAX] = {0};
  char s_reset_cmd[2] = {0x7a, 0x0a};
  int count;
  char buf[2048];

  /* get diag dev prop */
  property_get(DIAG_DEV_PROP, diag_chan, "not_find");
  MODEM_LOGD("%s: %s = %s\n", __FUNCTION__, DIAG_DEV_PROP, diag_chan);

  /* get diag dev prop */
  property_get(LOG_DEV_PROP, log_chan, "not_find");
  MODEM_LOGD("%s: %s = %s\n", __FUNCTION__, LOG_DEV_PROP, log_chan);

  log_fd = open(log_chan, O_RDWR | O_NONBLOCK);
  if (log_fd < 0) {
    log_fd = open(diag_chan, O_RDWR | O_NONBLOCK);
    MODEM_LOGD("%s: log chanel open failed, use diag chanel, %s\n",
               __FUNCTION__, strerror(errno));
  }

  if (log_fd >= 0) {
    modem_ctrl_read_empty_log(log_fd);
    MODEM_LOGD("%s: read log over %s!\n", __FUNCTION__, log_chan);
    close(log_fd);
  } else {
    MODEM_LOGE("%s: MODEM cannot open log chanel, %s\n", __FUNCTION__,
               strerror(errno));
  }

  /* than write 'z' to cp */
  diag_fd = open(diag_chan, O_RDWR | O_NONBLOCK);
  if (diag_fd < 0) {
    MODEM_LOGE("%s: MODEM cannot open %s, %s\n", __FUNCTION__, diag_chan,
               strerror(errno));
    return;
  } else {
    /* read empty diag first */
    do
    {
      count = read(diag_fd, buf, sizeof(buf));
      //MODEM_LOGD("read diag count = %d", count);
    }while(count>0);

    MODEM_LOGD("%s: ready write diag cmd = %s!", __FUNCTION__, s_reset_cmd);
    modem_ctrl_set_wait_reset_flag(1);
    w_cnt = write(diag_fd, s_reset_cmd, sizeof(s_reset_cmd));
    if (w_cnt != sizeof(s_reset_cmd))
      MODEM_LOGD("%s: MODEM write diag_chan:%d ,%s\n", __FUNCTION__, w_cnt,
                 strerror(errno));
    close(diag_fd);
    return;
  }
}

int modem_ctrl_parse_cmdline(char *cmdvalue) {
  int fd = -1, ret = 0;
  char cmdline[CPCMDLINE_SIZE] = {0};
  char *str = NULL, *temp = NULL;
  int val;

  if (cmdvalue == NULL) {
    MODEM_LOGD("cmd_value = NULL\n");
    return -1;
  }
  fd = open("/proc/cmdline", O_RDONLY);
  if (fd >= 0) {
    if ((ret = read(fd, cmdline, sizeof(cmdline) - 1)) > 0) {
      cmdline[ret] = '\0';
      MODEM_LOGD("modem_ctrl: cmdline %s\n", cmdline);
      str = strstr(cmdline, "modem=");
      if (str != NULL) {
        str += strlen("modem=");
        temp = strchr(str, ' ');
        if(temp)
          *temp = '\0';
      } else {
        MODEM_LOGE("cmdline 'modem=' is not exist\n");
        goto ERROR;
      }
      MODEM_LOGD("cmdline len = %zu, str=%s\n", strlen(str), str);
      if (!strcmp(cmdvalue, str))
        val = 1;
      else
        val = 0;
      close(fd);
      return val;
    } else {
      MODEM_LOGE("cmdline modem=NULL");
      goto ERROR;
    }
  } else {
    MODEM_LOGE("/proc/cmdline open error:%s\n", strerror(errno));
    return 0;
  }
ERROR:
  MODEM_LOGD("modem_ctrl: exit parse!");
  close(fd);
  return 0;
}

int modem_has_been_boot(void) {
    int fd;
    char prop_value[PROPERTY_VALUE_MAX] = {0};
    char tty_dev[256] = {0};

    /* first check dev/sctl_pm */
    fd = open(PMIC_MONITOR_PATH, O_RDWR);
    if (fd >=0) {
        MODEM_LOGD("%s: PM has been boot!\n", __FUNCTION__);
        close(fd);
        return 1;
    }

    /* may some platform without dev/sctl_pm, here check dev/stty_xxx0*/
    memset(prop_value, 0, sizeof(prop_value));
    property_get(TTY_DEV_PROP, prop_value, "not_find");
    MODEM_LOGD("%s: %s = %s\n", __FUNCTION__, TTY_DEV_PROP, prop_value);
    snprintf(tty_dev, sizeof(tty_dev), "%s0", prop_value);
    fd = open(tty_dev, O_RDWR);
    if (fd >=0) {
        MODEM_LOGD("%s: Modem has been boot!\n", __FUNCTION__);
        close(fd);
        return 1;
    }

    return 0;
}

bool modem_ctrl_get_alive_dev(char *alive_dev, int size) {
  char prop_value[PROPERTY_VALUE_MAX] = {0};
  bool has_alive_prop;

  /* get alive dev prop, new board use alive dev */
  if (property_get(ALIVE_DEV_PROP, prop_value, NULL) > 0) {
    memcpy(alive_dev, prop_value, size);
    has_alive_prop = true;
  } else {
    /* old board use tty dev, get stty dev prop */
    memset(prop_value, 0, sizeof(prop_value));
    property_get(TTY_DEV_PROP, prop_value, "not_find");
    snprintf(alive_dev, size, "%s0", prop_value);
    has_alive_prop = false;
  }
  MODEM_LOGD("%s: alive_dev=%s, has_alive_prop=%d\n",
             __FUNCTION__, alive_dev, has_alive_prop);

  return has_alive_prop;
}

void modem_ctrl_set_modem_state(int state)
{
  g_modem_state = state;
  MODEM_LOGD("%s: g_modem_state = %d\n", __FUNCTION__, g_modem_state);
}

int modem_ctrl_get_modem_state(void)
{
  return g_modem_state;
}

void modem_ctrl_reboot_all_system(void) {
  modem_ctrl_set_modem_state(MODEM_STATE_REBOOT_SYS);
  sleep(5);

  system("echo \"c\" > /proc/sysrq-trigger");

  while(1)
    sleep(1);
}

void modem_ctrl_alive_timeout(int signo) {
    bool is_dump_orca;
    int is_reset;

    #ifdef FEATURE_EXTERNAL_MODEM
    char prop_value[PROPERTY_VALUE_MAX] = {0};
    #endif

    MODEM_LOGD("recv signo = %d\n", signo);
    if (signo != SIGALRM)
        return;

    if (MODEM_STATE_ALIVE == modem_ctrl_get_modem_state())
        return;

    /* has been stop. */
    if(s_wait_alive_timer.it_value.tv_sec == 0)
        return;

    /* means don't wait alive again. */
    s_wait_alive_timer.it_value.tv_sec = 0;

    MODEM_LOGE("wait modem alive timeout!");
    /* 6s for log can flush to log file befor rebboot system. */
    sleep(6);

    #ifdef FEATURE_EXTERNAL_MODEM
    property_get(MODEM_SYSDUMP_PROP, prop_value, "off");
    is_dump_orca = strstr(prop_value, "on");
    #endif

    property_get(MODEM_RESET_PROP, prop_value, "0");
    is_reset = atoi(prop_value);
    MODEM_LOGD("%s=%s, is reset=%d\n", MODEM_RESET_PROP, prop_value, is_reset);

    if (is_reset && !is_dump_orca) {
        MODEM_LOGE("%s: wait modem alive timeout, reboot system\n");
        modem_ctrl_reboot_all_system();
    } else {
        /* notify other app, modem not alive, set modem state*/
	modem_ctrl_set_modem_state(MODEM_STATE_ASSERT);
        modem_write_data_to_clients(MODEM_NOT_ALIVE, strlen(MODEM_NOT_ALIVE));
    }
}

int modem_ctrl_boot_modem (void) {
  int modem_is_alive = 0;
  pthread_t t1, t2;

  /* init modem img info */
  if (0 != init_modem_img_info())
    return -1;

  /* boot modem */
  modem_is_alive = modem_has_been_boot();
  if (modem_ctrl_parse_cmdline("shutdown")
      && (modem_is_alive == 0)) {
    load_modem_img(LOAD_ALL_IMG);
  }

  if (0 != pthread_create(&t1, NULL, modem_ctrl_listen_sp, NULL)) {
    MODEM_LOGE("modem_ctrl_listen_sp  create error!\n");
  }

#ifndef FEATURE_REMOVE_SPRD_MODEM
  if (0 != pthread_create(&t2, NULL, modem_ctrl_listen_modem, NULL)) {
    MODEM_LOGE("modem_ctrl_listen_modem create error!\n");
  }

  /*  init alive time out handle function. */
  signal(SIGALRM, modem_ctrl_alive_timeout);

  /* wait  modem alive */
  if (modem_is_alive == 0)
    modem_ctrl_start_wait_alive_timer();

 #endif
  return 0;
}

void *modem_ctrl_listen_clients(void *param) {
  char buf[MAX_ASSERT_INFO_LEN] = {0};
  int cnt = 0;
  bool reboot_modem_only = false;
  char prop[PROPERTY_VALUE_MAX] = {0};

  MODEM_LOGD("%s: start listen clients...\n", __FUNCTION__);

  while (1) {
    cnt = modem_read_data_from_clients(buf, sizeof(buf));
    if (cnt <= 0) {
      sleep(1);
      continue;
    }
    MODEM_LOGD("%s: read cnt= %d, str= %s", __FUNCTION__, cnt, buf);
    /* get wake_lock */
    modem_ctrl_enable_wake_lock(1, __FUNCTION__);

    if (strstr(buf, MODEM_BLOCK)) {
      int isDump;

      modem_ctrl_set_modem_state(MODEM_STATE_BLOCK);
      /* only savedump open need assert modem,
       * assert modem for modem block.
       */
      property_get(MODEM_SAVE_DUMP_PROP, prop, "0");
      isDump = atoi(prop);
      if (isDump)
        modem_load_assert_modem();
    }

    if (MODEM_STATE_BLOCK != g_modem_state
        && MODEM_STATE_ASSERT != g_modem_state
        && MODEM_STATE_RESET != g_modem_state) {
      MODEM_LOGD("state(%d) not block or assert skip it!", g_modem_state);
      continue;
    }

    if (strstr(buf, MODEM_RESET)) {
       modem_ctrl_stop_wait_alive_timer();
#ifdef FEATURE_EXTERNAL_MODEM
        memset(prop, 0, sizeof(prop));
        property_get(MODEM_REBOOT_MODEMONLY, prop, "0");
        reboot_modem_only = atoi(prop);
        if (!reboot_modem_only)
         modem_ctrl_reboot_external_modem();
        else
          load_modem_img(LOAD_MODEM_IMG);
#else
        load_modem_img(LOAD_MODEM_IMG);
#endif
        modem_ctrl_start_wait_alive_timer();
    }
    else if (strstr(buf, PREPARE_RESET)) {
     /* miniap panic, don't wait modem reset, just load all external image */
#ifdef FEATURE_EXTERNAL_MODEM
     if (b_miniap_panic) {
       b_miniap_panic = 0;
       reboot_modem_only = false;
      }

     memset(prop, 0, sizeof(prop));
     property_get(MODEM_REBOOT_MODEMONLY, prop, "0");
     reboot_modem_only = atoi(prop);

     if (!reboot_modem_only) {
       modem_ctrl_stop_wait_alive_timer();
       modem_ctrl_reboot_external_modem();
       modem_ctrl_start_wait_alive_timer();
       modem_ctrl_enable_wake_lock(0, __FUNCTION__);
       continue;
     }
#endif

      prepare_reset_modem();

      /*wait modem reset from modem 5s*/
      cnt = 50;
      do {
       usleep(100 * 1000);
       cnt--;
      } while (cnt > 0 && wait_modem_reset);

      /* can't wait modem reset, force modem reset */
      if (wait_modem_reset) {
        modem_ctrl_stop_wait_alive_timer();
        MODEM_LOGD("%s: wait modem rest timeout, force reset modem\n",
                   __FUNCTION__);
        #ifdef FEATURE_EXTERNAL_MODEM
        modem_ctrl_reboot_external_modem();
        #else
        load_modem_img(LOAD_MODEM_IMG);
        #endif
        modem_ctrl_start_wait_alive_timer();
      }
    }

    /* release wake_lock */
    modem_ctrl_enable_wake_lock(0, __FUNCTION__);
  }

  return NULL;
}

void* modem_ctrl_listen_sp(void *param) {
  int fd;
  int num_read;
  int is_reset;
  int ret;
  char buf[128];
  char prop[256];
  fd_set rfds;
  int try_cnt = 60;

  MODEM_LOGD("enter %s", __FUNCTION__);

  do {
    fd = open(PMIC_MONITOR_PATH, O_RDWR);
    if (fd >= 0) {
      MODEM_LOGD("%s: open %s success\n", __FUNCTION__, PMIC_MONITOR_PATH);
      break;
    } else {
      if (errno == ENOENT) {
        MODEM_LOGD("unsupport sp, path is %s\n", PMIC_MONITOR_PATH);
        return NULL;
      }
    }

    sleep(1);
    try_cnt--;
  } while (try_cnt > 0);

  if (fd < 0) {
    MODEM_LOGE("%s: open %s failed, %s\n",
               __FUNCTION__, PMIC_MONITOR_PATH, strerror(errno));
    return NULL;
  }

  /* If the user mode is notified CM4 open the watchdog */
  memset(prop, 0, sizeof(prop));
  property_get("ro.debuggable", prop, "0");
  if (!atoi(prop)) {
    MODEM_LOGD("%s:user mode need watchdog on\n", __FUNCTION__);
    property_get("persist.vendor.modem.spwdt", prop, "on");
    if (strcmp(prop, "off")){
      MODEM_LOGD("%s: persist.vendor.modem.spwdt = %s",__FUNCTION__, prop);
      if (write(fd, "watchdog on", strlen("watchdog on")) <= 0)
        MODEM_LOGE("%s: write %d error, errno = %s",
             __FUNCTION__, fd, strerror(errno));
    }
  }

  MODEM_LOGD("%s: enter read ...", __FUNCTION__);
  for(;;) {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    ret = select(fd + 1, &rfds, NULL, NULL, NULL);
    if (ret < 0) {
      MODEM_LOGE("select error: %s", strerror(errno));
      if (errno == EINTR || errno == EAGAIN) {
        sleep(1);
        continue;
      } else {
        close(fd);
        return NULL;
      }
    } else if (ret == 0) {
      MODEM_LOGE("select timeout");
      continue;
    } else {
      /* get wake_lock */
      modem_ctrl_enable_wake_lock(1, __FUNCTION__);

      memset(buf, 0, sizeof(buf));
      num_read = read(fd, buf, sizeof(buf) -1);
      if (num_read <= 0) {
        MODEM_LOGE("%s: read %d return %d, errno = %s",
                 __FUNCTION__,
                 fd,
                 num_read,
                 strerror(errno));
        continue;
      }

      MODEM_LOGD("%s: buf=%s", __FUNCTION__, buf);
      if (!strstr(buf, "P-ARM Modem Assert"))
        continue;

      memset(prop, 0, sizeof(prop));
      property_get(MODEM_RESET_PROP, prop, "0");
      is_reset = atoi(prop);
      if (is_reset) {
        MODEM_LOGD("%s: P-ARM Modem Assert, reboot system\n", __FUNCTION__);
        modem_ctrl_reboot_all_system();
      }

      if (modem_ctrl_get_boot_mode() != BOOT_MODE_CALIBRATION) {
        modem_write_data_to_clients(buf, num_read);
      }
      /* release wake_lock */
      modem_ctrl_enable_wake_lock(0, __FUNCTION__);
    }
  }

  close(fd);
  return NULL;
}
