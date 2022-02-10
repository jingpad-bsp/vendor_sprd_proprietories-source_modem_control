#ifndef _EVENTMONITOR_H
#define _EVENTMONITOR_H
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
/*
 * Eventmonitor.c - monitor or kernel uevent
 *
 *
 *  Copyright (C) 2019 spreadtrum Communications Inc.
 *
 *  History:
 *  2019-03-01 yunhu, wenping.zhou
 *  Initial version.
 *
 */

typedef struct BaseUEventInfo_T {
    const char *action;
    const char *path;
    const char *subsystem;
    const char *firmware;
    const char *modem_event;
    int  modem_stat;
    int major;
    int minor;
    int handle_index;
}BaseUEventInfo;

void *uevent_monitor_thread(void* data);
int  modem_event_register(char *subsystem,
    void (*handler)(BaseUEventInfo *, void *), void *data);
void modem_event_unregister(char *subsystem);
#endif
