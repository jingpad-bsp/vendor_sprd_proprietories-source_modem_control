
/**
 * internal_modem_control.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#include <cutils/properties.h>

#include "modem_control.h"
#include "modem_load.h"
#include "modem_connect.h"
#include <pthread.h>

/* loop detect sipc modem state */
void *modem_ctrl_listen_modem(void *param) {
  char assert_dev[PROPERTY_VALUE_MAX] = {0};
  char watchdog_dev[PROPERTY_VALUE_MAX] = {0};
  char *alive_dev;
  int ret, watchdog_fd, max_fd, fd = -1;
  int assert_fd, alive_fd = -1, to_clients;
  fd_set rfds;
  char buf[256];
  char alive_dev_buf[256];
  int numRead, numWrite;
  pthread_t t1;
  int cnt = 0;
  bool has_alive_prop;

  MODEM_LOGD("Enter %s !", __FUNCTION__);

  /* get diag assert prop */
  property_get(ASSERT_DEV_PROP, assert_dev, "not_find");
  MODEM_LOGD("%s: %s = %s\n", __FUNCTION__, ASSERT_DEV_PROP, assert_dev);

  /* get watchdog dev */
  property_get(PROC_DEV_PROP, watchdog_dev, "not_find");
  strncat(watchdog_dev, WDOG_BANK,
          sizeof(watchdog_dev) - strlen(watchdog_dev) - 1);
  MODEM_LOGD("%s: %s = %s\n", __FUNCTION__, watchdog_dev, watchdog_dev);

  /* get alive dev */
  has_alive_prop = modem_ctrl_get_alive_dev(alive_dev_buf, sizeof(alive_dev_buf));
  /* if have not alive prop or assert dev as same as alive dev, don't monitor it here */
  if (!has_alive_prop ||
      0 == strcmp(alive_dev_buf, assert_dev))
    alive_dev = NULL;
  else
    alive_dev = alive_dev_buf;

  /* create listen_thread */
  if (0 != pthread_create(&t1, NULL, modem_ctrl_listen_clients, NULL))
    MODEM_LOGE(" %s: create error!\n", __FUNCTION__);

  /* open assert_dev */
  while ((assert_fd = open(assert_dev, O_RDWR)) < 0) {
    if (cnt++ == 600)
      MODEM_LOGE("%s: open %s failed, error: %s\n", __FUNCTION__, assert_dev,
                 strerror(errno));
    usleep((cnt < 600) ? 100000 : 1000000);
  }
  MODEM_LOGD("%s: open assert dev: %s, fd = %d\n", __FUNCTION__, assert_dev,
             assert_fd);

  /* set all valid fd */
  FD_ZERO(&rfds);
  FD_SET(assert_fd, &rfds);
  max_fd = assert_fd;

  /* open watchdog_fd */
  watchdog_fd = open(watchdog_dev, O_RDONLY);
  if (watchdog_fd < 0) {
    /* if  watchdog_fd can't be open, we also continue */
    MODEM_LOGD("open %s unsupport watchdog, errno: %s",
               watchdog_dev, strerror(errno));
  } else {
    FD_SET(watchdog_fd, &rfds);
    max_fd = max(watchdog_fd, max_fd);
  }

  /* open alive dev */
  if (alive_dev) {
    alive_fd = open(alive_dev, O_RDONLY);
    if (alive_fd < 0) {
      MODEM_LOGE("open %s  , errno: %s",
                 alive_dev, strerror(errno));
    } else {
      FD_SET(alive_fd, &rfds);
      max_fd = max(alive_fd, max_fd);
    }
  }

  /*listen assert, alive and WDG event*/
  for (;;) {
    MODEM_LOGD("%s: wait for modem assert/hangup event ...", __FUNCTION__);
    do {
      ret = select(max_fd + 1, &rfds, NULL, NULL, 0);
    } while (ret == -1 && errno == EINTR);
    if (ret > 0) {
      if (FD_ISSET(assert_fd, &rfds)) {
        fd = assert_fd;
      } else if (watchdog_fd >= 0 && FD_ISSET(watchdog_fd, &rfds)) {
        fd = watchdog_fd;
      } else if (alive_fd >= 0 && FD_ISSET(alive_fd, &rfds)) {
        fd = alive_fd;
      } else {
        MODEM_LOGD("%s: no fd is readalbe", __FUNCTION__);
        sleep(1);
        continue;
      }

      /* get wake_lock */
      modem_ctrl_enable_wake_lock(1, __FUNCTION__);

      MODEM_LOGD("%s: enter read ...", __FUNCTION__);
      numRead = read(fd, buf, sizeof(buf) - 1);
      if (numRead <= 0) {
        MODEM_LOGE("%s: read %d return %d, errno = %s", __FUNCTION__, fd,
                   numRead, strerror(errno));
        /* release wake_lock */
        modem_ctrl_enable_wake_lock(0, __FUNCTION__);
        sleep(1);
        continue;
      }
      buf[numRead] = 0; /* end with '\0' */
      MODEM_LOGD("%s: read %s from modem!\n", __FUNCTION__, buf);
      to_clients = 0;

      if (strstr(buf, MODEM_ALIVE)) {
        to_clients = 1;
        modem_ctrl_stop_wait_alive_timer();
        modem_ctrl_set_modem_state(MODEM_STATE_ALIVE);
      } else if (strstr(buf, MODEM_RESET)) {
        modem_ctrl_set_modem_state(MODEM_STATE_RESET);
        modem_ctrl_set_wait_reset_flag(0);
      } else if (strstr(buf, MODEM_ASSERT)) {
        to_clients = 1;
        modem_ctrl_set_modem_state(MODEM_STATE_ASSERT);

        /*if cp or dsp hung, if mode reset open, will reboot system */
        if (strstr(buf, "HUNG")) {
          char prop[PROPERTY_VALUE_MAX];
          int is_reset;

          memset(prop, 0, sizeof(prop));
          property_get(MODEM_RESET_PROP, prop, "0");
          is_reset = atoi(prop);
          MODEM_LOGD("%s = %s, is reset = %d\n",
                     MODEM_RESET_PROP, prop, is_reset);

          if (is_reset) {
            MODEM_LOGD("%s: dsp HUNG, reboot system\n", __FUNCTION__);
            modem_ctrl_reboot_all_system();
          }
        }
      }

      if (to_clients) {
        numWrite = modem_write_data_to_clients(buf, numRead);
        MODEM_LOGD("%s: write to clients len = %d\n", __FUNCTION__, numWrite);
      }
      /* release wake_lock */
      modem_ctrl_enable_wake_lock(0, __FUNCTION__);
    }else {
      MODEM_LOGE("%s: ret=%d, errno=%s!", __FUNCTION__, ret, strerror(errno));
      sleep(1);
    }
  }
  return NULL;
}

