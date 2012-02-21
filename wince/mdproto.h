/*
 * Copyright (c) 2012 Alexey Illarionov <littlesavage@rambler.ru>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

enum mdproto_cmd_t {
   MDPROTO_CMD_PING               = 'z',
   MDPROTO_CMD_PING_RESPONSE      = 'Z',
   MDPROTO_CMD_MEM_READ           = 'x',
   MDPROTO_CMD_MEM_READ_RESPONSE  = 'X',
   MDPROTO_CMD_EXEC_CODE_RESPONSE ='Y',
   MDPROTO_CMD_FLASH_INFO         ='w',
   MDPROTO_CMD_FLASH_INFO_RESPONSE ='W',
   MDPROTO_CMD_FLASH_PROGRAM          ='v',
   MDPROTO_CMD_FLASH_PROGRAM_RESPONSE  ='V',
   MDPROTO_CMD_FLASH_ERASE_SECTOR     = 'u',
   MDPROTO_CMD_FLASH_ERASE_SECTOR_RESPONSE = 'U',

   MDPROTO_STATUS_OK                  = '+',
   MDPROTO_STATUS_WRONG_CMD           = '?',
   MDPROTO_STATUS_READ_HEADER_TIMEOUT = '.',
   MDPROTO_STATUS_READ_DATA_TIMEOUT   = ',',
   MDPROTO_STATUS_TOO_BIG             = '>',
   MDPROTO_STATUS_WRONG_CSUM          = '#',
   MDPROTO_STATUS_WRONG_PARAM         = '-'
};
#define MDPROTO_CMD_MAX_RAW_DATA_SIZE 508

#pragma pack(push,1)
struct mdproto_cmd_buf_t {
   uint16_t size;
   union {
      uint8_t id;
      uint8_t p[512];
   } data;
   uint8_t _csum_buf;
};
#pragma pack(pop)

#pragma pack(push,1)
struct mdproto_cmd_flash_info_t {

   /* software id. cmd 90h  */
   uint16_t manuf_id;
   uint16_t device_id;

   /* cfi query. cmd 98h  */
   /* 10 - 1a  */
   struct {
      uint8_t q, r, y;
      uint16_t primary_alg_id;
      uint16_t primary_alg_tbl;
      uint16_t secondary_alg_id;
      uint16_t secondary_alg_tbl;
   } cfi_id_string;

   /* 1b - 26  */
   struct {
      uint8_t vcc_min;                /* bits 0-3: BCD *100mV; bits 4-7: HEX V */
      uint8_t vcc_max;                /* bits 0-3: BCD *100mV; bits 4-7: HEX V */
      uint8_t vpp_min;                /* bits 0-3: BCD *100mV; bits 4-7: HEX V */
      uint8_t vpp_max;                /* bits 0-3: BCD *100mV; bits 4-7: HEX V */
      uint8_t word_write_tmout;       /* 1<<n us  */
      uint8_t buf_write_tmout;        /* 1<<n us  */
      uint8_t block_erase_tmout;      /* 1<<n ms  */
      uint8_t chip_erase_tmout;       /* 1<<n ms  */
      uint8_t max_word_write_tmout;   /* 1<<n * word_write_tmout  */
      uint8_t max_buf_write_tmout;    /* 1<<n * buf_write_tmout  */
      uint8_t max_block_erase_tmout;  /* 1<<n * block_erase_tmout  */
      uint8_t max_chip_erase_tmout;   /* 1<<n * chip_erase_tmout  */
   } interface_info;

   /* 27 - 34 */
   struct {
      uint8_t size;                  /* 1<<n bytes  */
      uint16_t interface_desc;
      uint16_t max_write_buf_size;   /* 1<<n bytes */
      uint8_t  num_erase_blocks;
      uint32_t erase_blocks[8];
   } flash_geometry;

};
#pragma pack(pop)

