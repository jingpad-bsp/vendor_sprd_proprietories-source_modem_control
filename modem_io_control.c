/**
 * modem_io_control.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <cutils/properties.h>
#include <signal.h>

#include "modem_control.h"
#include "modem_io_control.h"

 /* modem io cmd */
#define MODEM_MAGIC 'M'

#define MODEM_READ_LOCK_CMD _IO(MODEM_MAGIC, 0x1)
#define MODEM_READ_UNLOCK_CMD _IO(MODEM_MAGIC, 0x2)

#define MODEM_WRITE_LOCK_CMD _IO(MODEM_MAGIC, 0x3)
#define MODEM_WRITE_UNLOCK_CMD _IO(MODEM_MAGIC, 0x4)

#define MODEM_GET_LOAD_INFO_CMD _IOR(MODEM_MAGIC, 0x5, modem_load_info)
#define MODEM_SET_LOAD_INFO_CMD _IOW(MODEM_MAGIC, 0x6, modem_load_info)

#define MODEM_SET_READ_REGION_CMD _IOR(MODEM_MAGIC, 0x7, int)
#define MODEM_SET_WRITE_GEGION_CMD _IOW(MODEM_MAGIC, 0x8, int)

#define MODEM_GET_REMOTE_FLAG_CMD _IOR(MODEM_MAGIC, 0x9, int)
#define MODEM_SET_REMOTE_FLAG_CMD _IOW(MODEM_MAGIC, 0xa, int)
#define MODEM_CLR_REMOTE_FLAG_CMD _IOW(MODEM_MAGIC, 0xb, int)

#define MODEM_STOP_CMD _IO(MODEM_MAGIC, 0xc)
#define MODEM_START_CMD _IO(MODEM_MAGIC, 0xd)
#define MODEM_ASSERT_CMD _IO(MODEM_MAGIC, 0xe)

#define MODEM_REBOOT_EXT_MODEM_CMD _IO(MODEM_MAGIC, 0xf)
#define MODEM_POWERON_EXT_MODEM_CMD _IO(MODEM_MAGIC, 0x10)
#define MODEM_POWEROFF_EXT_MODEM_CMD _IO(MODEM_MAGIC, 0x11)

static int modem_iocmd(unsigned int cmd, void* arg, char *path) {
  int ret = -1;
  int fd;

  fd = open(path, O_RDWR);
  if (fd < 0) {
    MODEM_LOGE("%s: %s failed, error: %s", __FUNCTION__, path,
              strerror(errno));
    return -1;
  }

  ret = ioctl(fd, cmd, (unsigned long)arg);
  if (ret) {
    MODEM_LOGE("%s! ret = %d, errno(%s)\n", __FUNCTION__, ret, strerror(errno));
  }

  close(fd);
  return ret;
}

int modem_lock_read(char *path) {
  MODEM_LOGIF("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_READ_LOCK_CMD, NULL, path);
}

int modem_unlock_read(char *path) {
  MODEM_LOGIF("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_READ_UNLOCK_CMD, NULL, path);
}

int modem_lock_write(char *path) {
  MODEM_LOGIF("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_WRITE_LOCK_CMD, NULL, path);
}

int modem_unlock_write(char *path) {
  MODEM_LOGIF("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_WRITE_UNLOCK_CMD, NULL, path);
}

int modem_get_load_info(char *path, modem_load_info *info) {
  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_GET_LOAD_INFO_CMD, info, path);
}

int modem_set_load_info(char *path, modem_load_info *info) {
  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_SET_LOAD_INFO_CMD, info, path);
}

int modem_set_read_region(char *path, int region) {
  int param = region;

  MODEM_LOGD("%s, region = %d, path=%s!\n", __FUNCTION__, region, path);
  return modem_iocmd(MODEM_SET_READ_REGION_CMD, &param, path);
}

int modem_set_write_region(char *path, int region) {
  int param = region;

  MODEM_LOGD("%s, region = %d, path=%s!\n", __FUNCTION__, region, path);
  return modem_iocmd(MODEM_SET_WRITE_GEGION_CMD, &param, path);
}


int modem_get_remote_flag(char *path) {
  int param = -1;

  modem_iocmd(MODEM_GET_REMOTE_FLAG_CMD, &param, path);

  MODEM_LOGIF("%s flag = 0x%x, path=%s!\n", __FUNCTION__, param, path);
  return param;
}

int modem_set_remote_flag(char *path, int flag) {
  int param = flag;

  MODEM_LOGD("Set flag = 0x%x, path=%s!\n", flag, path);
  return modem_iocmd(MODEM_SET_REMOTE_FLAG_CMD, &param, path);
}

int modem_clear_remote_flag(char *path, int flag) {
  int param = flag;

  modem_iocmd(MODEM_CLR_REMOTE_FLAG_CMD, &param, path);

  MODEM_LOGD("%s flag = 0x%x, path=%s!\n", __FUNCTION__, param, path);
  return param;
}

int modem_ioctrl_stop(char *path) {
  int param = 0;

  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);
  return modem_iocmd(MODEM_STOP_CMD, &param, path);
}

int modem_ioctrl_start(char *path) {
  int param = 0;

  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);
  return modem_iocmd(MODEM_START_CMD, &param, path);
}

int modem_ioctrl_assert(char *path) {
  int param = 0;

  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);
  return modem_iocmd(MODEM_ASSERT_CMD, &param, path);
}

int modem_ioctrl_reboot_ext_modem(char *path) {
  MODEM_LOGD("%s!, path=%s\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_REBOOT_EXT_MODEM_CMD, NULL, path);
}

int modem_ioctrl_poweron_ext_modem(char *path) {
  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_REBOOT_EXT_MODEM_CMD, NULL, path);
}

int modem_ioctrl_poweroff_ext_modem(char *path) {
  MODEM_LOGD("%s, path=%s!\n", __FUNCTION__, path);

  return modem_iocmd(MODEM_REBOOT_EXT_MODEM_CMD, NULL, path);
}

