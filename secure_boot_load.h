/**
 * secure_boot_load.h ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#ifndef SECURE_BOOT_LOAD_H_
#define SECURE_BOOT_LOAD_H_

void secure_boot_init(void);
void secure_boot_verify_all(void);
int secure_boot_load_img(IMAGE_LOAD_S *img);
void secure_boot_get_patiton_info(IMAGE_LOAD_S *img,
  unsigned int *boot_offset, size_t *size);
void secure_boot_unlock_ddr(void);
void secure_boot_set_flag(int load_type);
#endif
