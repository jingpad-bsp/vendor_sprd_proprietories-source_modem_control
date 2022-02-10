/**
 * modem_connect.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <pthread.h>

#include "modem_control.h"
#include "modem_connect.h"

#define MAX_CLIENT_NUM          20
#define BUFFER_SIZE             128
#define TIME_FOR_MD_DUMP        (60 * 5) // modem memory dump time out (5 min)

#define SOCKET_NAME_MODEMD   "modemd"
#define MODEM_SAVE_DUMP_PROP    "persist.vendor.sys.modem.save_dump"
#define WIFI_ONLY_IMET_PROP     "vendor.sys.wifionly.imei"
#define WIFI_ONLY_VERSION_PROP  "persist.vendor.sys.wifionly"

typedef enum {
    MODEMCON_STATE_OFFLINE   = 0,
    MODEMCON_STATE_ALIVE     = 1,
    MODEMCON_STATE_ASSERT    = 2,
    MODEMCON_STATE_RESET     = 3,
} ModemState;


enum {
  DUMP_BEGIN = 0,
  DUMP_GOING,
  DUMP_COMPLETE
};

static int s_fdModemCtrlRead = -1;   // used for modem control read modem reset or block
static int s_fdModemCtrlWrite = -1;  // used for info modem control modem reset or block
static int s_clientFd[MAX_CLIENT_NUM];          // modem control client fd
static int s_notifypipe[] = {-1, -1};           // listen thread notify by pipe
static bool s_needResetModem = true;    // P-ARM Modem Assert doesn't need to reset modem
static bool s_wakeLocking = false;
static ModemState s_modemState = MODEMCON_STATE_OFFLINE;
static pthread_mutex_t s_dumpMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_dumpCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t s_writeMutex = PTHREAD_MUTEX_INITIALIZER;

/* start / stop nvitemd accodring modem state */
static void control_nvitemd(int isStart);

/* before modem control write modem state to clients, process according modem state */
static void modem_ctrl_process_message(void *buf, int size);

/* recive modem blocked from clinet: rild */
static int dispatch_modem_blocked(int blockFd);

/* when modem assert, and need reset, and need save dump log,
 * wait for slogmodem dump complete or wait for 5 minutes, then send modem reset */
static void *write_reset_to_modem_ctrl(void *ctrlFd);

/* read message from clients: slogmodem/audio/rild/network/aprd/modemnotifier */
static void *modem_ctrl_read_thread(void);


int modem_write_data_to_clients(void *buf, int size)
{
  int i;
  int ret = 0;

  pthread_mutex_lock(&s_writeMutex);

  modem_ctrl_process_message(buf, size);

  /* info socket clients that modem is assert/hangup/blocked */
  for (i = 0; i < MAX_CLIENT_NUM; i++) {
      if (s_clientFd[i] >= 0) {
          ret = write(s_clientFd[i], buf, size);
          MODEM_LOGD("write %d bytes to s_clientFd[%d]: %d", size, i,
                      s_clientFd[i]);
          if (ret < 0) {
              MODEM_LOGE("reset client_fd[%d] = -1, errno: %d, err: %s",
                          i, errno, strerror(errno));
              close(s_clientFd[i]);
              s_clientFd[i] = -1;
          }
      }
  }

  pthread_mutex_unlock(&s_writeMutex);
  return ret;
}

int modem_read_data_from_clients(void *buf, int size)
{
  int cnt = 0;
  while (s_fdModemCtrlRead < 0) {
    MODEM_LOGE("%s: s_fdModemCtrlRead = %d", __FUNCTION__, s_fdModemCtrlRead);
    return -1;
  }

  cnt = read(s_fdModemCtrlRead, buf, size);
  if (cnt <= 0) {
    MODEM_LOGE("%s: errno(%d) %s\n", __FUNCTION__, errno,
               strerror(errno));
  }
  return cnt;
}

void *modem_setup_clients_connect(void) {
    int sfd, n, index;
    int filedes[2];
    pthread_t tid;
    pthread_attr_t attr;
    for (index = 0; index < MAX_CLIENT_NUM; index++) {
        s_clientFd[index] = -1;
    }
    /* s_notifypipe[1]: used for notify;
     * s_notifypipe[0]: used for receiving notify,then update the listen fds
     */
    if (pipe(s_notifypipe) < 0) {
        MODEM_LOGE("pipe error!\n");
    }

    if (pipe(filedes) < 0) {
        RLOGE("Error in pipe() errno:%d", errno);
    }
    s_fdModemCtrlRead = filedes[0];
    s_fdModemCtrlWrite = filedes[1];

    sfd = socket_local_server(SOCKET_NAME_MODEMD,
                              ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if (sfd < 0) {
        MODEM_LOGE("%s: cannot create local socket server, errno: %d, err: %s",
                    __FUNCTION__, errno, strerror(errno));
        return NULL;
    }
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, (void*)modem_ctrl_read_thread, NULL);
    for (;;) {
        MODEM_LOGD("%s: Waiting for new connect ...", __FUNCTION__);
        if ((n = accept(sfd, NULL, NULL)) == -1) {
            MODEM_LOGE("%s  accept error\n", __FUNCTION__);
            continue;
        }
        MODEM_LOGD("%s: accept client n=%d", __FUNCTION__, n);
        for (index = 0; index < MAX_CLIENT_NUM; index++) {
            if (s_clientFd[index] == -1) {
                s_clientFd[index] = n;
                write(s_notifypipe[1], "0", sizeof("0"));
                MODEM_LOGD("%s: fill%d to client[%d]", __FUNCTION__, n, index);

                // infor client modem current state
                const char *modemState = NULL;
                if (s_modemState == MODEMCON_STATE_OFFLINE) {
                    modemState = "Modem State: Offline";
                } else if (s_modemState == MODEMCON_STATE_ALIVE) {
                    modemState = "Modem State: Alive";
                } else if (s_modemState == MODEMCON_STATE_ASSERT) {
                    modemState = "Modem State: Assert";
                } else if (s_modemState == MODEMCON_STATE_RESET) {
                    modemState = "Modem State: Reset";
                } else {
                    modemState = "Modem State: Unknown";
                }
                write(s_clientFd[index], modemState, strlen(modemState) + 1);
                break;
            }
            /* if client_fd arrray is full, just fill the new socket to the
             * last element */
            if (index == MAX_CLIENT_NUM - 1) {
                MODEM_LOGD("%s: client full, just fill %d to client[%d]",
                            __FUNCTION__, n, index);
                close(s_clientFd[index]);
                s_clientFd[index] = n;
                write(s_notifypipe[1], "0", 2);
                break;
            }
        }
    }
    close(s_notifypipe[1]);
    s_notifypipe[1] = -1;

    close(s_fdModemCtrlRead);
    s_fdModemCtrlRead = -1;

    close(s_fdModemCtrlWrite);
    s_fdModemCtrlWrite = -1;

    return NULL;
}

static void *modem_ctrl_read_thread(void) {
    char controlinfo[BUFFER_SIZE] = {0};
    int readnum = -1;
    int i = 0 , num = 0;
    fd_set engpcFds;
    int nengfds = 0;  // max engpc_fd;
    int later_reset = 0;
    int state = -1;

    pthread_condattr_t reset_attr;
    pthread_condattr_init(&reset_attr);
    pthread_condattr_setclock(&reset_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&s_dumpCond, &reset_attr);

    MODEM_LOGD("%s: enter", __FUNCTION__);
    for (;;) {
        FD_ZERO(&engpcFds);
        nengfds = s_notifypipe[0] + 1;
        FD_SET(s_notifypipe[0], &engpcFds);
        for (i = 0; i < MAX_CLIENT_NUM; i++) {
            if (s_clientFd[i] != -1) {
                FD_SET(s_clientFd[i], &engpcFds);
                if (s_clientFd[i] >= nengfds)
                    nengfds = s_clientFd[i] + 1;
            }
        }
        MODEM_LOGD("%s: begin select", __FUNCTION__);
        num = select(nengfds, &engpcFds, NULL, NULL, NULL);
        MODEM_LOGD("%s: after select n = %d",  __FUNCTION__, num);
        if (num > 0 && (FD_ISSET(s_notifypipe[0], &engpcFds))) {
            char buf[32] = {0};
            memset(buf, 0, sizeof(buf));
            read(s_notifypipe[0], buf, sizeof(buf) - 1);
            MODEM_LOGD("%s: client cnct to modemd num = %d, buf = %s",
                        __FUNCTION__, num, buf);
            if (num == 1) {
                continue;
            }
        }
        for (i = 0; (i < MAX_CLIENT_NUM) && (num > 0); i++) {
            int nfd = s_clientFd[i];
            if (nfd != -1 && FD_ISSET(nfd, &engpcFds)) {
                num--;
                MODEM_LOGD("%s: begin read ", __FUNCTION__);
                memset(controlinfo, 0, sizeof(controlinfo));
                readnum =  read(nfd, controlinfo, sizeof(controlinfo));
                MODEM_LOGD("%s: after read %s", __FUNCTION__, controlinfo);
                if (readnum <= 0) {
                    close(s_clientFd[i]);
                    s_clientFd[i] = -1;
                } else {
                  /* get dump state. */
                  if (strstr(controlinfo, "SLOGMODEM DUMP BEGIN")) {
                    state = DUMP_BEGIN;
                  } else if (strstr(controlinfo, "SLOGMODEM DUMP ONGOING")) {
                    state = DUMP_GOING;
                  } else if (strstr(controlinfo, "SLOGMODEM DUMP COMPLETE")) {
                    state = DUMP_COMPLETE;
                  }

                  if (strstr(controlinfo, "Modem Blocked")) {
                    later_reset = dispatch_modem_blocked(s_fdModemCtrlWrite);
                  } else if (strstr(controlinfo, "AGDSP Assert")) {
                    modem_write_data_to_clients(controlinfo, readnum);
                  } else if (DUMP_COMPLETE == state) {
                        pthread_mutex_lock(&s_dumpMtx);
                        pthread_cond_signal(&s_dumpCond);
                        pthread_mutex_unlock(&s_dumpMtx);
			MODEM_LOGD("send dump complete.");
                        /* modem block, after dump complete, reset modem. */
                        if (later_reset) {
                          MODEM_LOGD("%s: block, later reset.", __FUNCTION__);
                          later_reset = false;
                          if (s_fdModemCtrlWrite >= 0)
                           write(s_fdModemCtrlWrite, "Modem Reset", sizeof("Modem Reset"));
                        }
                    }
                }
            }
        }
    }
    close(s_notifypipe[0]);
    s_notifypipe[0] = -1;
    return NULL;
}

/* control nvitemd  para: 0, stop; !0 , start */
static void control_nvitemd(int isStart) {
    MODEM_LOGD("control nvitemd! isStart %d", isStart);
    if (isStart == 0) {
        property_set("ctl.stop", "vendor.cp_diskserver");
    } else {
        property_set("ctl.start", "vendor.cp_diskserver");
    }
}

// start only when need to dump after md assert
static void *write_reset_to_modem_ctrl(void *ctrlFd) {
    int fd = *((int *)ctrlFd);
    int ret = -1;
    struct timespec tv;

    clock_gettime(CLOCK_MONOTONIC, &tv);
    // wait 5 min for timeout
    tv.tv_sec += TIME_FOR_MD_DUMP;

    MODEM_LOGD("reset_md_thread wait dump complete...");

    pthread_mutex_lock(&s_dumpMtx);
    // wait for reset signal, dump completed or timeout
    pthread_cond_timedwait(&s_dumpCond, &s_dumpMtx, &tv);
    pthread_mutex_unlock(&s_dumpMtx);

    MODEM_LOGD("dump complete, Write Prepare Reset to modem_control.\n");

    ret = write(fd, "Prepare Reset", sizeof("Prepare Reset"));
    if (ret <= 0) {
        MODEM_LOGE("write_reset_to_modem_ctrl: write reset failed. errno: %d, error: %s",
                    errno, strerror(errno));
    }

    return NULL;
}

static void readIMEI() {
    int ret = -1;
    int fd = -1;
    int j = 0;
    fd_set rfds;
    char *device = "/dev/stty_lte2";
    char *atCmd = "AT+CGSN";
    char prop[PROPERTY_VALUE_MAX] = {0};
    char imei[PROPERTY_VALUE_MAX] = {0};

    property_get(WIFI_ONLY_IMET_PROP, prop, "");
    if (strlen(prop) >0) {
        return;
    }

    fd = open(device, O_RDWR | O_NONBLOCK);
    MODEM_LOGD("%s: open dev: %s, fd = %d", __func__, device, fd);

    if (fd < 0) {
        MODEM_LOGE("%s: open %s failed, error: %s", __func__, device,
                    strerror(errno));
        return;
    }
    ret = write(fd, atCmd, strlen(atCmd) + 1);

    if (ret < 0) {
        MODEM_LOGE("%s: write %d failed, error:%s", __func__, fd,
                    strerror(errno));
        close(fd);
        return;
    }

    MODEM_LOGD("write done and read start");
    //usleep(100*1000);
    memset(prop, 0, sizeof(prop));

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    ret = select(fd + 1, &rfds, NULL, NULL, NULL);

    if (ret <= 0) {
        MODEM_LOGD("read error hanppend!");
        return;
    }

    do {
        ret = read(fd, prop, sizeof(prop) - 1);
    } while (ret < 0 && errno == EINTR);
    if (ret <= 0) {
        MODEM_LOGE("%s: read %d return %d, errno = %s", __func__, fd, ret,
                    strerror(errno));
        close(fd);
        return;
    }

    MODEM_LOGD("%s: read imei : %s is OK", __func__, prop);

    int count = strlen(prop);
    for (int i = 0; i < count; i++) {
        if (prop[i] >= '0' && prop[i] <= '9') {
            imei[j] = prop[i];
            j++;
        }
    }

    MODEM_LOGD("%s: read imei2 : %s is OK", __func__, imei);
    property_set(WIFI_ONLY_IMET_PROP, imei);
    close(fd);
}

static void modem_ctrl_process_message(void *buf, int size) {
    const char *message = (const char *)buf;

	MODEM_LOGD("process_message(%d) : %s",  size, message);

    if (strstr(message, "Modem Alive")) {
        char prop[PROPERTY_VALUE_MAX] = {0};
        s_modemState = MODEMCON_STATE_ALIVE;
        /* start nvitemd */
        control_nvitemd(1);

        property_get(WIFI_ONLY_VERSION_PROP, prop, "false");
        MODEM_LOGD("wifionly get wifionly prop : %s", prop);

        if (!strcmp(prop, "true")) {
            MODEM_LOGD("wifionly to start read imei");
            readIMEI();
        }
    } else if (strstr(message, "Modem Assert")) {
        if (strstr(message, "P-ARM Modem Assert")) {
            s_needResetModem = false;
        }

        if (s_needResetModem) {
            s_modemState = MODEMCON_STATE_ASSERT;
            /* stop nvitemd */
            control_nvitemd(0);

            int isReset = 0, isDump = 0;
            char prop[PROPERTY_VALUE_MAX] = {0};
            property_get(MODEM_RESET_PROP, prop, "0");
            isReset = atoi(prop);
			property_get(MODEM_SAVE_DUMP_PROP, prop, "0");
            isDump = atoi(prop);
            MODEM_LOGD("reload modem: %d, dump: %d", isReset, isDump);
            // if it need save dump, it will not reset when receive modem assert
            // it will reset when receive SLOGMODEM DUMP COMPLETE or timeout
            if (isReset) {

                if (isDump) {
                    pthread_t reset_tid;
                    pthread_attr_t reset_attr;

                    pthread_attr_init(&reset_attr);
                    pthread_attr_setdetachstate(&reset_attr, PTHREAD_CREATE_DETACHED);
                    pthread_create(&reset_tid, &reset_attr,
                            (void *)write_reset_to_modem_ctrl, &s_fdModemCtrlWrite);
                } else {
                    write(s_fdModemCtrlWrite, "Prepare Reset", sizeof("Prepare Reset"));
                }
            }
        } else {
            s_needResetModem = true;
        }
    } else if (strstr(message, "Modem Reset")) {
        /* stop nvitemd */
        control_nvitemd(0);
        write(s_fdModemCtrlWrite, "Modem Reset", sizeof("Modem Reset"));
    }
}

static int dispatch_modem_blocked(int blockFd) {
    int ret, isReset, isDump, isWait = 0;;
    int loopFd;
    char loopDev[PROPERTY_VALUE_MAX] = {0};
    char prop[PROPERTY_VALUE_MAX], buffer[BUFFER_SIZE] = {0};
    char atStr[32] = "AT\r";

    ret = write(blockFd, "Modem Blocked", sizeof("Modem Blocked"));

    pthread_mutex_lock(&s_writeMutex);
    if (s_modemState != MODEMCON_STATE_ALIVE) {
        pthread_mutex_unlock(&s_writeMutex);
        return 0;
    }
    s_modemState = MODEMCON_STATE_ASSERT;
    pthread_mutex_unlock(&s_writeMutex);
    system("echo load_modem_img >/sys/power/wake_lock");
    s_wakeLocking = true;

    property_get("ro.vendor.modem.loop", loopDev, "/dev/spipe_lte0");
    loopFd = open(loopDev, O_RDWR | O_NONBLOCK);
    MODEM_LOGD("%s: open loop dev: %s, fd = %d", __func__, loopDev, loopFd);
    if (loopFd < 0) {
        MODEM_LOGE("%s: open %s failed, error: %s", __func__, loopDev,
                    strerror(errno));
        goto RAW_RESET;
    }
    ret = write(loopFd, atStr, sizeof(atStr) );
    if (ret < 0) {
        MODEM_LOGE("%s: write %s failed, error:%s", __func__, loopDev,
                    strerror(errno));
        close(loopFd);
        goto RAW_RESET;
    }
    usleep(100 * 1000);
    memset(buffer, 0, sizeof(buffer));
    do {
        ret = read(loopFd, buffer, sizeof(buffer) - 1);
    } while (ret < 0 && errno == EINTR);
    if (ret <= 0) {
        MODEM_LOGE("%s: read %d return %d, errno = %s", __func__, loopFd, ret,
                    strerror(errno));
        close(loopFd);
        goto RAW_RESET;
    }
    if (!strcmp(buffer, atStr)) {
        MODEM_LOGD("%s: loop spipe %s is OK", __func__, loopDev);
    }
    close(loopFd);

RAW_RESET:
    property_get(MODEM_SAVE_DUMP_PROP, prop, "0");
    isDump = atoi(prop);
    /* isDump open, wait 5s for modem assert and update cache to ddr. */
    if (isDump)
      sleep(5);

    MODEM_LOGE("Info all the sock clients that modem is blocked");
    modem_write_data_to_clients("Modem Blocked", sizeof("Modem Blocked"));

    /* reset or not according to property */
    memset(prop, 0, sizeof(prop));
    property_get(MODEM_RESET_PROP, prop, "0");
    isReset = atoi(prop);
    control_nvitemd(0);  // stop nvitemd
    if (isReset) {
		/* modem block also must wait dump complete to reset modem. */
		if (isDump) {
		  isWait = 1;
		} else {
		  MODEM_LOGD("%s: reset is enabled, reload modem...", __func__);
		  if (blockFd >= 0) {
			ret = write(blockFd, "Modem Reset", sizeof("Modem Reset"));
		  }
		  MODEM_LOGD("write sfd: %d, ret: %d, errno: %s", blockFd, ret,
					  strerror(errno));
		}
    } else {
        MODEM_LOGD("%s: reset is not enabled , not reset", __func__);
    }
    s_wakeLocking = false;
    system("echo load_modem_img >/sys/power/wake_unlock");
	return isWait;
}
