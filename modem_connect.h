/**
 * modem_connect.h ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#ifndef MODEM_CONNECT_H_
#define MODEM_CONNECT_H_

int modem_write_data_to_clients(void *buf, int size);
int modem_read_data_from_clients(void *buf, int size);
void *modem_setup_clients_connect(void);

#endif
