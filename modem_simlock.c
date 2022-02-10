/**
 * modem_simlock.c ---
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */
#include "modem_simlock.h"
#include "modem_control.h"

typedef struct __attribute__((packed)) {
  uint32_t len;
  uint16_t id;
  uint8_t flag;
  uint8_t uuid[16];
  uint32_t cmd_id;
  uint8_t ver;
  uint8_t x_or;
} tee_msg_rd_t;

typedef struct __attribute__((packed)) {
  uint32_t len;
  uint16_t id;
  uint8_t flag;
  uint32_t ret_code;
  uint32_t cmd_data;
  uint8_t x_or;
} tee_rsp_rd_t;

/* return 0 for success, not-zero for failure */
extern uint32_t TEECex_SendMsg_To_TEE(uint8_t* msg,uint32_t msg_len,uint8_t* rsp, uint32_t* rsp_len);

static uint8_t cal_xor(uint8_t *pin,uint32_t inlen)
{
  int i = 0;
  uint8_t xor = 0;
  for(i = 0; i < inlen; i++){
    xor ^= pin[i];
  }
  return xor;
}

static void EndianConvert(uint8_t *dst, uint8_t *src, int count)
{
  char *tmp = dst;
  char *s = src + count - 1;
  while(count --) {
    *tmp ++= *s--;
  }
}

void add_spuk_cmdline(char *str)
{
  uint32_t ret = 0;
  uint32_t tee_rsp_len = 0;
  uint32_t spuk = 0;
  uint8_t spuk_str[64] = {0};
  tee_msg_rd_t tee_msg_rd = {0x19000000, 0x0201, 0x80, {0xB3,0x78,0xEA,0x66,0x48,0xF7,0x11,0xE6,
    0xB8,0x78,0xE3,0xFF,0xB7,0x61,0x7E,0x0B}, 0x02010010, 0x1, 0x0};
  tee_rsp_rd_t tee_rd_rsp = {0};
  tee_msg_rd.id = 0x0301;
  tee_msg_rd.cmd_id = 0x03010010;
  tee_msg_rd.x_or = cal_xor((uint8_t*)&tee_msg_rd,sizeof(tee_msg_rd)-1);
  MODEM_LOGD("%s: add simlock.puk in cmdline",  __FUNCTION__);
  ret = TEECex_SendMsg_To_TEE(&tee_msg_rd, sizeof(tee_msg_rd_t), &tee_rd_rsp, &tee_rsp_len);
  MODEM_LOGD("%s: TEECex_SendMsg_To_TEE ret=%x",  __FUNCTION__, ret);
  if(ret){
    spuk = 0;
  }else{
    EndianConvert((uint8_t*)&spuk, (uint8_t*)&(tee_rd_rsp.cmd_data), 4);
  }
  MODEM_LOGD("%s: simlock.puk=0x%x",  __FUNCTION__, spuk);
  snprintf(spuk_str, sizeof(spuk_str), " simlock.puk=0x%x", spuk);
  strncat(str, spuk_str, sizeof(spuk_str));
}

