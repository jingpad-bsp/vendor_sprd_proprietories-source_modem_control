/**
 * modem_head_parse.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#include "modem_control.h"
#include "modem_load.h"
#include "xml_parse.h"
#include "modem_head_parse.h"

#if defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
#include "secure_boot_load.h"
#endif

#define BASE_TO_APBASE(addr)	(addr)

#define OLD_HEAD_MAGIC_INFO	0x5043454D
#define NEW_HEAD_MAGIC_INFO	0x44485043

#define MAX_NAME_LEN	0x14
#define MAX_HEAD_LENGTH	0x100
#define MAX_REGION_NUM	0xC
#define MAX_RSVMEM_NUM	0x4
#define MAX_RES_NUM		0x4
#define MAX_CODE_NUM	0x32
#define MAX_HEADER_SIZE	0x600

struct mem_data {
	char name[MAX_NAME_LEN];
	uint64_t base;//  __aligned(8)
	uint32_t size;
};

struct region_data {
	struct mem_data res;
	uint32_t flag; //load flag
};

struct iram_code {
	uint32_t count;
	uint32_t code[MAX_CODE_NUM];
};

struct rsvmem_info {
	struct region_data rsvmem[MAX_RSVMEM_NUM];
};

struct boot_info {
	struct iram_code bcode;
	struct region_data regions[MAX_REGION_NUM];
	struct mem_data res[MAX_RES_NUM];
	uint32_t ex_info;
};

struct comm_info {
	struct mem_data res[MAX_RES_NUM];
	uint32_t ex_info;
};

struct coupling_info {
	uint32_t magic;
	uint32_t version;
	uint32_t length;
	uint32_t ex_info;
	struct rsvmem_info rsvmem_info;
	struct boot_info boot_info;
	struct comm_info comm_info;
};

/* using the new format */
struct new_img_desc {
	char desc[MAX_NAME_LEN];
	uint32_t offs;
	uint32_t size;
};

struct new_img_head {
	uint32_t magic;
	struct new_img_desc desc[1];
};

struct img_region_info {
	char name[MAX_NAME_LEN];
	uint64_t base;// __aligned(8);
	uint32_t size;
	uint32_t attr;
};

struct new_coupling_info {
	uint32_t magic;
	uint32_t version;
	struct img_region_info region[1];
};

struct modem_image_info {
  uint32_t region_num;
  struct iram_code boot_code;
  struct img_region_info table[MAX_REGION_NUM];
};

union modem_header {
	struct new_img_head iheader;
	struct coupling_info mem_decoup;
};

static unsigned int modem_decouple_head[MAX_HEADER_SIZE/sizeof(int)];// __aligned(16);
static struct modem_image_info modem_img;

static IMAGE_LOAD_S * modem_head_find_modem(LOAD_VALUE_S *load_info) {
  int i, n;
  IMAGE_LOAD_S *table = load_info->load_table;

  n = load_info->table_num;
  for (i = 0; i < n; i++) {
    if (0 == strcmp(MODEM_BANK, table->name))
      return table;

    table++;
  }

 return NULL;
}

static int modem_head_get_head(LOAD_VALUE_S *load_info) {
  unsigned int offset;
  size_t size;
  int fd;
  IMAGE_LOAD_S *img;

  img = modem_head_find_modem(load_info);
  if (!img) {
    MODEM_LOGE("%s: can't get modem img!\n", __FUNCTION__);
    return MODEM_ERR;
  }
  
#if defined(SECURE_BOOT_ENABLE) || defined(CONFIG_SPRD_SECBOOT) || defined(CONFIG_VBOOT_V2)
  secure_boot_get_patiton_info(img, &offset, &size);
#else
  modem_get_patiton_info(img, &offset, &size);
#endif

  fd = open(img->path_r, O_RDONLY);
  if (fd < 0) {
    MODEM_LOGE("failed to open %s, error: %s", img->path_r, strerror(errno));
    return MODEM_ERR;
  }

  if (lseek(fd , offset, SEEK_SET) != (off_t)offset) {
    MODEM_LOGE("failed to lseek %d in %s", offset, img->path_r);
    close(fd);
    return MODEM_ERR;
  }

  size = read(fd, modem_decouple_head, sizeof(modem_decouple_head));
  if (size != sizeof(modem_decouple_head)) {
    MODEM_LOGE("failed to read %zu in %s", size, img->path_r);
    close(fd);
    return MODEM_ERR;
  }
  close(fd);

  return 0;
}

static void modem_head_old_construct(struct coupling_info *decoup_data) {
  int i;
  struct region_data *region;
  struct iram_code *boot_code;

  region = &decoup_data->boot_info.regions[0];
  for (i = 0; i < MAX_REGION_NUM && strlen(region->res.name); i++, region++) {
    MODEM_LOGD("%s: name=%s, base=0x%lx, size=0x%x!", __FUNCTION__,
      region->res.name, region->res.base, region->res.size);
    modem_img.table[i].size = region->res.size;
    modem_img.table[i].base = BASE_TO_APBASE(region->res.base);
    strncpy(modem_img.table[i].name, region->res.name, MAX_NAME_LEN -1);
  }
  modem_img.region_num = i;
  boot_code = &decoup_data->boot_info.bcode;
  memcpy(modem_img.boot_code.code,
       boot_code->code,
       boot_code->count * sizeof(int));
  modem_img.boot_code.count = boot_code->count;
}

static void modem_head_new_construct(struct new_img_head *ih_desc) {
  int i;
  struct new_img_desc *i_desc;
  struct new_coupling_info *c_info;
  struct img_region_info *region;

  for (i_desc = ih_desc->desc; i_desc->size != 0; i_desc++) {
   if (!strcmp("decoup-desc", i_desc->desc)) {
     MODEM_LOGD("%s: decoup desc was found!", __FUNCTION__);
     c_info = (struct new_coupling_info *)((char *)modem_decouple_head + i_desc->offs);
     region = c_info->region;
     for (i = 0; (i < MAX_REGION_NUM) && region->size; i++, region++) {
       MODEM_LOGD("%s: name=%s, base=0x%lx, size=0x%x!", __FUNCTION__,
        region->name, region->base, region->size);
       modem_img.table[i].size = region->size;
       modem_img.table[i].base = BASE_TO_APBASE(region->base);
       strncpy(modem_img.table[i].name, region->name, MAX_NAME_LEN -1);
     }
     modem_img.region_num = i;
   } else if (!strcmp("boot-code", i_desc->desc)) {
    MODEM_LOGD("%s: boot-code was found!", __FUNCTION__);
    memcpy(modem_img.boot_code.code,
         (char *)modem_decouple_head + i_desc->offs,
         i_desc->size);
    modem_img.boot_code.count = i_desc->size;
   }
  }
}

static void modem_head_construct_img(void) {
  union modem_header *c_header;
  struct new_img_head *ih_desc;
  struct coupling_info *decoup_data;

  c_header = (union modem_header *)modem_decouple_head;
  ih_desc = &c_header->iheader;
  MODEM_LOGD("%s: magic = 0x%x", __FUNCTION__, ih_desc->magic);

  if (ih_desc->magic == OLD_HEAD_MAGIC_INFO) {
    decoup_data = &c_header->mem_decoup;
    modem_head_old_construct(decoup_data);
  } else if (ih_desc->magic == NEW_HEAD_MAGIC_INFO) {
    modem_head_new_construct(ih_desc);
  }
}

static int modem_head_convert_img(LOAD_VALUE_S *load_info)
{
  IMAGE_LOAD_S *load_table;
  struct img_region_info *region;
  uint32_t region_cnt, i, j;

  region_cnt = modem_img.region_num;
  region = modem_img.table;
  MODEM_LOGD("%s: region_cnt = %d!\n", __FUNCTION__, region_cnt);

  if (!region_cnt)
    return MODEM_ERR;

  load_table = load_info->load_table;
  if (!load_table)
    return MODEM_ERR;

  for (i = 0; i < load_info->table_num; i++) {
    for (j = 0; j < region_cnt; j++) {
      if (0 == strcmp(load_table[i].name, region[j].name)) {
        MODEM_LOGD("%s: find, region[%d].name = %s!\n",
                   __FUNCTION__, j, region[j].name);
        load_table[i].addr = region[j].base;
        load_table[i].size = region[j].size;
        break;
      }
    }

    /*  can't find in modem head region */
    if (j == region_cnt) {
      if (!load_info->xml_is_ok) {
        /* not support xml, invalid it */
        load_table[i].size = 0;
        MODEM_LOGIF("%s: can't find in modem head, invalid it!", load_table[i].name);
      } else {
        /* support xml, if is modem bin(except mode head) flag, invalid it */
        if (MODEM_IMG_EXCPT_HEAD_FLAG & load_table[i].flag) {
          load_table[i].size = 0;
          MODEM_LOGIF("%s: can't find in modem head, invalid it!", load_table[i].name);
        }
      }
    } else {
      MODEM_LOGD("load_table[%d].name = %s: addr=0x%lx, size =0x%x\n",
                 i, load_table[i].name, load_table[i].addr, load_table[i].size);
    }
  }

  return MODEM_SUCC;
}

int modem_head_correct_load_info(LOAD_VALUE_S *load_info) {
  MODEM_LOGD("%s\n", __FUNCTION__);

  if(MODEM_SUCC == modem_head_get_head(load_info))
    modem_head_construct_img();

  return modem_head_convert_img(load_info);
}

int modem_head_get_boot_code(char *buf, uint32_t size) {
  uint32_t copy = 0;

  if (modem_img.boot_code.count > 0) {
    /* size = instruction count * sizeof(int) */
    copy = modem_img.boot_code.count * sizeof(int);
    copy = min(copy, size);
    memcpy(buf, modem_img.boot_code.code, copy);
  }

  return copy;
}
