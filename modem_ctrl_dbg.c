/*
 *  modem_ctrl_dbg.c - debug modem_control.
 *
 *  Copyright (C) 2018 spreadtrum Communications Inc.
 *
 *  History:
 *  2018-10-22 han.zhou
 *  2019-03-22 wenping.zhou
 *  Initial version.
 *
 */
#include "modem_control.h"
#include "modem_io_control.h"
#include "modem_load.h"

static modem_load_info g_cp_load_info;
static modem_load_info g_sp_load_info;
#ifdef FEATURE_EXTERNAL_MODEM
static modem_load_info g_dp_load_info;
#endif

#define CP_DEV_PATH "/dev/modem"
#define SP_DEV_PATH "dev/pmsys"
#define DP_DEV_PATH "/dev/dpsys"

#define MAX_ONECE_READ_SIZE 32*1024*1024
#define INVALID_INDEX 0xFFFF

#define    ERROR_OPEN_DIR 1
#define    ERROR_OPEN_READ_FILE 2
#define    ERROR_OPEN_WRITE_FILE 3
#define    ERROR_READ_FILE 4
#define    ERROR_WRITE_FILE 5
#define    ERROR_INVALID_INDEX 6
#define    ERROR_UNDEFINE_SYSTEM 7
#define    ERROR_UNDEFINE_CMD 8
#define    ERROR_GET_LOAD_INFO 9

enum {
  MODEM_CP = 0,
  MODEM_SP,
  MODEM_DP,
  MODEM_CNT
};

enum {
  ACTION_HELP = 0,
  ACTION_GET_INFO,
  ACTION_DUMP_MEMORY,
  ACTION_CNT
};

#define DUMP_CMD "dump"
#define GET_CMD "get"
#define MODEM_STORE_PATH "/data/modem_dump/"

typedef struct modem_cmd_tag {
    uint32_t action;
    uint32_t system;
    uint32_t index;
}modem_cmd;

static int usage(void) {
    fprintf(stdout,
            "\n"
            "get modem info\n"
            "==== Usage: ====\n"
            "[get] [cp]\n"
            "[get] [sp]\n"
            #ifdef FEATURE_EXTERNAL_MODEM
            "[get] [dp]\n"
            #endif
            "\n"
            "dump modem memory, the dump file\n"
            "will store in /data/modem_dump/\n"
            "==== Usage: ====\n"
            "[dump] [cp] [index]\n"
            "[dump] [sp] [index]\n"
            #ifdef FEATURE_EXTERNAL_MODEM
            "[dump] [dp] [index]\n"
            #endif
            "\n"
            "==== eg: ====\n"
            "get cp\n"
            "dump cp 0\n");

    return 0;
}

static int get_load_info(char *path, modem_load_info *load_info) {
    int ret;

    modem_lock_read(path);
    ret = modem_get_load_info(path, load_info);
    modem_unlock_read(path);

    return ret;
}

static int init_load_info(void)
{
    int ret;

    ret = get_load_info(CP_DEV_PATH ,&g_cp_load_info);
    ret += get_load_info(SP_DEV_PATH ,&g_sp_load_info);
#ifdef FEATURE_EXTERNAL_MODEM
    ret += get_load_info(DP_DEV_PATH ,&g_dp_load_info);
#endif

    return ret;
}

static int modem_dbg_print_info(modem_load_info *load_info, int system) {
    char *sys;
    uint32_t i;

    if (system == MODEM_CP)
        sys = "cp";
    else if (system == MODEM_SP)
        sys = "sp";
    else
        sys = "dp";

    fprintf(stdout, "The %s system info:\n", sys);

    for (i = 0; i < load_info->region_cnt; i++) {
        fprintf(stdout,
                "  index =%03d, address = 0x%08lx, size = 0x%08x, name = %s.\n",
                i,
                load_info->regions[i].address,
                load_info->regions[i].size,
                load_info->regions[i].name);
    }

    if (system == MODEM_CP) {
        fprintf(stdout,
                "  index =%03d, address = 0x%08lx, size = 0x%08x, name = all modem.\n",
                MODEM_READ_MODEM_MEM,
                load_info->modem_base,
                load_info->modem_size);
        fprintf(stdout,
                "  index =%03d, address = 0x%08lx, size = 0x%08x, name = all memory.\n",
                MODEM_READ_ALL_MEM,
                load_info->all_base,
                load_info->all_size);
    }

    return 0;
}

static int modem_dbg_open_files(int *in,int *out,
                                char *rd_path, char *wr_path) {
    int fdin, fdout, err;

    err = mkdir(MODEM_STORE_PATH, 0x0644);
    if (-1 == err && EEXIST != errno) {
      fprintf(stdout, "create %s fail!\n", MODEM_STORE_PATH);
      return ERROR_OPEN_DIR;
    }

    fdin = open(rd_path, O_RDONLY, 0);
    if (fdin < 0) {
       fprintf(stdout, "open: %s fail!\n", rd_path);
       return ERROR_OPEN_READ_FILE;
    }

    fdout = open(wr_path, O_WRONLY|O_CREAT, 0);
    if (fdout < 0) {
       close(fdin);
       fprintf(stdout, "open: %s fail!\n", wr_path);
       return ERROR_OPEN_WRITE_FILE;
    }

    *in = fdin;
    *out = fdout;

    return 0;
}

static char *modem_dbg_get_read_path(int system) {
    if (system == MODEM_CP)
        return CP_DEV_PATH;

    if (system == MODEM_SP)
        return SP_DEV_PATH;

   /* system ony can be MODEM_DP here */
   return DP_DEV_PATH;
}

static int modem_dbg_dump_region(modem_load_info *load_info,
                                 uint32_t index, uint32_t system) {
    uint32_t size, rsize;
    int wsize, rrsize, ret = 0;
    char *name, *buf, *rd_path, *buf_heap;
    int fdin, fdout;
    char wr_path[MAX_PATH_LEN + 1];
    char buf_stack[64 * 1024];

    fprintf(stdout, "modem_dbg_dump_region, index = %d.\n", index);
    if (index >= load_info->region_cnt
        && index != MODEM_READ_ALL_MEM
        && index != MODEM_READ_MODEM_MEM)
        return ERROR_INVALID_INDEX;

    if (index == MODEM_READ_ALL_MEM) {
        size = load_info->all_size;
        name = "all";
    } else if (index == MODEM_READ_MODEM_MEM) {
        size = load_info->modem_size;
        name = "all_modem";
    } else {
        size = load_info->regions[index].size;
        name = load_info->regions[index].name;
    }
    fprintf(stdout, "dump %s: size = 0x%x.\n", name, size);

    rsize = min(size, MAX_ONECE_READ_SIZE);
    buf_heap = malloc(rsize);
    if (buf_heap) {
        buf = buf_heap;
    } else {
        buf = buf_stack;
        rsize = sizeof(buf_stack);
    }

    rd_path = modem_dbg_get_read_path(system);
    snprintf(wr_path, sizeof(wr_path), "%s%s.mem", MODEM_STORE_PATH, name);
    ret = modem_dbg_open_files(&fdin, &fdout, rd_path, wr_path);
    if (ret) {
        if (buf_heap)
            free(buf_heap);

        return ret;
    }

    modem_lock_write(rd_path);
    modem_set_read_region(rd_path, index);
    modem_unlock_write(rd_path);

    modem_lock_read(rd_path);
    do {
        rrsize = read(fdin, buf, rsize);
        if (rrsize == 0) {
            fprintf(stdout,"no more data.");
            break;
        }

        if (rrsize < 0) {
            fprintf(stdout,"read err: [ret=%d]\n", rrsize);
            ret = ERROR_READ_FILE;
            break;
        }

        if (rrsize > 0) {
          wsize = write(fdout, buf, rrsize);
          if (wsize != rrsize) {
            fprintf(stdout,"write err: [wsize=%d, rrsize=0x%x, remain=0x%x]\n",
                     wsize, rrsize, size);
            ret = ERROR_WRITE_FILE;
            break;
          }
        }

        size -= rrsize;
    } while (size > 0);
    modem_unlock_read(rd_path);

    if (0 == ret)
        fprintf(stdout, "dump succ %s.\n", wr_path);

    if (buf_heap)
        free(buf_heap);
    close(fdin);
    close(fdout);

    return ret;
}

static int modem_dbg_proc_get(modem_cmd *cmd) {
    if (cmd->system == MODEM_CP)
        return modem_dbg_print_info(&g_cp_load_info, MODEM_CP);

    if (cmd->system == MODEM_SP)
        return modem_dbg_print_info(&g_sp_load_info, MODEM_SP);

#ifdef FEATURE_EXTERNAL_MODEM
    if (cmd->system == MODEM_DP)
        return modem_dbg_print_info(&g_dp_load_info, MODEM_DP);
#endif

    return ERROR_UNDEFINE_SYSTEM;
}

static int modem_dbg_proc_dump(modem_cmd *cmd) {
    if (cmd->system == MODEM_CP)
        return modem_dbg_dump_region(&g_cp_load_info, cmd->index, MODEM_CP);

    if (cmd->system == MODEM_SP)
        return modem_dbg_dump_region(&g_sp_load_info, cmd->index, MODEM_SP);

#ifdef FEATURE_EXTERNAL_MODEM
    if (cmd->system == MODEM_DP)
        return modem_dbg_dump_region(&g_dp_load_info, cmd->index, MODEM_DP);
#endif

    return ERROR_UNDEFINE_SYSTEM;
}

static int modem_dbg_proc(modem_cmd *cmd) {
    int ret;

    if (cmd->action == ACTION_HELP)
        return usage();

    if (cmd->system >= MODEM_CNT
        || (cmd->action == ACTION_DUMP_MEMORY
            && cmd->index == INVALID_INDEX))
        return ERROR_INVALID_INDEX;

    ret = init_load_info();
    if (ret)
        return ERROR_GET_LOAD_INFO;

    if (cmd->action == ACTION_GET_INFO)
        return modem_dbg_proc_get(cmd);

    if (cmd->action == ACTION_DUMP_MEMORY)
        return modem_dbg_proc_dump(cmd);

    return ERROR_UNDEFINE_CMD;
}

int main(int argc, char **argv) {
    int index;
    modem_cmd cmd;

    /* default help */
    cmd.action = ACTION_HELP;
    cmd.system = MODEM_CNT;
    cmd.index  = INVALID_INDEX;

    if (argc > 1) {
        if (0 == strcmp(argv[1], DUMP_CMD))
            cmd.action = ACTION_DUMP_MEMORY;
        else if (0 == strcmp(argv[1], GET_CMD))
            cmd.action = ACTION_GET_INFO;
    }

    if (argc > 2) {
        if (0 == strcmp(argv[2], "cp"))
            cmd.system = MODEM_CP;
        else if (0 == strcmp(argv[2], "sp"))
            cmd.system = MODEM_SP;
        else if (0 == strcmp(argv[2], "dp"))
            cmd.system = MODEM_DP;
    }

    if ((argc > 3)) {
        errno = 0;
        index = strtol(argv[3], (char **) NULL, 10);
        if (!errno)
            cmd.index = index;
    }

    return modem_dbg_proc(&cmd);
}

