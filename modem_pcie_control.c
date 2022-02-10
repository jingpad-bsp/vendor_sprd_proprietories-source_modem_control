/*
 *  modem_pcie_control.c - rescan pcie ep device.
 *
 *
 *  Copyright (C) 2019 spreadtrum Communications Inc.
 *
 *  History:
 *  2019-03-01 wenping.zhou
 *  Initial version.
 *
 */
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <cutils/properties.h>
#include <cutils/android_filesystem_config.h>

#include "modem_control.h"
#include "eventmonitor.h"

/*
 * all the ep pcie parameters can be config in borad,
 * it can be read in sys/bus/pci/device/xxx/xxx.
 * #define PCIE_EP_VENDOR_ID "0x16c3"
 * #define PCIE_EP_DEVICE_ID "0xabcd"
 * #define PCIE_EP_DEVICE_CLASS_ID "0x0d8000"
 * #define PCIE_EP_BRIDGE_CLASS_ID "0x060400"
*/
#define PCIE_DEVICE_PATH "ro.vendor.modem.ep.device.path"
#define PCIE_EP_VENDOR_ID "ro.vendor.modem.ep.vendor.id"
#define PCIE_EP_DEVICE_ID "ro.vendor.modem.ep.device.id"
#define PCIE_EP_CLASS_ID "ro.vendor.modem.ep.class.id"

#define EP_DEVICE_DIR "devices/"

#define PCIE_EP_VENDOR_DEV "vendor"
#define PCIE_EP_DEVICE_DEV "device"
#define PCIE_EP_CLASS_DEV "class"

#define PCIE_EP_REMOVE_DEV "remove"
#define PCIE_EP_RESCAN_DEV "rescan"

#define MAX_EP_PATH_LEN 128


static char device_remove_path[MAX_EP_PATH_LEN];
static char bridge_remove_path[MAX_EP_PATH_LEN];
static char rescan_path[MAX_EP_PATH_LEN];


/* /bus/pci/devices
0000:10:00.0  -- pcie 0 bridge(special device, device 0)
0000:11:00.0  -- pcie 0 device  1
0001:00:00.0
0001:01:00.0
*/


#define PCI_SUBSYSTEM "pci"

static int modem_write_file(char *file, char*str)
{
  int fd;
  ssize_t size;

  fd = open(file, O_WRONLY);

  if (fd < 0) {
    MODEM_LOGE("fd =%d: open file %s, error: %s", fd, file, strerror(errno));
    return -1;
  }

  size = strlen(str) *sizeof(char);
  if (write(fd, str, size) != size) {
    MODEM_LOGE("Could not write %s in %s, error :%s", str, file,
               strerror(errno));
    close(fd);
    return -2;
  }

  close(fd);

  return 0;
}

static void modem_chane_ep_device_owner(void)
{
  /* not normal mode, modem control is root user, do nothing */
  if (modem_ctrl_get_boot_mode() != BOOT_MODE_NORMAL)
    return;

  /*
   * in ud710_2h10/rootdir/root/init.ud710_2h10.rc
   * on property:vendor.modem.pcie.ep.rescan=true
   * chown system root sys/bus/pci/devices/xxx/remove
   * chown system root sys/bus/pci/devices/xxx/remove
   */
  MODEM_LOGD("change the ep owner!");
  // system("chown system sys/bus/pci/devices/*/remove");

  if (chown(rescan_path, AID_SYSTEM, AID_ROOT))
    MODEM_LOGE("chown %s, %s", rescan_path, strerror(errno));
  if (chown(device_remove_path, AID_SYSTEM, AID_ROOT))
    MODEM_LOGE("chown %s, %s", device_remove_path, strerror(errno));
  if (chown(bridge_remove_path, AID_SYSTEM, AID_ROOT))
    MODEM_LOGE("chown %s, %s", bridge_remove_path, strerror(errno));
}

static int modem_match_something(DIR *dir, char *name, char *prop)
{
  int match = 0, fd, len, i;
  char buf[20];
  char value[PROPERTY_VALUE_MAX];

  if (property_get(prop, value, NULL) <=0) {
    MODEM_LOGE("%s: get %s prop err!", __FUNCTION__, prop);
    return 0;
  }
  MODEM_LOGIF("%s: prop=%s, value=%s", __FUNCTION__, prop, value);

  fd = openat(dirfd(dir), name, O_RDONLY);
  if (fd < 0) {
    MODEM_LOGE("%s: open %s error: %s", __FUNCTION__, name,
               strerror(errno));
    return 0;
  }

  len = read(fd, buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = 0;
    MODEM_LOGIF("%s:value=%s, buf=%s, len=%d.", __FUNCTION__, value, buf, len);
    /* the buf may be end with \n or \r*/
    for (i = 0; buf[i]; i++) {
      if (buf[i] == '\n' || buf[i] == '\r') {
        buf[i] = 0;
        break;
      }
    }

    if (0 == strcmp(value, buf))
      match = 1;
  } else {
      MODEM_LOGE("%s: read %s error: %s", __FUNCTION__, name,
                 strerror(errno));
  }

  close(fd);
  return match;
}

static int modem_find_ep_device(char *path)
{
  DIR *dir;
  int find = 0;

  MODEM_LOGIF("%s:file ep dev in %s", __FUNCTION__, path);

  dir = opendir(path);
  if (dir) {
    /* match vendor and device */
    if (modem_match_something(dir, PCIE_EP_VENDOR_DEV, PCIE_EP_VENDOR_ID)
        && modem_match_something(dir, PCIE_EP_DEVICE_DEV, PCIE_EP_DEVICE_ID)
        && modem_match_something(dir, PCIE_EP_CLASS_DEV, PCIE_EP_CLASS_ID))
      find = 1;
  } else {
      MODEM_LOGE("%s: opendir %s error: %s", __FUNCTION__, path,
                 strerror(errno));
  }

  closedir(dir);
  return find;
}

static int modem_init_ep_device_path(void)
{
  char path[PROPERTY_VALUE_MAX] = {0};
  char dir_path[PROPERTY_VALUE_MAX] = {0};
  char ep_device_name[MAX_EP_PATH_LEN];
  DIR *dir;
  struct dirent *de;
  int find = 0;
  long start;

  if (property_get(PCIE_DEVICE_PATH, dir_path, NULL) <=0) {
    MODEM_LOGE("%s: cat dev path prop err!", __FUNCTION__);
    return -1;
  }

  /* init scan path */
  strncpy(rescan_path, dir_path, sizeof(rescan_path) - 1);
  strncat(rescan_path, PCIE_EP_RESCAN_DEV, sizeof(rescan_path) - strlen(rescan_path) - 1);
  MODEM_LOGD("rescan path is %s", rescan_path);

  strncat(dir_path, EP_DEVICE_DIR, sizeof(dir_path) - 1);
  dir = opendir(dir_path);
  if (!dir) {
      MODEM_LOGE("%s: opendir %s, error: %s", __FUNCTION__, dir_path,
                 strerror(errno));
    return -2;
  }

  /* save start pos */
  start = telldir(dir);

  /* 1th, find ep device path */
  while ((de = readdir(dir))) {
    MODEM_LOGIF("file is %s \n", de->d_name);
    if (de->d_type == DT_LNK) {
      strncpy(path, dir_path, sizeof(path) - 1);
      strncat(path, de->d_name, sizeof(path) - 1);

      if (modem_find_ep_device(path)) {
        strncpy(ep_device_name, de->d_name, sizeof(ep_device_name) - 1);
        find = 1;

        /* init ep device remove path */
        snprintf(device_remove_path, sizeof(device_remove_path),
                 "%s/%s", path, PCIE_EP_REMOVE_DEV);
        MODEM_LOGD("device remove path is %s", device_remove_path);
        break;
      }
    }
  }

  /* 2th, find ep bridge path, the ep name is must in its bridge dir */
  if (find) {
    seekdir(dir, start);
    while ((de = readdir(dir))) {
      if (de->d_type == DT_LNK) {
        strncpy(path, dir_path, sizeof(path) - 1);
        snprintf(path, sizeof(path), "%s%s/%s",
                 dir_path, de->d_name, ep_device_name);
        MODEM_LOGIF("try ep path is %s\n", path);

        if (0 == access(path, F_OK)) {
          find += 1;
          /* init ep bridge remove path */
          snprintf(bridge_remove_path, sizeof(bridge_remove_path),
                  "%s%s/%s", dir_path, de->d_name, PCIE_EP_REMOVE_DEV);
          MODEM_LOGD("bridge remove path is %s", bridge_remove_path);
          break;
        }
      }
    }
  }

  closedir(dir);
  return (find != 2);
}

int modem_rescan_ep_device(void)
{
  int ret;

  MODEM_LOGD("%s: !\n", __FUNCTION__);

  modem_chane_ep_device_owner();

  /* remove ep device*/
  ret = modem_write_file(device_remove_path, "1");
  if (ret)
    return ret;

  /* remove ep bridge */
  ret = modem_write_file(bridge_remove_path, "1");
  if (ret)
    return ret;

  /* rescan ep device */
  ret = modem_write_file(rescan_path, "1");

  return ret;

#if 0
  /* remove ep device*/
  system("echo 1 > /sys/bus/pci/devices/0000:11:00.0/remove");

  /* remove ep bridge */
  system("echo 1 > /sys/bus/pci/devices/0000:10:00.0/remove");

  /* rescan ep device */
  system("echo 1 > /sys/bus/pci/rescan");
#endif
}

static void modem_pcie_event(BaseUEventInfo *info, void *data)
{
  char path[MAX_EP_PATH_LEN];
  char *dev;
  int find_cnt = 0;

  MODEM_LOGIF("modem_pcie_event: %s", info->action);

  if (strcmp(info->action, "add"))
    return;

  /* if the ep device path has been add, all the paths have been add  */
  strncpy(path, device_remove_path, sizeof(path) - 1);

  /* go to end */
  dev = path + strlen(path);
  while(path < dev) {
    dev--;
    if (*dev =='/') {
      find_cnt++;
      /* find the first '/',  0 replace it */
      if (find_cnt == 1)
        *dev = 0;

      /* find the second '/',  dev+1 */
      if (find_cnt == 2) {
        dev++;
        break;
      }
    }
  }

  MODEM_LOGIF("dev = %s, path=%s", dev, info->path);

  if (strstr(info->path, dev))
    modem_chane_ep_device_owner();
}

void modem_pcie_init(void)
{
  if (modem_init_ep_device_path())
    return;

  modem_chane_ep_device_owner();
  modem_event_register(PCI_SUBSYSTEM, modem_pcie_event, NULL);
}

