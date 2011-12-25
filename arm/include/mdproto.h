/*
 * Copyright (c) 2011 Alexey Illarionov <littlesavage@rambler.ru>
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

#ifndef _MDPROTO_H
#define _MDPROTO_H

#include <stdint.h>

enum mdproto_cmd_t {
   MDPROTO_CMD_PING               ='z',
   MDPROTO_CMD_PING_RESPONSE      ='Z',
   MDPROTO_CMD_MEM_READ           ='x',
   MDPROTO_CMD_MEM_READ_RESPONSE  ='X',

   MDPROTO_STATUS_OK = '+',
   MDPROTO_STATUS_WRONG_CMD = '?',
   MDPROTO_STATUS_READ_HEADER_TIMEOUT = '.',
   MDPROTO_STATUS_READ_DATA_TIMEOUT = ',',
   MDPROTO_STATUS_TOO_BIG='>',
   MDPROTO_STATUS_WRONG_CSUM='#',
   MDPROTO_STATUS_WRONG_PARAM='-'
};

struct mdproto_cmd_buf_t {
   uint16_t size;
   union {
      uint8_t id;
      uint8_t p[512];
   } data;
   uint8_t _csum_buf;
} __attribute__((packed));
#define MDPROTO_CMD_SIZE(_p) ((((_p).size << 8) | (((_p).size >> 8) & 0xff)) & 0xffff)
#define MDPROTO_CMD_MAX_RAW_DATA_SIZE 508

int mdproto_pkt_init(struct mdproto_cmd_buf_t *buf,
      unsigned cmd_id,
      void *raw_data,
      unsigned raw_data_size);

uint8_t mdproto_pkt_csum(void *buf, size_t size);

#endif
