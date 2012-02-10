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

#include <sys/types.h>
#include <stdint.h>

#include "mdproto.h"

int mdproto_pkt_init(struct mdproto_cmd_buf_t *buf,
      unsigned cmd_id,
      void *raw_data,
      unsigned raw_data_size)
{
   unsigned data_size;
   unsigned i;

   if (raw_data_size > MDPROTO_CMD_MAX_RAW_DATA_SIZE)
      return -1;

   /* raw_data_size + id  */
   data_size = raw_data_size+1;

   buf->data.id = cmd_id;
   buf->size = (data_size << 8) | (data_size >> 8);

   for (i=0; i < raw_data_size; i++)
      buf->data.p[i+1] = ((uint8_t *)raw_data)[i];

   buf->data.p[raw_data_size+1] = mdproto_pkt_csum(buf, data_size+2);

   /* size, id, data, csum  */
   return raw_data_size+4;
}

uint8_t mdproto_pkt_csum(void *buf, size_t size)
{
   uint8_t csum = 0;
   size_t i;

   for (i=0; i < size ; i++)
      csum += ((uint8_t *)buf)[i];
   return (uint8_t)(0 - csum);
}

int mdproto_pkt_append(struct mdproto_cmd_buf_t *buf,
      void *data, unsigned appended_size)
{
   unsigned i;
   unsigned payload_size;
   unsigned new_size;
   int8_t csum;

   payload_size = MDPROTO_CMD_SIZE(*buf);
   new_size = payload_size + appended_size;

   if (new_size > MDPROTO_CMD_MAX_RAW_DATA_SIZE)
      return -1;

   csum = (int8_t)(0 - buf->data.p[payload_size]);
   csum -= (int8_t)((payload_size >> 8)&0xff);
   csum -= (int8_t)(payload_size & 0xff);
   csum += (int8_t)((new_size >> 8)&0xff);
   csum += (int8_t)(new_size & 0xff);

   for(i=0; i<appended_size; i++) {
      buf->data.p[payload_size+i] = ((uint8_t *)data)[i];
      csum += ((uint8_t *)data)[i];
   }

   buf->size = (new_size << 8) | (new_size >> 8);
   buf->data.p[new_size] = (uint8_t)(0-csum);

   return new_size+3;
}




