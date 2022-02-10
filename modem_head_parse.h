/**
 * modem_head_parse.h ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */

#ifndef __MODEM_HEAD_PARSE_H__
#define __MODEM_HEAD_PARSE_H__
#include "modem_load.h"

int modem_head_correct_load_info(LOAD_VALUE_S *load_info);
int modem_head_get_boot_code(char *buf, uint32_t size);

#endif


