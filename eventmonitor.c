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

#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include <cutils/uevent.h>

#include "modem_control.h"
#include "eventmonitor.h"

#define UEVENT_MSG_LEN 4096

static void parse_event(const char *msg, BaseUEventInfo *Info);

struct event_client {
    char *subsystem;
    void (*handler)(BaseUEventInfo *, void *);
    void *data;
};

#define MAX_EVENT_CLIENT 10

static struct event_client g_event_client[MAX_EVENT_CLIENT];

int modem_event_register(char *subsystem,
    void (*handler)(BaseUEventInfo *, void *), void *data)
{
  int i;

  for(i = 0; i < MAX_EVENT_CLIENT; i++) {
    if (!g_event_client[i].handler)
      break;
  }

  MODEM_LOGIF("event register: subsystem = %s, i = %d", subsystem, i);

  if (i == MAX_EVENT_CLIENT)
    return -1;

  g_event_client[i].handler = handler;
  g_event_client[i].data = data;
  g_event_client[i].subsystem = subsystem;

  return 0;
}

void modem_event_unregister(char *subsystem)
{
  int i;

  MODEM_LOGIF("event unregister: subsystem = %s", subsystem);

  for (i = 0; i < MAX_EVENT_CLIENT; i++) {
      if (g_event_client[i].subsystem &&
          0 == strcmp(g_event_client[i].subsystem, subsystem)) {
        g_event_client[i].subsystem = NULL;
        g_event_client[i].handler = NULL;
        g_event_client[i].data = NULL;
        break;
      }
  }
}

static void *uevent_handle_thread(void* info)
{
  BaseUEventInfo *pInfo = (BaseUEventInfo *)info;
  void (*handler)(BaseUEventInfo *, void *);
  void* data;

  MODEM_LOGIF("handle thread!");

  handler = g_event_client[pInfo->handle_index].handler;
  data = g_event_client[pInfo->handle_index].data;
  handler(pInfo, data);
  free(pInfo);

  return 0;
}

static void modem_event_process(BaseUEventInfo *info)
{
  int i;
  void (*handler)(BaseUEventInfo *, void *) = NULL;
  pthread_t thread;

  MODEM_LOGIF("event process: subsystem = %s", info->subsystem);

  for (i = 0; i < MAX_EVENT_CLIENT; i++) {
      if (!g_event_client[i].subsystem)
        continue;

      if (0 == strcmp(g_event_client[i].subsystem, info->subsystem)) {
        handler = g_event_client[i].handler;
        break;
      }
  }

  if (handler) {
    info->handle_index = i;
    if (0 != pthread_create(&thread, NULL, uevent_handle_thread, info)) {
      MODEM_LOGE(" handle thread create error!\n");
      free(info);
    }
  }
  else {
    MODEM_LOGIF("event process: can't find client!");
    free(info);
  }
}

void modem_event_device_fd(int sock) {
  char msg[UEVENT_MSG_LEN + 2];
  int n;
  BaseUEventInfo *info;

  while ((n = uevent_kernel_multicast_recv(sock, msg, UEVENT_MSG_LEN)) > 0) {
    if (n >= UEVENT_MSG_LEN) /* overflow -- discard */
      continue;

    msg[n] = '\0';
    msg[n + 1] = '\0';

    info = (BaseUEventInfo *)malloc(sizeof(BaseUEventInfo));
    if (info) {
      parse_event(msg, info);
      modem_event_process(info);
    } else {
        MODEM_LOGE("malloc BaseUEventInfo failed!");
    }
  }
}

void *uevent_monitor_thread(void *data)
{
  struct pollfd ufd;
  int sock = -1;
  int nr;

  sock = uevent_open_socket(256 * 1024, true);
  if (-1 == sock) {
   MODEM_LOGE("%s: socket init failed !%s, %d\n", __FUNCTION__, strerror(errno), errno);
    return 0;
  }
  MODEM_LOGD("%s: sock =  %d\n", __FUNCTION__, sock);

  ufd.events = POLLIN;
  ufd.fd = sock;
  while (1) {
    ufd.revents = 0;
    nr = poll(&ufd, 1, -1);

    if (nr <= 0) continue;
    if (ufd.revents == POLLIN)
      modem_event_device_fd(sock);
  }
}

static void parse_event(const char *msg, BaseUEventInfo *Info)
{
    Info->action = NULL;
    Info->path = NULL;
    Info->subsystem = NULL;
    Info->firmware = NULL;
    Info->modem_event = NULL;
    Info->major = -1;
    Info->minor = -1;
    Info->modem_stat = -1;

    while (*msg) {
        MODEM_LOGIF("uevent %s\n", msg);
        if (!strncmp(msg, "ACTION=", 7)) {
            msg += 7;
            Info->action = msg;
        } else if (!strncmp(msg, "DEVPATH=", 8)) {
            msg += 8;
            Info->path = msg;
        } else if (!strncmp(msg, "SUBSYSTEM=", 10)) {
            msg += 10;
            Info->subsystem = msg;
        } else if (!strncmp(msg, "FIRMWARE=", 9)) {
            msg += 9;
            Info->firmware = msg;
        } else if (!strncmp(msg, "MAJOR=", 6)) {
            msg += 6;
            Info->major = atoi(msg);
        } else if (!strncmp(msg, "MINOR=", 6)) {
            msg += 6;
            Info->minor = atoi(msg);
        }else if(!strncmp(msg,"MODEM_EVENT=",12)){
            msg += 12;
            MODEM_LOGD("uevent %s\n", msg);
            Info->modem_event = msg;
        }else if(!strncmp(msg,"MODEM_STAT=",11)){
            msg += 11;
            MODEM_LOGD("uevent %s\n", msg);
            Info->modem_stat =atoi(msg);
        }

        while(*msg++);
    }
}
