/**
 * modem_load.h ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
 #ifndef MODEM_LOAD_H_
#define MODEM_LOAD_H_

/* modem/dsp partition, in here, will config them enough big */
#define MODEM_SIZE (20 * 1024 * 1024)
#define DELTANV_SIZE (1 * 1024 * 1024)
#define FIXNV_SIZE (1 * 1024 * 1024)
#define RUNNV_SIZE (2 * 1024 * 1024)
#define TGDSP_SIZE (5 * 1024 * 1024 + 64 * 1024)
#define LDSP_SIZE  (12 * 1024 * 1024)
#define CDSP_SIZE  (1 * 1024 * 1024)
#define WARM_SIZE  (5 * 1024 * 1024)
#define PMCP_SIZE    (1 * 1024 * 1024)
#define PMCP_CALI_SIZE    (64 * 1024)

#define MAX_PATH_LEN 128
#define MAX_FILE_NAME_LEN 30

#define mstrncpy2(dst, str1, str2)\
   do {\
     snprintf(dst, sizeof(dst), "%s%s", str1, str2);\
   } while (0)
#define mstrncpy3(dst, str1, str2, str3)\
   do {\
     snprintf(dst, sizeof(dst), "%s%s%s", str1, str2, str3);\
   } while (0)

/*
 * special: bit0 sec,  bit1 nv, bit2 boot iram, bit3 cmdline;
 * miniap: bit4 spl,  bit5 sml, bit6 uboot, bit7 boot;
 * modem: bit8 modem head, bit9 modem, bit10 mode dsp, bit11 other modem
 * pmsys:  bit12 pm, bit13 pm cali
 * audio:    bit16 adsp
 * bit 31 clear
 */

#define SECURE_FLAG 0
#define NV_FLAG 1
#define BOOT_CODE 2
#define CMDLINE_FLAG 3

#define SPL_FLAG 4
#define SML_FLAG 5
#define UBOOT_FLAG 6
#define BOOT_FLAG 7

#define MODEM_HEAD_FLAG 8
#define MODEM_FLAG 9
#define MODEM_DSP_FLAG 10
#define MODEM_OTHER_FLAG 11

#define SP_FLAG 12
#define SP_CALI_FLAG 13

#define ADSP_FLAG 16

#define CLR_FLAG 31

#define SPCIAL_IMG_FLAG 0x0000000F
#define MINIAP_IMG_FLAG 0x000000F0
#define MODEM_IMG_FLAG 0x00000F00
#define MODEM_IMG_EXCPT_HEAD_FLAG 0x00000E00
#define SP_IMG_FLAG 0x0000F000
#define AUDIO_IMG_FLAG 0x000F0000

#define SPL_IMG_FLAG (1 << (SPL_FLAG))
#define SML_IMG_FLAG (1 << (SML_FLAG))
#define UBOOT_IMG_FLAG (1 << (UBOOT_FLAG))
#define BOOT_IMG_FLAG (1 << (BOOT_FLAG))
#define MODEM_HEAD_IMG_FLAG (1 << (MODEM_HEAD_FLAG))

#define ALL_IMAG_FLAG 0xFFFFFFFF
#define NONE_IMAG_FLAG 0x00000000

#define SET_FLAG(flag, bit) ((flag) = (flag) | (1 << (bit)))
#define SET_2FLAG(flag, bit1, bit2) ((flag) = (flag) | (1 << (bit1)) | (1 << (bit2)))
#define GET_FLAG(flag, bit) ((flag) & (1 << (bit)))

 typedef struct image_load {
  char path_w[MAX_PATH_LEN + 1];
  char path_r[MAX_PATH_LEN + 1];
  char name[MAX_FILE_NAME_LEN + 1];
  uint64_t addr;
  uint32_t size;
  uint32_t flag;
} IMAGE_LOAD_S;

enum {
  IMAGE_LOAD_MODEM = 0,
  IMAGE_LOAD_DELTANV,
  IMAGE_LOAD_DSP,
  IMAGE_LOAD_GDSP,
  IMAGE_LOAD_LDSP,
  IMAGE_LOAD_CDSP,
  IMAGE_LOAD_WARM,
  IMAGE_LOAD_CMDLINE,
  IMAGE_LOAD_FIXNV,
  IMAGE_LOAD_RUNNV,
  IMAGE_LOAD_CP_NUM
};

enum {
  IMAGE_LOAD_SP = 0,
  IMAGE_LOAD_SP_CALI,
  IMAGE_LOAD_SP_NUM
};

enum {
  IMAGE_CP = 0,
  IMAGE_SP,
  IMAGE_DP
};

#define LOAD_SP_IMG 0x1
#define LOAD_MODEM_IMG 0x2
#define LOAD_DP_IMG 0x4
#define LOAD_MINIAP_IMG 0x8
#define LOAD_AGDSP_IMG 0x10

#define LOADL_ALL_EXTERNAL_IMG 0x1E
#define LOAD_ALL_IMG 0x1F

typedef struct load_value {
  IMAGE_LOAD_S *load_table;
  uint  table_num;
  uint64_t modem_base;
  size_t modem_size;
  uint64_t all_base;
  size_t all_size;
  int  xml_is_ok;
  int  drv_is_ok;
  int  ioctrl_is_ok;
  int  img_type;
  char io_ctrl[MAX_PATH_LEN + 1];
  char start[MAX_PATH_LEN + 1];
  char stop[MAX_PATH_LEN + 1];
  char name[MAX_FILE_NAME_LEN + 1];
} LOAD_VALUE_S;

#define MAX_MODEM_NODE_NUM      0xA
#define MAX_SP_NODE_NUM         0x1
#define MAX_LOAD_NODE_NUM       (MAX_MODEM_NODE_NUM + MAX_SP_NODE_NUM)

#define MODEM_START "start"
#define MODEM_STOP "stop"
#define MODEM_BANK "modem"
#define DELTANV_BANK "deltanv"
#define CODE_BANK "bootcode"
#define DSP_BANK "dsp"
#define TGDSP_BANK "tgdsp"
#define GDSP_BANK  "gdsp"
#define LDSP_BANK "ldsp"
#define CDSP_BANK "cdsp"
#define WARM_BANK "warm"
#define CMDLINE_BANK "cpcmdline"

#define MODEM_SYS_NODE        "/proc/cptl/ldinfo"
#define SP_SYS_NODE           "/proc/pmic/ldinfo"
#define MAX_MODEM_NODE_NAME_LEN    0x20

typedef struct load_node_info {
    char name[MAX_MODEM_NODE_NAME_LEN];
    uint32_t base;
    uint32_t size;
}LOAD_NODE_INFO;

void modem_load_assert_modem(void);
int init_modem_img_info(void);
int load_modem_img(int load_type);
int load_spl_img(void);

void modem_get_patiton_info(IMAGE_LOAD_S *img,
  unsigned int *boot_offset, size_t *size);
int modem_load_image(IMAGE_LOAD_S* img,
  int offsetin, int offsetout, uint size);
unsigned get_modem_img_info(const IMAGE_LOAD_S* img,
                                   uint32_t secure_offset,
                                   int* is_sci,
                                   size_t* total_len,
                                   size_t* modem_exe_size);
LOAD_VALUE_S * modem_get_load_value(int img);
void modem_clear_region(char *fin, uint size);
#ifdef FEATURE_EXTERNAL_MODEM
void modem_reboot_all_modem(void);
#endif
#endif
