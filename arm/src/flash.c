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


#include <sys/types.h>
#include <stdint.h>

#include "sirfgps.h"
#include "sirfgpsconf.h"

#include "mdproto.h"

#define EXT_SRAM_CSN0 0x40000000

extern volatile enum sirfgps_version_e gps_version; /* sirfmemdump.c */

/* sirfmemdump.c  */
void wait(unsigned n);

int flash_get_info(struct mdproto_cmd_flash_info_t *dst);

int flash_get_info(struct mdproto_cmd_flash_info_t *dst)
{
   unsigned i;
   volatile uint16_t *flash __attribute__((aligned(__alignof__(uint16_t))));

   dst->manuf_id = 0xff;
   dst->device_id = 0xff;

   for (i=0; i<sizeof(dst->cfi_query_id_data)/sizeof(dst->cfi_query_id_data[0]); i++)
      dst->cfi_query_id_data[i] = 0xff;
   for (i=0; i<sizeof(dst->sys_int_info)/sizeof(dst->sys_int_info[0]); i++)
      dst->sys_int_info[i] = 0xff;
   for (i=0; i<sizeof(dst->dev_geometry)/sizeof(dst->dev_geometry[0]); i++)
      dst->dev_geometry[i] = 0xff;

   flash = (uint16_t *)EXT_SRAM_CSN0;

   /* Read software id entry  */

   /* Software data protection  */
   flash[0x5555] = 0xaaaa;
   flash[0x2aaa] = 0x5555;

   /* command 90h - soft id entry */
   flash[0x5555] = 0x9090;

   /* Wait Tida  */
   wait(1000);

   /* Read soft id  */
   dst->manuf_id = flash[0];
   dst->device_id = flash[1];

   return 1;
}

