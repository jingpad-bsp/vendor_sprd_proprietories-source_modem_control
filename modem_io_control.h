/**
 * modem_io_control.h ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */

#ifndef MODEM_IO_CONTROL_H_
#define MODEM_IO_CONTROL_H_

#include "modem_load.h"

/* modem region data define */
#define MAX_REGION_NAME_LEN 20
#define MAX_REGION_CNT 20

#define MODEM_READ_ALL_MEM 0xff
#define MODEM_READ_MODEM_MEM 0xfe


typedef struct modem_region_info_s {
  uint64_t address;
  uint32_t size;
  char name[MAX_REGION_NAME_LEN + 1];
}modem_region_info;

typedef struct modem_load_info_s {
  uint32_t region_cnt;
  uint64_t modem_base;
  uint32_t modem_size;
  uint64_t all_base;
  uint32_t all_size;
  modem_region_info regions[MAX_REGION_CNT];
}modem_load_info;

/* modem remote flag bit define */
#define REMOTE_CLEAR_FLAG 0x0
#define REMOTE_DDR_READY_FLAG BIT(0) /* remote to local, 1 active */
#ifdef FEATURE_PCIE_RESCAN
#define EP_SET_BAR_DONE_FLAG BIT(0)  /* remote to local, mux ddr read bit */
#define EP_RESCAN_DONE_FLAG	BIT(0) /* local to remote, mux ddr read bit, low active */
#endif

#define MODEM_IMAGE_DONE_FLAG	BIT(1)
#define MODEM_WARM_RESET_FLAG	BIT(1) /* load spl only, mux modem img done bit */
#define UBOOT_IMAGE_DONE_FLAG	BIT(2)
#define BOOT_IMAGE_DONE_FLAG	BIT(3)
#define MODEM_HEAD_DONE_FLAG	BIT(4)
#define SPL_IMAGE_DONE_FLAG	BIT(5)

#define BIT(n) (1 << (n))

int modem_lock_read(char *path);
int modem_unlock_read(char *path);
int modem_lock_write(char *path) ;
int modem_unlock_write(char *path);
int modem_get_load_info(char *path, modem_load_info *info);
int modem_set_load_info(char *path, modem_load_info *info);
int modem_set_read_region(char *path, int region);
int modem_set_write_region(char *path, int region);
int modem_get_remote_flag(char *path);
int modem_set_remote_flag(char *path, int flag);
int modem_clear_remote_flag(char *path, int flag);
int modem_ioctrl_stop(char *path);
int modem_ioctrl_start(char *path);
int modem_ioctrl_assert(char *path);
int modem_ioctrl_reboot_ext_modem(char *path);
int modem_ioctrl_poweron_ext_modem(char *path);
int modem_ioctrl_poweroff_ext_modem(char *path);

#endif  // MODEM_IO_CONTROL_H_

