/**
 * xml_parse.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
 #include <expat.h>

#include "modem_control.h"
#include "modem_load.h"
#include "xml_parse.h"

/*
 * ((v) == 0 && (p1) == (p2)),
 * means that there were no digits at all
 * ((v) == ULONG_MAX && ERANGE == errno),
 * means that  the original value would overflow
 */
#define STRTOUL_ERR(v, p1, p2) \
    (((v) == 0 && (p1) == (p2)) || ((v) == ULONG_MAX && ERANGE == errno))


#define MODEM_XML_BUF_SIZE 1024
#define CP_XML_PATH "/vendor/etc/modem_cp_info.xml"
#define SP_XML_PATH "/vendor/etc/modem_sp_info.xml"
#ifdef FEATURE_EXTERNAL_MODEM
#define DP_XML_PATH "/vendor/etc/modem_dp_info.xml"
#endif

typedef struct partion_item_s {
  uint64_t addr;
  uint32_t size;
  char dst_file[MAX_FILE_NAME_LEN + 1];
  char src_file[MAX_PATH_LEN + 1];
  char name[MAX_FILE_NAME_LEN + 1];
  uint32_t flag;
}partion_item;

typedef struct modem_xml_info_s {
  int partion_cnt;
  char name[MAX_FILE_NAME_LEN + 1];
  char src_path[MAX_PATH_LEN + 1];
  char dst_path[MAX_PATH_LEN + 1];
  char io_ctrol[MAX_PATH_LEN + 1];
  uint64_t modem_base;
  size_t modem_size;
  uint64_t all_base;
  size_t all_size;
  partion_item *item_arry;
}modem_xml_info;

static modem_xml_info cp_xml_info;
static modem_xml_info sp_xml_info;
#ifdef FEATURE_EXTERNAL_MODEM
static modem_xml_info dp_xml_info;
#endif

static modem_xml_info *cur_xml_info;

#define XML_STRTOUL(str, n, v, e) do {\
    if (str) {\
        v = strtoul(str, (char **)&e, 0);\
        if (STRTOUL_ERR(v, str, e))\
            MODEM_LOGE("xml strtoul invalid str = %s!\n", str);\
        else\
            n = v;\
    }\
}while(0)

static void modem_xml_start_tag(void *data, const XML_Char *tag_name,
                      const XML_Char **attr)
{
    const XML_Char *valptr = NULL;
    const XML_Char *base = NULL;
    const XML_Char *size = NULL;
    const XML_Char *name = NULL;
    const XML_Char *src = NULL;
    const XML_Char *dst = NULL;
    const XML_Char *flag = NULL;
    const XML_Char *endptr = NULL;
    unsigned long int value = 0;
    static int item_cnt;
    unsigned int i, cnt;
    partion_item *p_item;

    MODEM_LOGIF("start_tag: name =%s, data =%s!\n", tag_name, data);

    for (i = 0; attr[i]; i += 2) {
        if (strcmp(attr[i], "val") == 0)
            valptr = attr[i + 1];
        else if (strcmp(attr[i], "base") == 0)
            base = attr[i + 1];
        else if (strcmp(attr[i], "size") == 0)
            size = attr[i + 1];
        else if (strcmp(attr[i], "name") == 0)
            name = attr[i + 1];
        else if (strcmp(attr[i], "src_file") == 0)
            src = attr[i + 1];
        else if (strcmp(attr[i], "dst_file") == 0)
            dst = attr[i + 1];
        else if (strcmp(attr[i], "flag") == 0)
            flag = attr[i + 1];
    }

    if (valptr && strcmp(tag_name, "product") == 0)
        MODEM_LOGD("product:%s!\n", valptr);

    if (valptr && strcmp(tag_name, "modem_name") == 0) {
        MODEM_LOGD("modem_name:%s!\n", valptr);
        strncpy(cur_xml_info->name, valptr, MAX_FILE_NAME_LEN);
    }

    if (valptr && strcmp(tag_name, "partion_cnt") == 0) {
        cnt = strtoul(valptr, (char **)NULL, 0);
         /* 0 < cnt < 100 */
        if (cnt == 0 || cnt > 100) {
            MODEM_LOGE("invalid cnt:%s!\n", valptr);
            return;
        }
        MODEM_LOGD("partion_cnt:%d!\n", cnt);
        if (cur_xml_info->item_arry == NULL) {
            cur_xml_info->item_arry = malloc(sizeof(partion_item) * cnt);
            if(cur_xml_info->item_arry) {
              cur_xml_info->partion_cnt = cnt;
              memset(cur_xml_info->item_arry, 0, (sizeof(partion_item) * cnt));
            }
        }
        item_cnt = 0;
    }

    if (valptr && strcmp(tag_name, "ioctl_path") == 0) {
        MODEM_LOGD("ioctl_path:%s!\n", valptr);
        strncpy(cur_xml_info->io_ctrol, valptr, MAX_PATH_LEN);
    }

    if (valptr && strcmp(tag_name, "src_path") == 0) {
        MODEM_LOGD("src_path:%s!\n", valptr);
        strncpy(cur_xml_info->src_path, valptr, MAX_PATH_LEN);
    }

    if (valptr && strcmp(tag_name, "dst_path") == 0) {
        MODEM_LOGD("dst_path:%s!", valptr);
        strncpy(cur_xml_info->dst_path, valptr, MAX_PATH_LEN);
    }

    if (strstr(tag_name, "modem_range")) {
        XML_STRTOUL(base, cur_xml_info->modem_base, value, endptr);
        MODEM_LOGD("modem_range: base = 0x%lx!\n", value);

        XML_STRTOUL(size, cur_xml_info->modem_size, value, endptr);
        MODEM_LOGD("modem_range: size = 0x%lx!\n", value);
    }

    if (strstr(tag_name, "all_range")) {
        XML_STRTOUL(base, cur_xml_info->all_base, value, endptr);
        MODEM_LOGD("all_range: base = 0x%lx!\n", value);

        XML_STRTOUL(size, cur_xml_info->all_size, value, endptr);
        MODEM_LOGD("all_range: size = 0x%lx!\n", value);
    }

    if(!cur_xml_info->item_arry)
      return;

  if (strstr(tag_name, "partition")) {
    p_item = cur_xml_info->item_arry + item_cnt;

    XML_STRTOUL(base, p_item->addr, value, endptr);
    MODEM_LOGD("partition[%d]: base = 0x%lx!\n", item_cnt, value);

    XML_STRTOUL(size, p_item->size, value, endptr);
    MODEM_LOGD("partition[%d]: size = 0x%lx!\n", item_cnt, value);

    if (name) {
      strncpy(p_item->name, name, MAX_FILE_NAME_LEN);
      MODEM_LOGD("partition[%d]: name = %s!\n", item_cnt, name);
    }

    if (src) {
      strncpy(p_item->src_file, src, MAX_PATH_LEN);
      MODEM_LOGD("partition[%d]: src_file = %s!\n", item_cnt, src);
    }

    if (dst) {
      strncpy(p_item->dst_file, dst, MAX_FILE_NAME_LEN);
      MODEM_LOGD("partition[%d]: dst_file = %s!\n", item_cnt, dst);
    }

    XML_STRTOUL(flag, p_item->flag, value, endptr);
    MODEM_LOGD("partition[%d]: flag = 0x%lx!\n", item_cnt, value);

    item_cnt++;
  }
}

static void modem_xml_end_tag(void *data, const XML_Char *tag_name)
{
    MODEM_LOGIF("end_tag: name =%s, data =%s!\n", tag_name, data);
}

static void modem_xml_start_parse(char *path) {
  FILE *file;
  XML_Parser parser;
  int bytes_read;
  void *buf;
  bool eof = false;

  MODEM_LOGD("parse xml, path is %s\n", path);

  if (0 != access(path, F_OK)) {
    if (errno == ENOENT) {
      MODEM_LOGD("unsupport xml, path is %s\n", path);
    } else {
      MODEM_LOGE("Failed to access xml, error %s: %s\n",
                 path, strerror(errno));
    }
    return;
  }

  file = fopen(path, "r");
  if (!file) {
    MODEM_LOGE("Failed to open %s: %s\n", path, strerror(errno));
    return;
  }

  parser = XML_ParserCreate(NULL);
  if (!parser) {
    MODEM_LOGD("Failed to create XML parser\n");
    goto err_parser_create;
  }

  XML_SetUserData(parser, NULL);
  XML_SetElementHandler(parser,
      modem_xml_start_tag, modem_xml_end_tag);

  for (;;) {
      buf = XML_GetBuffer(parser, MODEM_XML_BUF_SIZE);
      if (buf == NULL) {
          MODEM_LOGE("GetBuffer err!\n");
          goto err_parse;
      }

      bytes_read = fread(buf, 1, MODEM_XML_BUF_SIZE, file);
      if (ferror(file)) {
          MODEM_LOGE("read err!\n");
          goto err_parse;
      }

      eof = feof(file);
      if (XML_ParseBuffer(parser, bytes_read, eof)
                          == XML_STATUS_ERROR) {
          MODEM_LOGE("Parse error at line %u:\n%s\n",
          (unsigned int)XML_GetCurrentLineNumber(parser),
          XML_ErrorString(XML_GetErrorCode(parser)));
          goto err_parse;
      }

      if (eof)
          break;
  }
err_parse:
    XML_ParserFree(parser);
err_parser_create:
    fclose(file);
}

static void modem_xml_init(int img)
{
  char *path;

  if (IMAGE_CP == img) {
    path = CP_XML_PATH;
    cur_xml_info = &cp_xml_info;
  } else if (IMAGE_SP == img) {
    path = SP_XML_PATH;
    cur_xml_info = &sp_xml_info;
  }
#ifdef FEATURE_EXTERNAL_MODEM
  else if (IMAGE_DP == img) {
    path = DP_XML_PATH;
    cur_xml_info = &dp_xml_info;
  }
#endif
  else
    return;

  memset(cur_xml_info, 0, sizeof(modem_xml_info));
  modem_xml_start_parse(path);
}

static int modem_xml_to_load_info(int img)
{
  IMAGE_LOAD_S *load_table;
  LOAD_VALUE_S *load_info;
  partion_item *item_arry;
  modem_xml_info *xml_info;
  int i, size;

  MODEM_LOGD("xml to load info, img = %d\n", img);

  if (IMAGE_CP == img)
    xml_info = &cp_xml_info;
  else if (IMAGE_SP == img)
    xml_info = &sp_xml_info;
#ifdef FEATURE_EXTERNAL_MODEM
  else if (IMAGE_DP == img)
    xml_info = &dp_xml_info;
#endif
  else
    return -1;

  item_arry = xml_info->item_arry;
  load_info = modem_get_load_value(img);

  if (!item_arry || !load_info)
    return -1;
 
  size = sizeof(IMAGE_LOAD_S) * xml_info->partion_cnt;
  load_table = (IMAGE_LOAD_S *)malloc(size);
  if (!load_table)
    return -1;

  /*  free old table */
  if (load_info->load_table)
    free(load_info->load_table);

  memset(load_table, 0, size);
  load_info->table_num = xml_info->partion_cnt;
  load_info->load_table = load_table;
  load_info->modem_base = xml_info->modem_base;
  load_info->modem_size = xml_info->modem_size;
  load_info->all_base   = xml_info->all_base;
  load_info->all_size   = xml_info->all_size;

  mstrncpy2(load_info->start, xml_info->dst_path, MODEM_START);
  mstrncpy2(load_info->stop, xml_info->dst_path, MODEM_STOP);
  strncpy(load_info->io_ctrl, xml_info->io_ctrol, MAX_PATH_LEN);
  strncpy(load_info->name, xml_info->name, MAX_FILE_NAME_LEN);

  for (i = 0; i < xml_info->partion_cnt; i++) {
    mstrncpy2(load_table[i].path_w, xml_info->dst_path, item_arry[i].dst_file);

    /* if src_file include folder, it means is a full path */
    if (strstr(item_arry[i].src_file, "/"))
       strncpy(load_table[i].path_r, item_arry[i].src_file, MAX_PATH_LEN);
    else
      mstrncpy2(load_table[i].path_r, xml_info->src_path, item_arry[i].src_file);

    strncpy(load_table[i].name, item_arry[i].name, MAX_FILE_NAME_LEN);
    load_table[i].addr = item_arry[i].addr;
    load_table[i].size  = item_arry[i].size;
    load_table[i].flag  = item_arry[i].flag;
  }

  if (xml_info->dst_path[0]) {
    if (0 == access(xml_info->dst_path, F_OK))
      load_info->drv_is_ok = 1;
    else
      MODEM_LOGE("Failed to access %s, error %s: \n",
                 xml_info->dst_path, strerror(errno));
  }

  if (xml_info->io_ctrol[0]) {
    if (0 == access(xml_info->io_ctrol, F_OK))
      load_info->ioctrl_is_ok = 1;
    else
      MODEM_LOGE("Failed to access %s, error %s: \n",
                 xml_info->io_ctrol, strerror(errno));
  }

  /* if ioctrl is ok , replace dst path with io_ctrol path */
  if (load_info->ioctrl_is_ok) {
    for (i = 0; i < xml_info->partion_cnt; i++) {
      strncpy(load_table[i].path_w, xml_info->io_ctrol, MAX_PATH_LEN);
    }
  }

  load_info->xml_is_ok =1;

  return 0;
}

int modem_xml_init_load_info(void) {
  /* init cp */
  modem_xml_init(IMAGE_CP);
  modem_xml_to_load_info(IMAGE_CP);

  /* init sp */
  modem_xml_init(IMAGE_SP);
  modem_xml_to_load_info(IMAGE_SP);

#ifdef FEATURE_EXTERNAL_MODEM
  /* init dp */
  modem_xml_init(IMAGE_DP);
  modem_xml_to_load_info(IMAGE_DP);
#endif

  return 0;
}

