/**
 * secure_boot_load.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#if defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
#include "modem_control.h"
#include "modem_verify.h"
#include "kernelbootcp_ca_ipc.h"
#include "modem_load.h"
#include "secure_boot_load.h"

// Add for kernel boot cp
#define MAX_CERT_SIZE              4096
#define AVB_CERT_SIZE              8192

#if defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
KBC_LOAD_TABLE_S       kbc_table;
#endif

#ifdef SECURE_BOOT_ENABLE
uint8_t s_modem_puk[PUBKEY_LEN];

int HexToBin(const char *hex_ptr, int length, char *bin_ptr) {
  char *dest_ptr = bin_ptr;
  int i;
  char ch;

  if (hex_ptr == NULL || bin_ptr == NULL) {
    return -1;
  }

  for (i = 0; i < length; i += 2) {
    ch = hex_ptr[i];
    if (ch >= '0' && ch <= '9')
      *dest_ptr = (char)((ch - '0') << 4);
    else if (ch >= 'a' && ch <= 'f')
      *dest_ptr = (char)((ch - 'a' + 10) << 4);
    else if (ch >= 'A' && ch <= 'F')
      *dest_ptr = (char)((ch - 'A' + 10) << 4);
    else
      return -1;

    ch = hex_ptr[i + 1];
    if (ch >= '0' && ch <= '9')
      *dest_ptr |= (char)(ch - '0');
    else if (ch >= 'a' && ch <= 'f')
      *dest_ptr |= (char)(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F')
      *dest_ptr |= (char)(ch - 'A' + 10);
    else
      return -1;

    dest_ptr++;
  }
  return 0;
}

static int modem_parse_puk_cmdline(uint8_t *puk_ptr) {
  int fd = -1, ret = 0, i = 0, flag = 0;
  char cmdline[CMDLINE_LENGTH] = {0};
  char puk_str[PUBKEY_LEN * 2 + 1] = {0};
  char *p_str = NULL;

  /* Read PUK from cmdline */
  fd = open("/proc/cmdline", O_RDONLY);
  if (fd < 0) {
    MODEM_LOGE("[secure]%s, /proc/cmdline open failed", __FUNCTION__);
    return 0;
  }
  ret = read(fd, cmdline, sizeof(cmdline));
  if (ret < 0) {
    MODEM_LOGE("[secure]%s,/proc/cmdline read failed", __FUNCTION__);
    close(fd);
    return 0;
  }
  MODEM_LOGD("[secure]%s,cmdline: %s\n", __FUNCTION__, cmdline);
  p_str = strstr(cmdline, CMD_PUKSTRING);
  if (p_str != NULL) {
    p_str += strlen(CMD_PUKSTRING);
    memcpy(puk_str, p_str, PUBKEY_LEN * 2);
    MODEM_LOGD("[secure]%s, puk_str = %s\n", __FUNCTION__, puk_str);
    HexToBin(puk_str, PUBKEY_LEN * 2, puk_ptr);
    flag = 1;
  } else {
    MODEM_LOGD("[secure]%s, parse puk failed", __FUNCTION__);
  }
  return flag;
}

static int modem_verify_image(char *fin, int offsetin, int size) {
  int ret = 0;
  int fdin = -1, readsize = 0, imagesize = 0;
  uint8_t *buf = NULL;

  MODEM_LOGD("[secure]%s: enter", __FUNCTION__);
  MODEM_LOGD("[secure]%s: fin = %s, size = %d", __FUNCTION__, fin, size);

  /* Read image */
  fdin = open(fin, O_RDONLY);
  if (fdin < 0) {
    MODEM_LOGE("[secure]%s: Failed to open %s", __FUNCTION__, fin);
    return -1;
  }
  if (lseek(fdin, offsetin, SEEK_SET) != offsetin) {
    MODEM_LOGE("[secure]failed to lseek %d in %s", offsetin, fin);
    ret = -1;
    goto leave;
  }

   imagesize = size;

  MODEM_LOGD("[secure]%s: imagesize = %d", __FUNCTION__, imagesize);
  buf = malloc(imagesize);
  if (buf == 0) {
    MODEM_LOGE("[secure]%s: Malloc failed!!", __FUNCTION__);
    ret = -1;
    goto leave;
  }
  memset(buf, 0, imagesize);
  readsize = read(fdin, buf, imagesize);
  MODEM_LOGD("[secure]%s: buf readsize = %d", __FUNCTION__, readsize);
  if (readsize <= 0) {
    MODEM_LOGE("[secure]failed to read %s", fin);
    ret = -1;
    goto leave;
  }

  /* Start verify */
  secure_verify(buf, s_modem_puk);
  ret = 0;
leave:
  close(fdin);
  free(buf);
  return ret;
}
#endif

#if defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
void dumpHex(const char *title, uint8_t * data, int len)
{
    int i, j;
    int N = len / 16 + 1;
    MODEM_LOGD("%s\n", title);
    MODEM_LOGD("dumpHex:%d bytes", len);
    for (i = 0; i < N; i++) {
        MODEM_LOGD("\r\n");
        for (j = 0; j < 16; j++) {
            if (i * 16 + j >= len)
                goto end;
            MODEM_LOGD("%02x", data[i * 16 + j]);
        }
    }
end:    MODEM_LOGD("\r\n");
    return;
}

static uint32_t get_verify_img_info(char *fin, uint32_t *is_packed)
{
    int               fd = -1;
    sys_img_header    header;
    uint32_t          read_len = 0;
    uint32_t          cert_off = 0;
    uint32_t          ret_size = 0;

    if (NULL == fin || NULL == is_packed) {
        MODEM_LOGD("[secure]%s: input para wrong!\n", __func__);
        return 0;
    }
    modem_ctrl_enable_busmonitor(false);
    modem_ctrl_enable_dmc_mpu(false);
    MODEM_LOGD("[secure]%s enter, fin = %s\n", __func__, fin);
    fd = open(fin, O_RDONLY);
    if (fd < 0) {
        modem_ctrl_enable_busmonitor(true);
        modem_ctrl_enable_dmc_mpu(true);
        MODEM_LOGE("[secure]failed to open %s", fin);
        return 0;
    }
    if (lseek(fd, 0, SEEK_SET) != 0) {
        MODEM_LOGE("[secure]failed to lseek %s", fin);
        goto LEAVE;
    }
    memset(&header, 0, sizeof(sys_img_header));
    read_len = read(fd, &header, sizeof(sys_img_header));
    MODEM_LOGD("[secure]%s read_len = %d\n", __func__, read_len);
    if (read_len <= 0) {
        MODEM_LOGE("[secure]read failed!");
        goto LEAVE;
    }
    MODEM_LOGE("[secure] ImgSize = %x", header.mImgSize);
    MODEM_LOGE("[secure] is_packed = %d", header.is_packed);
    MODEM_LOGE("[secure] mFirmwareSize = %x", header.mFirmwareSize);
    if (1 == header.is_packed) {
        *is_packed = header.is_packed;
        ret_size = header.mFirmwareSize;
    } else {
        ret_size = header.mImgSize;
    }
LEAVE:
    modem_ctrl_enable_busmonitor(true);
    modem_ctrl_enable_dmc_mpu(true);
    close(fd);
    MODEM_LOGE("[secure] ret_size = %x", ret_size);
    return ret_size;
}

void fill_verify_table(uint32_t size, uint64_t addr, char *name,
                       uint32_t maplen, KBC_LOAD_TABLE_S *table)
{
    if (NULL == name || NULL == table) {
        MODEM_LOGD("[secure]%s: input para wrong!\n", __func__);
	    return;
    }
    MODEM_LOGD("[secure]%s: name: %s\n", __func__, name);
    if( (strstr(name, "pm_sys") != NULL) ||
        (strstr(name, "dev/pmsys") != NULL)){
        table->pm_sys.img_len  = size;
        table->pm_sys.img_addr = addr;
        table->pm_sys.map_len  = maplen;
#ifndef NOT_VERIFY_MODEM
    } else if (strstr(name, MODEM_BANK) != NULL){
        table->modem.img_len  = size;
        table->modem.img_addr = addr;
        table->modem.map_len  = maplen;
//    } else if (strstr(name, TGDSP_BANK) != NULL){
    } else if (strstr(name, GDSP_BANK) != NULL){
        table->tgdsp.img_len  = size;
        table->tgdsp.img_addr = addr;
        table->tgdsp.map_len  = maplen;
    } else if (strstr(name, LDSP_BANK) != NULL){
        table->ldsp.img_len  = size;
        table->ldsp.img_addr = addr;
        table->ldsp.map_len  = maplen;
#endif
#ifdef SHARKL5_CDSP
    } else if (strstr(name, CDSP_BANK) != NULL){
        table->cdsp.img_len  = size;
        table->cdsp.img_addr = addr;
        table->cdsp.map_len  = maplen;
#endif
    } else {
        MODEM_LOGD("[secure]%s: name not match\n", __func__);
    }
    return;
}

static void get_verify_img_footer(char *fin, uint8_t *footer)
{
    int         fd = -1;
    uint32_t    read_len = 0;
    int         size = 0;
    off_t     offset = 0;
    off_t     where = 0;
    int         ret = -1;

    if (NULL == fin || NULL == footer) {
        MODEM_LOGD("[secure]%s: input para wrong!\n", __func__);
        return;
    }
    modem_ctrl_enable_busmonitor(false);
    modem_ctrl_enable_dmc_mpu(false);
    MODEM_LOGD("[secure]%s enter, fin = %s\n", __func__, fin);
    fd = open(fin, O_RDONLY);
    if (fd < 0) {
        modem_ctrl_enable_busmonitor(true);
        modem_ctrl_enable_dmc_mpu(true);
        MODEM_LOGE("[secure]failed to open %s", fin);
        return;
    }
    size = lseek(fd, 0, SEEK_END);
    MODEM_LOGD("[secure]%s size = %#x\n", __func__, size);
    if (size < 0) {
        MODEM_LOGE("[secure]failed to get partion size %s", fin);
        goto LEAVE;
    }
    offset = size - AVB_FOOTER_SIZE;
    MODEM_LOGD("[secure]%s offset = %#lx\n", __func__, offset);
    where = lseek(fd, offset, SEEK_SET);
    if (where == -1) {
        MODEM_LOGE("Error seeking to offset.\n");
        goto LEAVE;
    }
    if (where != offset) {
        MODEM_LOGE("Error seeking to offset.\n");
        goto LEAVE;
   }
    MODEM_LOGD("[secure]%s read footer.\n", __func__);
    read_len = read(fd, footer, AVB_FOOTER_SIZE);
    MODEM_LOGD("[secure]%s read_len = %d\n", __func__, read_len);
    if (read_len <= 0) {
        MODEM_LOGE("[secure]read failed!");
        goto LEAVE;
    }
LEAVE:
    modem_ctrl_enable_busmonitor(true);
    modem_ctrl_enable_dmc_mpu(true);
    close(fd);
    return;
}

/* parse file name from absolutely path */
static const char *parse_file_name(const char *path_name)
{
    const char *file_name;

    /* if it's not a path, nothing else to do */
    file_name = strrchr(path_name, '/');
    if(!file_name)
        return NULL;

    file_name++;

    return file_name;
}

static void get_image_footer_byname(char *name, char *fin, KBC_LOAD_TABLE_S *table)
{
    KBC_IMAGE_S   *img_ptr = NULL;
    const char *partition = NULL;

    if (NULL == name || NULL == table || NULL == fin) {
        MODEM_LOGD("[secure]%s: input para wrong!\n", __func__);
        return;
    }
    MODEM_LOGD("[secure]%s: name: %s\n", __func__, name);
    if ((strstr(name, "pm_sys") != NULL) ||
        (strstr(name, "dev/pmsys") != NULL)) {
        img_ptr = &table->pm_sys;
#ifndef NOT_VERIFY_MODEM
    } else if (strstr(name, MODEM_BANK) != NULL){
        img_ptr = &table->modem;
    } else if (strstr(name, GDSP_BANK) != NULL){
        img_ptr = &table->tgdsp;
    } else if (strstr(name, LDSP_BANK) != NULL){
        img_ptr = &table->ldsp;
#endif
#ifdef SHARKL5_CDSP
    } else if (strstr(name, CDSP_BANK) != NULL){
        img_ptr = &table->cdsp;
#endif
    } else {
        MODEM_LOGD("[secure]%s: name not match\n", __func__);
    }
    if (img_ptr != NULL) {
        partition = parse_file_name(fin);
        if (NULL == partition) {
           MODEM_LOGD("[secure]%s: partition name empty!\n", __func__);
           return;
        }
        strcpy((char*)img_ptr->partition, partition);
        MODEM_LOGD("[secure]%s: partition: %s\n", __func__, img_ptr->partition);

        get_verify_img_footer(fin, img_ptr->footer);
    }
    return;
}

static int modem_check_cmdline(char *value) {
  int fd = -1, ret = 0, flag = 0;
  char cmdline[CPCMDLINE_SIZE] = {0};
  char *p_str = NULL;

  fd = open("/proc/cmdline", O_RDONLY);
  if (fd < 0) {
    MODEM_LOGD("[secure]%s, /proc/cmdline open failed", __FUNCTION__);
    return 0;
  }
  ret = read(fd, cmdline, sizeof(cmdline));
  if (ret < 0) {
    MODEM_LOGD("[secure]%s,/proc/cmdline read failed", __FUNCTION__);
    close(fd);
    return 0;
  }
  MODEM_LOGD("[secure]%s,cmdline: %s\n", __FUNCTION__, cmdline);
  p_str = strstr(cmdline, value);
  if (p_str != NULL) {
      MODEM_LOGD("[secure]%s, str -%s- found", __FUNCTION__, value);
      flag = 1;
  } else {
      MODEM_LOGD("[secure]%s, str -%s- not found", __FUNCTION__, value);
      flag = 0;
  }
  close(fd);
  return flag;
}
#endif

void secure_boot_init(void) {
#ifdef SECURE_BOOT_ENABLE
  memset(s_modem_puk, 0, sizeof(s_modem_puk));
  if (!modem_parse_puk_cmdline(s_modem_puk)) {
    MODEM_LOGD("[secure]: modem_parse_puk_cmdline failed!!!");
  }
#endif

#if defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
  memset(&kbc_table, 0, sizeof(KBC_LOAD_TABLE_S));
#endif
}

int secure_boot_load_img(IMAGE_LOAD_S *img) {
  unsigned int load_offset = 0;
  size_t load_size = 0;

  secure_boot_get_patiton_info(img, &load_offset, &load_size);
#ifdef SECURE_BOOT_ENABLE
  MODEM_LOGD("[secure]verify start");
  modem_verify_image(img->path_r, 0, (int)load_size);
  MODEM_LOGD("[secure]verify done.");
#else
#if defined(CONFIG_SPRD_SECBOOT)
  // Get image header info(size, total size, packed flag)
  uint32_t mImgsize = get_verify_img_info(img->path_r,
                                          &kbc_table.is_packed);
  if (mImgsize != 0) {
      // fill verify table
      fill_verify_table(mImgsize, img->addr, img->path_w,
                        img->size, &kbc_table);
  } else {
      MODEM_LOGD("[secure] get_verify_img_info failed!!! \n");
  }
#else
#if defined(CONFIG_VBOOT_V2)
  MODEM_LOGD("[secure] run in vboot v2 \n");
  MODEM_LOGD("[secure] load_offset = 0x%x \n", load_offset);
  // Get image footer info
  get_image_footer_byname(img->path_w, img->path_r, &kbc_table);
  // fill verify table
  fill_verify_table(img->size, img->addr, img->path_w,
                    img->size, &kbc_table);
  if (load_offset > 0) {
      kbc_table.packed_offset = load_offset;
  }
#endif
#endif
#endif
  return modem_load_image(img, load_offset, 0, (uint)load_size);
}

void secure_boot_verify_all(void) {
#if defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
  int        ret = 0;

	// Check calibration mode
	MODEM_LOGD("[secure] check calibration mode \n");
	if (modem_check_cmdline("calibration")) {
		MODEM_LOGD("[secure] is calibration mode \n");
	}
  // Call verify func
  ret = kernel_bootcp_verify_all(&kbc_table);
  MODEM_LOGD("[secure]%s: ret = %d\n", __func__, ret);
  if ( ret < 0 ) {
      MODEM_LOGD("[secure]%s: verify failed!!!\n", __func__);
      while(1);
  }
  memset(&kbc_table, 0, sizeof(KBC_LOAD_TABLE_S));
#endif
}

void secure_boot_set_flag(int load_type)
{
    kbc_table.flag = load_type;
}

void secure_boot_get_patiton_info(IMAGE_LOAD_S *img,
  unsigned int *boot_offset, size_t *size) {
  unsigned int normal_offset = 0;
  unsigned int load_offset = 0;
  size_t load_size = 0;
  int is_sci;
  size_t total_len;
  size_t modem_exe_size;
  int secure_offset = 0;

  if (GET_FLAG(img->flag, SECURE_FLAG))
  {
#ifdef SECURE_BOOT_ENABLE
    secure_offset = BOOT_INFO_SIZE + VLR_INFO_SIZ;
#else
#if defined(CONFIG_SPRD_SECBOOT)
    secure_offset = sizeof(sys_img_header);
#endif
#endif
  }

  normal_offset = get_modem_img_info(img,
                                        secure_offset,
                                        &is_sci,
                                        &total_len,
                                        &modem_exe_size);

  load_offset = normal_offset + secure_offset;
  load_size = modem_exe_size;

  /* MODEM image is SCI format, the size is the actual size.
    * the other  images size is enough big, no need add the  cert size
    */
  if (strstr(img->path_r, MODEM_BANK)) {
   if (GET_FLAG(img->flag, SECURE_FLAG))
    {
  #if defined(CONFIG_SPRD_SECBOOT)
      load_size += MAX_CERT_SIZE;
  #else
  #if defined(CONFIG_VBOOT_V2)
      load_size += AVB_CERT_SIZE;
  #endif
  #endif
    }
  }
  MODEM_LOGD("%s: image[%s], normal_offset=0x%x, load_offset=0x%x, load_size=0x%x\n",
    __FUNCTION__, img->name, normal_offset, load_offset, (unsigned int)load_size);

  *boot_offset = load_offset;
  *size = load_size;
}

void secure_boot_unlock_ddr(void)
{
  int ret = 0;
  ret = kernel_bootcp_unlock_ddr(&kbc_table);
  MODEM_LOGD("[secure]%s: ret = %d\n", __func__, ret);
  if ( ret < 0 )
  {
      MODEM_LOGD("[secure]%s: unlock_ddr failed!!!\n", __func__);
      while(1);
  }
}

#endif
