/**
 * modem_load.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */

#include <fcntl.h>
#include <errno.h>
#include <cutils/properties.h>

#include "modem_load.h"
#include "nv_read.h"
#include "modem_control.h"
#include "xml_parse.h"
#include "modem_head_parse.h"
#include "modem_io_control.h"

#ifdef FEATURE_PCIE_RESCAN
#include "modem_pcie_control.h"
#endif

#if defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
#include "secure_boot_load.h"
#endif

#define PERSIST_MODEM_PROP "persist.vendor.modem.nvp"
#define PERSIST_MODEM_PATH "ro.vendor.product.partitionpath"
#define PMCP_CALI_PATH "/vendor/firmware/EXEC_CALIBRATE_MAG_IMAGE"
#define EXTERN_MDMCTRL_PATH "/dev/mdm_ctrl"

#define FIXNV_BANK  "fixnv"
#define RUNNV_BANK_RD "runtimenv"
#define RUNNV_BANK_WT "runnv"

#define MODEM_MAGIC "SCI1"
#define MODEM_HDR_SIZE 12  // size of a block
#define SCI_TYPE_MODEM_BIN 1
#define SCI_TYPE_PARSING_LIB 2
#define SCI_LAST_HDR 0x100
#define MODEM_SHA1_HDR 0x400
#define MODEM_SHA1_SIZE 20

typedef struct __attribute__((packed)){
   uint32_t type_flags;
   uint32_t offset;
   uint32_t length;
} data_block_header_t;

static LOAD_VALUE_S cp_load_info;
static LOAD_VALUE_S sp_load_info;
#ifdef FEATURE_EXTERNAL_MODEM
static LOAD_VALUE_S dp_load_info;
#endif

static LOAD_NODE_INFO *modem_node_info = NULL;
static uint modem_node_num = 0;

static int write_proc_file(char *file, int offset, char *string) {
  int fd, stringsize, res = -1, retry = 0;

  MODEM_LOGD("%s: file is %s, string is %s!\n", __FUNCTION__, file, string);

  do {
    fd = open(file, O_RDWR);
    if (fd < 0) {
      usleep(200000);
      retry++;
    }
  } while (fd < 0 && retry < 6);

  if (fd < 0) {
    MODEM_LOGE("fd =%d: open file %s, error: %s", fd, file, strerror(errno));
    return res;
  }
  if (lseek(fd, offset, SEEK_SET) != offset) {
    MODEM_LOGE("Cant lseek file %s, error :%s", file, strerror(errno));
    goto leave;
  }

  stringsize = strlen(string);
  if (write(fd, string, stringsize) != stringsize) {
    MODEM_LOGE("Could not write %s in %s, error :%s", string, file,
               strerror(errno));
    goto leave;
  }

  res = 0;
leave:
  close(fd);

  return res;
}

static int get_load_node_info(char *file, int size, struct load_node_info *info)
{
  int fd, ret, i, num;

  fd = open(file, O_RDONLY);
  if(fd < 0) {
    if (errno == ENOENT)
      MODEM_LOGD("%s: unsupport load node %s ", __func__, file);
    else
      MODEM_LOGE("%s: open %s failed, error: %s", __func__,
                 file, strerror(errno));
    return -1;
  }

  ret = read(fd, info, size);
  close(fd);

  if(ret < 0) {
    MODEM_LOGE("%s: read %s failed, error: %s", __func__,
               file, strerror(errno));
    return -1;
  }

  num = ret/sizeof(LOAD_NODE_INFO);
  for(i=0; i < num; i++, info++) {
    MODEM_LOGD("%s: <%s, 0x%x, 0x%x>", __func__,
               info->name, info->base, info->size);
  }

  return num;
}

static void modem_init_load_node_info(void) {
  int n, num, size;

  MODEM_LOGD("%s\n", __FUNCTION__);

  num = 0;
  size = sizeof(LOAD_NODE_INFO) * MAX_LOAD_NODE_NUM;
  modem_node_info = (LOAD_NODE_INFO*)malloc(size);
  if (!modem_node_info) {
    MODEM_LOGE("%s: malloc node failed!\n", __FUNCTION__);
    return;
  }

  memset(modem_node_info, 0, size);
  modem_node_num = 0;

  n = get_load_node_info(SP_SYS_NODE, size, modem_node_info);
  if (n > 0)
    num += n;

  n = get_load_node_info(MODEM_SYS_NODE, size - n * sizeof(LOAD_NODE_INFO),
      modem_node_info + n);
   if (n > 0)
    num += n;

  if (num == 0) {
    free(modem_node_info);
    modem_node_info = NULL;
  } else {
    modem_node_num = num;
  }
}

static void modem_correct_image(LOAD_VALUE_S *load_info){
  IMAGE_LOAD_S *table;
  LOAD_NODE_INFO *node;
  uint i, j, find;

  table = load_info->load_table;
  for (i = 0; i < load_info->table_num; i++, table++)
  {
    MODEM_LOGD("%s: table[%d]: w_path=%s, addr=0x%lx, size=0x%x\n",
           __func__, i, table->path_w, table->addr, table->size);

    find = 0;
    node = modem_node_info;
    for(j = 0; j < modem_node_num; j++, node++)
    {
      if (strstr(table->path_w, node->name))
      {
        find = 1;
        break;
      }
    }

    if (find) {
      MODEM_LOGD("%s: %s addr 0x%lx ==> 0x%x, size 0x%x ==> 0x%x\n",
                 __func__, node->name,
                 table->addr, node->base,
                 table->size, node->size);
      table->addr = node->base;
      table->size = node->size;
      continue;
    }
  }
}

static void modem_correct_modem_info(void) {
  MODEM_LOGD("%s:modem_node_num = %d\n", __FUNCTION__, modem_node_num);

  if (modem_node_num == 0)
    return;

  modem_correct_image(&cp_load_info);
  modem_correct_image(&sp_load_info);

  free(modem_node_info);
  modem_node_info = NULL;
}

static int modem_load_cp_cmdline(char *fin, char *fout)
{
  int fdin, fdout, wsize;
  char *str;
  char cmdline[CPCMDLINE_SIZE] = {0};

  MODEM_LOGD("%s (%s ==> %s)\n",__func__,
      fin, fout);
  modem_ctrl_enable_busmonitor(false);
  modem_ctrl_enable_dmc_mpu(false);

  fdin = open(fin, O_RDONLY);
  if (fdin < 0) {
    MODEM_LOGE("failed to open %s, error: %s", fin, strerror(errno));
    modem_ctrl_enable_busmonitor(true);
    modem_ctrl_enable_dmc_mpu(true);
    return -1;
  }

  str = cmdline;
  if (read(fdin, cmdline, sizeof(cmdline) - 1)> 0) {
    str = strstr(cmdline, "modem=");
    if (str != NULL){
      str = strchr(str,' ');
      str += 1;
    } else {
      MODEM_LOGD("cmdline 'modem=' is not exist\n");
      str = cmdline;
    }
  }

  /* add sim lock pubkey in cp cmdline in noraml mode*/
#if defined(SIMLOCK_AP_READ_EFUSE)
  if (NULL == strstr(cmdline, "calibration=")) {
    add_spuk_cmdline(str);
  }
#endif

  fdout = open(fout, O_WRONLY);
  if (fdout < 0) {
    close(fdin);
    MODEM_LOGE("failed to open %s, error: %s", fout, strerror(errno));
    modem_ctrl_enable_busmonitor(true);
    modem_ctrl_enable_dmc_mpu(true);
    return -1;
  }

  /* some cp cmdline only 0x400, we just write all cp cmdline,
     and end with 0, so size is  strlen(str)*sizeof(char) + 1 */
  wsize = write(fdout, str, (strlen(str) + 1)*sizeof(char));
  MODEM_LOGD("write cmdline [wsize = %d]", wsize);

  if (wsize <= 0) {
    MODEM_LOGE("failed to write %s [wsize = %d]",
        fout, wsize);
  }

  modem_ctrl_enable_busmonitor(true);
  modem_ctrl_enable_dmc_mpu(true);

  close(fdin);
  close(fdout);
  return wsize;
}

static int modem_load_cp_boot_code(char *fout)
{
  int fdout, wsize;
  char buf[512] = {0};

  MODEM_LOGD("%s (boot code ==> %s)\n",__func__, fout);

  wsize = modem_head_get_boot_code(buf, sizeof(buf));
  fdout = open(fout, O_WRONLY);
  if (fdout < 0) {
    MODEM_LOGE("failed to open %s, error: %s", fout, strerror(errno));
    return -1;
  }

  wsize = write(fdout, buf, wsize);
  MODEM_LOGD("write boot code [wsize = %d]", wsize);
  if (wsize <= 0) {
    MODEM_LOGE("failed to write %s [wsize = %d]",
        fout, wsize);
  }

  close(fdout);
  return wsize;
}


static void modem_load_cp_nv(char* read, char* write) {
  char path[MAX_PATH_LEN + 1];
  char bak[MAX_PATH_LEN + 1];

  mstrncpy2(path, read, "1"); // xxnv1
  mstrncpy2(bak, read, "2");  //xxnv2
  MODEM_LOGD("%s: path=%s, bak_path=%s, out=%s\n",
             __func__, path, bak, write);
  read_nv_partition(path, bak, write);
}

static int load_img_from_table(LOAD_VALUE_S *load,
                               uint32_t load_flag,
                               uint32_t skip_flag) {
  IMAGE_LOAD_S *tmp_table;
  uint i, max;
  unsigned int load_offset = 0;
  size_t load_size = 0;
  char *ioctrl;
  int ret = 0;

  tmp_table = load->load_table;
  max = load->table_num;
  ioctrl = load->io_ctrl;
  MODEM_LOGIF("load img: load_flag = 0x%x, skip_flag = 0x%x!\n",
              load_flag, skip_flag);

  for (i = 0; i < max; i++, tmp_table++) {
    /* skip invalid tabel */
    if (tmp_table->size == 0)
      continue;

    /* first,  skip */
    if (tmp_table->flag & skip_flag)
      continue;

    /* than,  load */
    if ((tmp_table->flag & load_flag)) {
      if (load->ioctrl_is_ok)
         modem_set_write_region(ioctrl, i);

      if (GET_FLAG(tmp_table->flag, CMDLINE_FLAG)) {
         MODEM_LOGD("%s: load cpcmdline\n", __func__);
         modem_load_cp_cmdline("/proc/cmdline", tmp_table->path_w);
         continue;
      }

      if (GET_FLAG(tmp_table->flag, NV_FLAG)) {
        MODEM_LOGD("%s: load nv\n", __func__);
        modem_load_cp_nv(tmp_table->path_r,tmp_table->path_w);
        continue;
      }

      if (GET_FLAG(tmp_table->flag, BOOT_CODE)) {
         MODEM_LOGD("%s: load boot code\n", __func__);
         modem_load_cp_boot_code(tmp_table->path_w);
         continue;
      }

#if (defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) \
            || defined(CONFIG_VBOOT_V2))
      if (GET_FLAG(tmp_table->flag, SECURE_FLAG)) {
        ret += secure_boot_load_img(tmp_table);
        continue;
      }
#endif
      modem_get_patiton_info(tmp_table, &load_offset, &load_size);
      ret += modem_load_image(tmp_table, load_offset, 0, load_size);
    }
  }

  return ret;
}

static void modem_init_load_info(LOAD_VALUE_S *load_info, int num) {
  IMAGE_LOAD_S *table_ptr;

  memset(load_info, 0, sizeof(LOAD_VALUE_S));
  table_ptr = malloc(sizeof(IMAGE_LOAD_S) * num);
  if (!table_ptr)
    MODEM_LOGE("%s: malloc load info failed!\n", __FUNCTION__);

  memset(table_ptr, 0, sizeof(IMAGE_LOAD_S) * num);
  load_info->table_num = num;
  load_info->load_table = table_ptr;
}

void modem_init_sp_load_info(void) {
  IMAGE_LOAD_S *table_ptr;
  char path_read[MAX_PATH_LEN] = {0};

  /* get read path */
  property_get(PERSIST_MODEM_PATH, path_read, "not_find");
  MODEM_LOGD("%s: path_read is %s\n", __FUNCTION__, path_read);

  modem_init_load_info(&sp_load_info, IMAGE_LOAD_SP_NUM);
  sp_load_info.img_type = IMAGE_SP;
  snprintf(sp_load_info.name, sizeof(sp_load_info.name), "%s", "sp");
  table_ptr = sp_load_info.load_table;
  if (!table_ptr)return;

  if (access("/proc/pmic", F_OK) == 0) {
    sp_load_info.drv_is_ok = 1;
    strncpy(table_ptr[IMAGE_LOAD_SP].name, "pm_sys", MAX_FILE_NAME_LEN);
    strncpy(table_ptr[IMAGE_LOAD_SP].path_w, "/proc/pmic/pm_sys", MAX_PATH_LEN);
    mstrncpy2(table_ptr[IMAGE_LOAD_SP].path_r, path_read, "pm_sys");
    strncpy(table_ptr[IMAGE_LOAD_SP].name, "cali_lib", MAX_FILE_NAME_LEN);
    strncpy(table_ptr[IMAGE_LOAD_SP_CALI].path_w,
            "/proc/pmic/cali_lib", MAX_PATH_LEN);
    strncpy(table_ptr[IMAGE_LOAD_SP_CALI].path_r, PMCP_CALI_PATH, MAX_PATH_LEN);
    table_ptr[IMAGE_LOAD_SP].size = PMCP_SIZE;
    table_ptr[IMAGE_LOAD_SP_CALI].size = PMCP_CALI_SIZE;
    SET_2FLAG(table_ptr[IMAGE_LOAD_SP_CALI].flag, CLR_FLAG, SP_CALI_FLAG);

    strncpy(sp_load_info.start, "/proc/pmic/start", MAX_PATH_LEN);
    strncpy(sp_load_info.stop, "/proc/pmic/stop", MAX_PATH_LEN);
    SET_2FLAG(table_ptr[IMAGE_LOAD_SP].flag, SECURE_FLAG, SP_FLAG);
  }
}

static void modem_init_cp_load_info(void) {
  int modem;
  char path_read[MAX_PATH_LEN] = {0};
  char path_write[MAX_PATH_LEN] = {0};
  char cproc_prop[PROPERTY_VALUE_MAX] = {0};
  char prop_nvp[PROPERTY_VALUE_MAX] = {0};
  char nvp_name[PROPERTY_VALUE_MAX] = {0};
  IMAGE_LOAD_S *table_ptr;

  MODEM_LOGD("%s\n", __FUNCTION__);

  modem_init_load_info(&cp_load_info, IMAGE_LOAD_CP_NUM);
  cp_load_info.img_type = IMAGE_CP;
  snprintf(cp_load_info.name, sizeof(cp_load_info.name), "%s", "cp");
  table_ptr = cp_load_info.load_table;
  if (!table_ptr)return;

  if (access("/proc/cptl", F_OK) == 0) {
    cp_load_info.drv_is_ok = 1;
    modem = modem_ctrl_get_modem_type();
    MODEM_LOGD("%s: modem type = 0x%x", __FUNCTION__, modem);

    /* get nvp prop */
    snprintf(nvp_name, sizeof(nvp_name), PERSIST_MODEM_PROP);
    property_get(nvp_name, prop_nvp, "not_find");
    MODEM_LOGD("%s: nvp_name is %s, prop_nvp is %s\n", __FUNCTION__, nvp_name,
               prop_nvp);

    /* get read path */
    property_get(PERSIST_MODEM_PATH, path_read, "not_find");
    MODEM_LOGD("%s: path_read is %s\n", __FUNCTION__, path_read);

    /*get write path */
    snprintf(cproc_prop, sizeof(cproc_prop), PROC_DEV_PROP);
    property_get(cproc_prop, path_write, "not_find");
    MODEM_LOGD("%s: cproc_prop is %s, path_write is %s\n", __FUNCTION__,
               cproc_prop, path_write);

    /* init cmd line info */
    strncpy(table_ptr[IMAGE_LOAD_CMDLINE].path_r,
            "/proc/cmdline", MAX_PATH_LEN);
    table_ptr[IMAGE_LOAD_CMDLINE].size = CPCMDLINE_SIZE;
    strncpy(table_ptr[IMAGE_LOAD_CMDLINE].name,
            CMDLINE_BANK, MAX_FILE_NAME_LEN);
    mstrncpy2(table_ptr[IMAGE_LOAD_CMDLINE].path_w, path_write, CMDLINE_BANK);
    SET_2FLAG(table_ptr[IMAGE_LOAD_CMDLINE].flag,
              CMDLINE_FLAG, MODEM_OTHER_FLAG);

    /* init modem info */
    switch (modem) {
      case TD_MODEM:
      case W_MODEM: {
        /* init dsp */
        table_ptr[IMAGE_LOAD_MODEM].size = MODEM_SIZE;
        table_ptr[IMAGE_LOAD_DSP].size = TGDSP_SIZE;
        strncpy(table_ptr[IMAGE_LOAD_DSP].name, DSP_BANK, MAX_FILE_NAME_LEN);
        mstrncpy2(table_ptr[IMAGE_LOAD_DSP].path_w, path_write, DSP_BANK);
        mstrncpy3(table_ptr[IMAGE_LOAD_DSP].path_r,
                  path_read, prop_nvp, DSP_BANK);
      } break;

      case LTE_MODEM: {
        table_ptr[IMAGE_LOAD_MODEM].size = MODEM_SIZE;
        table_ptr[IMAGE_LOAD_DELTANV].size = DELTANV_SIZE;
        table_ptr[IMAGE_LOAD_GDSP].size = TGDSP_SIZE;
        table_ptr[IMAGE_LOAD_LDSP].size = LDSP_SIZE;
        table_ptr[IMAGE_LOAD_CDSP].size = CDSP_SIZE;
        table_ptr[IMAGE_LOAD_WARM].size = WARM_SIZE;
        /* init gdsp */
        strncpy(table_ptr[IMAGE_LOAD_GDSP].name, TGDSP_BANK, MAX_FILE_NAME_LEN);
        mstrncpy2(table_ptr[IMAGE_LOAD_GDSP].path_w, path_write, TGDSP_BANK);
        if (0 != access(table_ptr[IMAGE_LOAD_GDSP].path_w, F_OK)) {
          strncpy(table_ptr[IMAGE_LOAD_GDSP].name,
                  GDSP_BANK, MAX_FILE_NAME_LEN);
          mstrncpy2(table_ptr[IMAGE_LOAD_GDSP].path_w, path_write, GDSP_BANK);
        }
        mstrncpy3(table_ptr[IMAGE_LOAD_GDSP].path_r,
                  path_read, prop_nvp, TGDSP_BANK);
        if (0 != access(table_ptr[IMAGE_LOAD_GDSP].path_r, F_OK))
          mstrncpy3(table_ptr[IMAGE_LOAD_GDSP].path_r,    
                    path_read, prop_nvp, GDSP_BANK);
        /* init ldsp */
        strncpy(table_ptr[IMAGE_LOAD_LDSP].name, LDSP_BANK, MAX_FILE_NAME_LEN);
        mstrncpy2(table_ptr[IMAGE_LOAD_LDSP].path_w, path_write, LDSP_BANK);
        mstrncpy3(table_ptr[IMAGE_LOAD_LDSP].path_r,
                  path_read, prop_nvp, LDSP_BANK);
        /* init cdsp */
        strncpy(table_ptr[IMAGE_LOAD_CDSP].name, CDSP_BANK, MAX_FILE_NAME_LEN);
        mstrncpy2(table_ptr[IMAGE_LOAD_CDSP].path_w, path_write, CDSP_BANK);
        mstrncpy3(table_ptr[IMAGE_LOAD_CDSP].path_r,
                  path_read, prop_nvp, CDSP_BANK);
        /* init warm */
        strncpy(table_ptr[IMAGE_LOAD_WARM].name, WARM_BANK, MAX_FILE_NAME_LEN);
        mstrncpy2(table_ptr[IMAGE_LOAD_WARM].path_w, path_write, WARM_BANK);
        mstrncpy3(table_ptr[IMAGE_LOAD_WARM].path_r,
                  path_read, prop_nvp, WARM_BANK);
      } break;

    case NR_MODEM:
        /* do nothing */
        break;

    default:
        break;
    }

    /* init modem */
    strncpy(table_ptr[IMAGE_LOAD_MODEM].name, MODEM_BANK, MAX_FILE_NAME_LEN);
    strncpy(table_ptr[IMAGE_LOAD_DELTANV].name,
            DELTANV_BANK, MAX_FILE_NAME_LEN);
    mstrncpy2(table_ptr[IMAGE_LOAD_MODEM].path_w, path_write, MODEM_BANK);
    mstrncpy2(table_ptr[IMAGE_LOAD_DELTANV].path_w, path_write, DELTANV_BANK);
    mstrncpy3(table_ptr[IMAGE_LOAD_MODEM].path_r,path_read, prop_nvp, MODEM_BANK);
    mstrncpy3(table_ptr[IMAGE_LOAD_DELTANV].path_r,
              path_read, prop_nvp, DELTANV_BANK);

  /* init secure boot info */
    SET_2FLAG(table_ptr[IMAGE_LOAD_MODEM].flag, SECURE_FLAG, MODEM_FLAG);
    SET_2FLAG(table_ptr[IMAGE_LOAD_DELTANV].flag, SECURE_FLAG, MODEM_FLAG);
    SET_2FLAG(table_ptr[IMAGE_LOAD_DSP].flag, SECURE_FLAG, MODEM_DSP_FLAG);
    SET_2FLAG(table_ptr[IMAGE_LOAD_GDSP].flag, SECURE_FLAG, MODEM_DSP_FLAG);
    SET_2FLAG(table_ptr[IMAGE_LOAD_LDSP].flag, SECURE_FLAG, MODEM_DSP_FLAG);
    SET_2FLAG(table_ptr[IMAGE_LOAD_CDSP].flag, SECURE_FLAG, MODEM_DSP_FLAG);
    SET_2FLAG(table_ptr[IMAGE_LOAD_WARM].flag, SECURE_FLAG, MODEM_DSP_FLAG);

    /* init fixnv image*/
    strncpy(table_ptr[IMAGE_LOAD_FIXNV].name, FIXNV_BANK, MAX_FILE_NAME_LEN);
    mstrncpy3(table_ptr[IMAGE_LOAD_FIXNV].path_r,
              path_read, prop_nvp, FIXNV_BANK);
    table_ptr[IMAGE_LOAD_FIXNV].size = FIXNV_SIZE;
    mstrncpy2(table_ptr[IMAGE_LOAD_FIXNV].path_w, path_write, FIXNV_BANK);
    SET_2FLAG(table_ptr[IMAGE_LOAD_FIXNV].flag, NV_FLAG, MODEM_OTHER_FLAG);

    /* init runnv image*/
    strncpy(table_ptr[IMAGE_LOAD_RUNNV].name, RUNNV_BANK_WT, MAX_FILE_NAME_LEN);
    mstrncpy3(table_ptr[IMAGE_LOAD_RUNNV].path_r,
              path_read, prop_nvp, RUNNV_BANK_RD);
    table_ptr[IMAGE_LOAD_RUNNV].size = RUNNV_SIZE;
    mstrncpy2(table_ptr[IMAGE_LOAD_RUNNV].path_w, path_write, RUNNV_BANK_WT);
    SET_2FLAG(table_ptr[IMAGE_LOAD_RUNNV].flag, NV_FLAG, MODEM_OTHER_FLAG);

    /* init start/stop path */
    strncpy(cp_load_info.start, "/proc/cptl/start", MAX_PATH_LEN);
    strncpy(cp_load_info.stop, "/proc/cptl/stop", MAX_PATH_LEN);
  }
}

static void modem_default_init_load_info(void) {
  modem_init_cp_load_info();
  modem_init_sp_load_info();
#ifdef FEATURE_EXTERNAL_MODEM
  modem_init_load_info(&dp_load_info, 1);
  dp_load_info.img_type = IMAGE_DP;
#endif
  /* correct load addr by get ldinfo(old cptl driver) */
  modem_init_load_node_info();
  modem_correct_modem_info();
}

static void modem_load_run(uint b_run, LOAD_VALUE_S *load) {
  char *path = b_run ? load->start : load->stop;

  MODEM_LOGD("%s: run = %d!\n", __FUNCTION__, b_run);

  if (load->ioctrl_is_ok) {
    if (b_run) {
      modem_ioctrl_start(load->io_ctrl);
      modem_unlock_write(load->io_ctrl);
    } else {
      modem_lock_write(load->io_ctrl);
      modem_ioctrl_stop(load->io_ctrl);
    }
  } else if (load->drv_is_ok) {
    MODEM_LOGD("%s: path = %s\n", __FUNCTION__, path);
    write_proc_file(path, 0, "1");
  }
}

static void modem_load_stop(int load_type) {
  if (load_type & LOAD_SP_IMG)
    modem_load_run(0, &sp_load_info);

#ifdef FEATURE_EXTERNAL_MODEM
  if (load_type & LOAD_DP_IMG)
    modem_load_run(0, &dp_load_info);
#endif

  if (load_type & (LOAD_MODEM_IMG | LOAD_MINIAP_IMG))
    modem_load_run(0, &cp_load_info);
}

static void modem_load_start(int load_type) {
  if (load_type & LOAD_SP_IMG)
    modem_load_run(1, &sp_load_info);

#ifdef FEATURE_EXTERNAL_MODEM
  if (load_type & LOAD_DP_IMG)
    modem_load_run(1, &dp_load_info);
#endif

  if (load_type & (LOAD_MODEM_IMG | LOAD_MINIAP_IMG))
    modem_load_run(1, &cp_load_info);
}

static int modem_convert_loadinfo(
    LOAD_VALUE_S *value, modem_load_info *info) {
  modem_region_info *region = info->regions;
  IMAGE_LOAD_S *table = value->load_table;
  uint i;
  uint num = value->table_num;

  if (num > MAX_REGION_CNT || num == 0) {
    MODEM_LOGD("%s table_num = %d is error!", value->name, num);
    return MODEM_ERR;
  }

  info->region_cnt = value->table_num;
  info->modem_base = value->modem_base;
  info->modem_size = value->modem_size;
  info->all_base = value->all_base;
  info->all_size = value->all_size;

  for (i = 0; i < num; i++) {
    region[i].address = table[i].addr;
    region[i].size = table[i].size;
    strncpy(region[i].name, table[i].name, MAX_REGION_NAME_LEN);
    MODEM_LOGD("convet: %02d. name=%s, addr=0x%lx, size=0x%x, flag=0x%x!\n",
      i, region[i].name, region[i].address, region[i].size, table[i].flag);
  }

  return MODEM_SUCC;
}

static void modem_load_do_set_load_info(LOAD_VALUE_S *load) {
  modem_load_info info = {0};
  char *ioctl;

  if (load->ioctrl_is_ok) {
    if (MODEM_SUCC == modem_convert_loadinfo(load, &info)) {
      ioctl = load->io_ctrl;
      modem_lock_write(ioctl);
      modem_set_load_info(ioctl, &info);
      modem_unlock_write(ioctl);
    }
  }
}

static void modem_load_set_load_info(void) {
  modem_load_do_set_load_info(&sp_load_info);

#ifndef FEATURE_REMOVE_SPRD_MODEM
  modem_load_do_set_load_info(&cp_load_info);
#ifdef FEATURE_EXTERNAL_MODEM
  modem_load_do_set_load_info(&dp_load_info);
#endif
#endif
}

void modem_load_assert_modem(void) {
  MODEM_LOGD("%s: \n", __FUNCTION__);

  if (cp_load_info.ioctrl_is_ok)
    modem_ioctrl_assert(cp_load_info.io_ctrl);
}

int init_modem_img_info(void) {
#if (defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) \
              || defined(CONFIG_VBOOT_V2))
    secure_boot_init();
#endif

  /* first use default value */
  modem_default_init_load_info();

  /* than try get from xml */
  modem_xml_init_load_info();

  /* than try get from modem head */
  modem_head_correct_load_info(&cp_load_info);

  /* if io control, set loadinfo to kernel driver*/
  modem_load_set_load_info();

  return 0;
}

#ifdef FEATURE_EXTERNAL_MODEM
static int modem_load_wait_remote(LOAD_VALUE_S *load_info, int wait_bit) {
  uint32_t flag, cnt = 100;
  char *io_ctrl;

  MODEM_LOGIF("%s: wait_bit=0x%x ...\n", __func__, wait_bit);

  if (!load_info->ioctrl_is_ok)
    return 1;

  io_ctrl = load_info->io_ctrl;
  while (1) {
    modem_lock_read(io_ctrl);
    flag = modem_get_remote_flag(io_ctrl);
    modem_unlock_read(io_ctrl);

    if (flag & wait_bit) {
      MODEM_LOGIF("%s: succ, wait_bit=0x%x\n", __func__, wait_bit);
      return 1; /* 1 succ */
    }

    if (cnt == 0)
      break;

    cnt--;
    usleep(100*1000);
  }

  MODEM_LOGE("%s: Timeout: wait_bit = 0x%x", __func__, wait_bit);

  return 0;
}

int load_spl_img(void)
{
  MODEM_LOGD("%s!\n", __FUNCTION__);

  /* clear remote flag, wait ddr ready */
  if (cp_load_info.ioctrl_is_ok) {
    modem_lock_write(cp_load_info.io_ctrl);
    modem_set_remote_flag(cp_load_info.io_ctrl, REMOTE_CLEAR_FLAG);

    /* load spl img */
    if (0 != load_img_from_table(&cp_load_info, SPL_IMG_FLAG, NONE_IMAG_FLAG)) {
       MODEM_LOGE("can't load spl, stop load!");
       modem_lock_write(cp_load_info.io_ctrl);
       return -1;
    }

    modem_set_remote_flag(cp_load_info.io_ctrl, SPL_IMAGE_DONE_FLAG|MODEM_WARM_RESET_FLAG);
    modem_unlock_write(cp_load_info.io_ctrl);
    return 0;
  }

  return -2;
}

int load_modem_img(int load_type) {
  int start_img;

#ifdef FEATURE_REMOVE_SPRD_MODEM
  load_type = LOAD_SP_IMG;
#endif

  start_img = load_type;

  /* get wake_lock */
  modem_ctrl_enable_wake_lock(1, __FUNCTION__);

  MODEM_LOGD("%s: load_type = 0x%x!\n", __FUNCTION__, load_type);

  /* set modem state */
  modem_ctrl_set_modem_state(MODEM_STATE_LOADING);

  /* load sp img */
  if (load_type & LOAD_SP_IMG) {
#if defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
    /* stop action will do in TA */
    secure_boot_set_flag(LOAD_SP_IMG);
    secure_boot_unlock_ddr();
#endif

    modem_load_stop(LOAD_SP_IMG);
    if (sp_load_info.drv_is_ok ||
      sp_load_info.ioctrl_is_ok) {
      MODEM_LOGD("start load sp image!\n");
      load_img_from_table(&sp_load_info, SP_IMG_FLAG, NONE_IMAG_FLAG);
    } else {
      MODEM_LOGE("sp driver is not ok!");
    }
    modem_load_start(LOAD_SP_IMG);

#if (defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) \
    || defined(CONFIG_VBOOT_V2))
    secure_boot_verify_all();
#endif

    start_img &= (~LOAD_SP_IMG);
  }

  /* stop modem */
  modem_load_stop(start_img);
  /* load dp img */
  if (load_type & LOAD_DP_IMG) {
    if (dp_load_info.drv_is_ok ||
      dp_load_info.ioctrl_is_ok) {
      MODEM_LOGD("start load dp image!\n");
      load_img_from_table(&dp_load_info, SP_IMG_FLAG, NONE_IMAG_FLAG);
    } else {
      MODEM_LOGIF("dp driver is not ok!");
    }
  }

  /* clear remote flag, wait ddr ready */
  if (cp_load_info.ioctrl_is_ok && load_type) {
    modem_set_remote_flag(cp_load_info.io_ctrl, REMOTE_CLEAR_FLAG);

    if (load_type & LOAD_MINIAP_IMG) {
      /* load spl img */
      if (0 != load_img_from_table(&cp_load_info,
                                   SPL_IMG_FLAG, NONE_IMAG_FLAG)) {
         MODEM_LOGE("can't load spl, stop load!");
         start_img = load_type = 0;
      } else {
         modem_set_remote_flag(cp_load_info.io_ctrl, SPL_IMAGE_DONE_FLAG);
         /* wait ddr ready */
         if (!modem_load_wait_remote(&cp_load_info, REMOTE_DDR_READY_FLAG)) {
           MODEM_LOGE("can't get REMOTE_DDR_READY_FLAG!");
           /* if ep ddr not ready, can't load any image again */
           start_img = load_type = 0;
        }
      }
    }
  }

  /* load audio dsp */
  if (load_type & LOAD_AGDSP_IMG)
    load_img_from_table(&cp_load_info, AUDIO_IMG_FLAG, NONE_IMAG_FLAG);

  /* load mini ap img */
  if (load_type & LOAD_MINIAP_IMG
      && cp_load_info.ioctrl_is_ok) {
    MODEM_LOGD("start load miniap image!\n");

    /* load sml and uboot img */
    if (0 == load_img_from_table(&cp_load_info,
        SML_IMG_FLAG | UBOOT_IMG_FLAG, NONE_IMAG_FLAG))
      modem_set_remote_flag(cp_load_info.io_ctrl, UBOOT_IMAGE_DONE_FLAG);

    /* load boot image */
    if (0 == load_img_from_table(&cp_load_info, BOOT_IMG_FLAG, NONE_IMAG_FLAG))
      modem_set_remote_flag(cp_load_info.io_ctrl, BOOT_IMAGE_DONE_FLAG);

    /* first, clear ep set bar falg and load modem head,
     * after load modem head, wait ep reset bar flag,
     * if get ep reset bar, rescan ep device,
     * after rescan finshed, set EP_RESCAN_DONE_FLAG to ep
     */
#ifdef FEATURE_PCIE_RESCAN
    /* clear  EP_SET_BAR_DONE_FLAG */
    modem_clear_remote_flag(cp_load_info.io_ctrl, EP_SET_BAR_DONE_FLAG);
#endif

    /* load modem head */
    if (0 == load_img_from_table(&cp_load_info,
        MODEM_HEAD_IMG_FLAG, NONE_IMAG_FLAG)) {
      modem_set_remote_flag(cp_load_info.io_ctrl, MODEM_HEAD_DONE_FLAG);

#ifdef FEATURE_PCIE_RESCAN
      /* wait EP_SET_BAR_DONE_FLAG */
      if (modem_load_wait_remote(&cp_load_info, EP_SET_BAR_DONE_FLAG)) {
        MODEM_LOGD("get EP_SET_BAR_DONE_FLAG!");
        /* rescan ep */
        /*  if rescan ep failed, can't load any image again */
        if (modem_rescan_ep_device())
           start_img = load_type = 0;
        else
          /* set EP_RESCAN_DONE_FLAG, low active, so clear */
          modem_clear_remote_flag(cp_load_info.io_ctrl, EP_RESCAN_DONE_FLAG);
      }
#endif
    }
  }

  if (load_type & LOAD_MODEM_IMG) {
    /* load modem img */
    if (0 == load_img_from_table(&cp_load_info,
        MODEM_IMG_FLAG, MODEM_HEAD_IMG_FLAG)) {
      if (cp_load_info.ioctrl_is_ok)
       modem_set_remote_flag(cp_load_info.io_ctrl, MODEM_IMAGE_DONE_FLAG);
    }
  }

  /* start modem */
  modem_load_start(start_img);
  modem_ctrl_set_modem_state(MODEM_STATE_BOOTING);

  /* release wake_lock */
  modem_ctrl_enable_wake_lock(0, __FUNCTION__);

  return 0;
}
#else
int load_modem_img(int load_type) {
  /* get wake_lock */
  modem_ctrl_enable_wake_lock(1, __FUNCTION__);

#ifdef FEATURE_REMOVE_SPRD_MODEM
  load_type = LOAD_SP_IMG;
#endif

  MODEM_LOGD("%s: load_type = 0x%x!\n", __FUNCTION__, load_type);

#if defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
  /* stop action will do in TA */
  secure_boot_set_flag(load_type);
  secure_boot_unlock_ddr();
#endif
  modem_load_stop(load_type);
  modem_ctrl_set_modem_state(MODEM_STATE_LOADING);

  /* first, load sp img */
  if (load_type & LOAD_SP_IMG) {
    if (sp_load_info.drv_is_ok ||
      sp_load_info.ioctrl_is_ok) {
      MODEM_LOGD("start load sp image!\n");
      load_img_from_table(&sp_load_info, SP_IMG_FLAG, NONE_IMAG_FLAG);
    } else {
      MODEM_LOGE("sp driver is not ok!");
    }
  }

  /* than, load modem img */
  if (load_type & LOAD_MODEM_IMG) {
    if (cp_load_info.drv_is_ok ||
        cp_load_info.ioctrl_is_ok) {
      MODEM_LOGD("start load cp image!\n");
      load_img_from_table(&cp_load_info, MODEM_IMG_FLAG, NONE_IMAG_FLAG);
    } else {
      MODEM_LOGE("cp driver is not ok!");
    }
  }
#if (defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) \
            || defined(CONFIG_VBOOT_V2))
  secure_boot_verify_all();
#endif

  modem_load_start(load_type);
  modem_ctrl_set_modem_state(MODEM_STATE_BOOTING);

  /* release wake_lock */
  modem_ctrl_enable_wake_lock(0, __FUNCTION__);

  return 0;
}
#endif

void modem_clear_region(char *fout, uint size)
{
  int fd;
  int rval, wsize;
  char buf[8 * 1024];

  MODEM_LOGD("clear region %s!", fout);

  fd = open(fout, O_WRONLY);
  if (fd < 0) {
    MODEM_LOGE("failed to open %s, error: %s", fout, strerror(errno));
    return;
  }

  memset(buf, 0, sizeof(buf));
  do {
    wsize = min(size, sizeof(buf));
    rval = write(fd, buf, wsize);
    if (rval <= 0) {
      MODEM_LOGE("write zero to %s [wsize=%d, remain=%d] failed",
                 fout, wsize, size);
      goto leave;
    }
    size -= wsize;

  } while (size > 0);

leave:
  close(fd);
}


int modem_load_image(IMAGE_LOAD_S* img, int offsetin, int offsetout,
                    uint size) {
  int res = -1, fdin, fdout, rsize, rrsize, wsize;
  uint buf_size;
  char buf_stack[512 * 1024];
  char *buf_heap = NULL;
  char *fin = img->path_r;
  char *fout= img->path_w;
  char *buf;

  buf_size = sizeof(buf_stack);
  buf = buf_stack;

  if (size > buf_size) {
    buf_heap = malloc(size);
    if (buf_heap) {
      buf_size = size;
      buf = buf_heap;
    }
  }

  MODEM_LOGD("%s: (%s(0x%x) ==> %s(0x%x) size=0x%x)\n",
             __FUNCTION__, fin, offsetin,
             fout, offsetout, size);

  modem_ctrl_enable_busmonitor(false);
  modem_ctrl_enable_dmc_mpu(false);

  if (GET_FLAG(img->flag, CLR_FLAG)) {
    modem_clear_region(fout, size);
  }

  fdin = open(fin, O_RDONLY);
  if (fdin < 0) {
    MODEM_LOGE("failed to open %s, error: %s", fin, strerror(errno));
    modem_ctrl_enable_busmonitor(true);
    modem_ctrl_enable_dmc_mpu(true);
    return -1;
  }

  fdout = open(fout, O_WRONLY);
  if (fdout < 0) {
    close(fdin);
    MODEM_LOGE("failed to open %s, error: %s", fout, strerror(errno));
    modem_ctrl_enable_busmonitor(true);
    modem_ctrl_enable_dmc_mpu(true);
    return -1;
  }

  if (lseek(fdin, offsetin, SEEK_SET) != offsetin) {
    MODEM_LOGE("failed to lseek %d in %s", offsetin, fin);
    goto leave;
  }

  if (lseek(fdout, offsetout, SEEK_SET) != offsetout) {
    MODEM_LOGE("failed to lseek %d in %s", offsetout, fout);
    goto leave;
  }

  do {
    rsize = min(size, buf_size);
    rrsize = read(fdin, buf, rsize);
    if (rrsize == 0) goto leave;
    if (rrsize < 0) {
      MODEM_LOGE("failed to read %s %s", fin, strerror(errno));
      goto leave;
    }
    wsize = write(fdout, buf, rrsize);

    MODEM_LOGIF("write %s [wsize=%d, rsize=%d, remain=%d]", fout, wsize, rsize,
                size);

    if (wsize <= 0) {
      MODEM_LOGE("failed to write %s [wsize=%d, rsize=%d, remain=%d]", fout,
                 wsize, rsize, size);
      goto leave;
    }
    size -= rrsize;
  } while (size > 0);
  res = 0;

leave:
  modem_ctrl_enable_busmonitor(true);
  modem_ctrl_enable_dmc_mpu(true);
  if (buf_heap)
    free(buf_heap);

  close(fdin);
  close(fdout);
  return res;
}

/*  get_modem_img_info - get the MODEM image parameters.
 *  @img: the image to load
 *  @secure_offset: the offset of the partition where to search for the
 *                  MODEM image. The unit is byte.
 *  @is_sci: pointer to the variable to indicate whether the image is of
 *           SCI format.
 *  @total_len: pointer to the variable to hold the length of the whole
 *              MODEM image size in byte, excluding the secure header.
 *  @modem_exe_size: pointer to the variable to hold the MODEM
 *                   executable size.
 *
 *  Return Value:
 *    If the MODEM image is not SCI, return 0;
 *    if the MODEM image is SCI format, return the offset of the
 *    MODEM executable to the beginning of the SCI image (in unit
 *    of byte).
 *    if the function fails to read eMMC, return 0.
 */
unsigned get_modem_img_info(const IMAGE_LOAD_S* img,
                                   uint32_t secure_offset,
                                   int* is_sci,
                                   size_t* total_len,
                                   size_t* modem_exe_size) {
  // Non-MODEM image is not SCI format.
  if(!strstr(img->path_r, MODEM_BANK)) {
    *is_sci = 0;
    *total_len = img->size;
    *modem_exe_size = img->size;
    return 0;
  }

  unsigned offset = 0;
  int fdin = open(img->path_r, O_RDONLY);
  if (fdin < 0) {
    *is_sci = 0;
    *total_len = img->size;
    *modem_exe_size = img->size;

    MODEM_LOGE("Open %s failed: %d", img->path_r, errno);
    return 0;
  }

  if ((off_t)secure_offset != lseek(fdin, (off_t)secure_offset, SEEK_SET)) {
    MODEM_LOGE("lseek %s failed: %d", img->path_r, errno);

    close(fdin);

    *is_sci = 0;
    *total_len = img->size;
    *modem_exe_size = img->size;

    return 0;
  }

  /* Only support 10 effective headers at most for now. */
  data_block_header_t hdr_buf[11];
  size_t read_len = sizeof(hdr_buf);

  ssize_t nr = read(fdin, hdr_buf, read_len);
  if (read_len != (size_t)nr) {
    MODEM_LOGE("Read MODEM image header failed: %d, %d",
               (int)nr, errno);
    close(fdin);

    *is_sci = 0;
    *total_len = img->size;
    *modem_exe_size = img->size;
    return 0;
  }

  close(fdin);

  /* Check whether it's SCI image. */
  if (memcmp(hdr_buf, MODEM_MAGIC, strlen(MODEM_MAGIC))) {
    /* Not SCI format. */
    *is_sci = 0;
    *total_len = img->size;
    *modem_exe_size = img->size;

    return 0;
  }

  /* SCI image. Parse the headers */
  *is_sci = 1;

  unsigned i;
  data_block_header_t* hdr_ptr;
  int modem_offset = -1;
  int image_len = -1;

  for (i = 1, hdr_ptr = hdr_buf + 1;
       i < sizeof hdr_buf / sizeof hdr_buf[0];
       ++i, ++hdr_ptr) {
    unsigned type = (hdr_ptr->type_flags & 0xff);
    if (SCI_TYPE_MODEM_BIN == type) {
      modem_offset = (int)hdr_ptr->offset;
      *modem_exe_size = hdr_ptr->length;
      if(hdr_ptr->type_flags & MODEM_SHA1_HDR) {
        modem_offset += MODEM_SHA1_SIZE;
        *modem_exe_size -= MODEM_SHA1_SIZE;
      }
    }
    if (hdr_ptr->type_flags & SCI_LAST_HDR) {
      image_len = (int)(hdr_ptr->offset + hdr_ptr->length);
      break;
    }
  }

  if (-1 == modem_offset) {
    MODEM_LOGE("No MODEM image found in SCI image!");
  } else if (-1 == image_len) {
    MODEM_LOGE("SCI header too long!");
  } else {
    *total_len = image_len;
    offset = modem_offset;
    MODEM_LOGD("Modem SCI offset: 0x%x!", (unsigned)offset);
  }

  return offset;
}

void modem_get_patiton_info(IMAGE_LOAD_S *img,
                            unsigned int *boot_offset, size_t *size) {
  unsigned int load_offset = 0;
  int is_sci;
  size_t total_len;
  size_t modem_exe_size;

  load_offset = get_modem_img_info(img,
                                        0,
                                        &is_sci,
                                        &total_len,
                                        &modem_exe_size);

  MODEM_LOGD("%s: image[%s], load_offset=0x%x, load_size=0x%x\n",
             __FUNCTION__, img->name,
             load_offset, (unsigned int)modem_exe_size);

  *boot_offset = load_offset;
  *size = modem_exe_size;
}

LOAD_VALUE_S * modem_get_load_value(int img) {
  if (IMAGE_CP == img)
    return &cp_load_info;
  else if (IMAGE_SP == img)
    return &sp_load_info;
#ifdef FEATURE_EXTERNAL_MODEM
  else if (IMAGE_DP == img)
    return &dp_load_info;
#endif
  else
    return NULL;
}

#ifdef FEATURE_EXTERNAL_MODEM
void modem_reboot_all_modem(void) {
#ifdef FEATURE_PCIE_RESCAN
  int b_continue;
#endif

/*
 * remove reboot orca here,
 * it will be done in modem_ioctrl_reboot_ext_modem.
 */
#if 0
  int fd = -1, wsize;
  char reset_cmd[2] = {'3', 0};

  /* echo '3' > dev/mdm_ctrl to  reboot orca */

  fd = open(EXTERN_MDMCTRL_PATH, O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    MODEM_LOGE("Failed to open %s: %s\n",
               EXTERN_MDMCTRL_PATH, strerror(errno));
    return;
  }

  wsize = write(fd, reset_cmd, sizeof(reset_cmd));
  close(fd);

  if (wsize != sizeof(reset_cmd)) {
    MODEM_LOGE("Failed to write %s, wsize=%d, err is %s\n",
               EXTERN_MDMCTRL_PATH, wsize, strerror(errno));
    return;
  }
#endif

#ifdef FEATURE_PCIE_RESCAN
  do {
    /*  if rescan ep failed, can't load, continue rescan */
    b_continue = modem_rescan_ep_device();
    usleep(1000*1000);
  } while(b_continue);
#endif

  /* reboot external modem */
  if (cp_load_info.ioctrl_is_ok)
    modem_ioctrl_reboot_ext_modem(cp_load_info.io_ctrl);

  load_modem_img(LOADL_ALL_EXTERNAL_IMG);
}
#endif

