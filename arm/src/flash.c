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

static void flash_sdp_null_unprotect(void);
static void flash_sdp_16b_unprotect(void);

static volatile uint16_t *flash __attribute__((aligned(__alignof__(uint16_t))));
static volatile unsigned flash_bus_width = 16;
static void (*flash_sdp_unprotect)(void) = &flash_sdp_16b_unprotect;

/* sirfmemdump.c  */
void wait(unsigned n);

int flash_init(void);
int flash_get_info(struct mdproto_cmd_flash_info_t *dst);


int flash_init()
{
   uint16_t orig[2];

   /* EXT SRAM Base address */
   flash = (uint16_t *)EXT_SRAM_CSN0;

   /* Flash bus width */
   flash_bus_width = 16;

   if(gps_version == GPS3) {
      volatile uint16_t *unk88010000;
      unsigned v;

      unk88010000 = (uint16_t *)0x80010000;
      v = (*unk88010000 >> 2) & 0x03;
      if (v == 0x02)
	 flash_bus_width = 32;
   }

   if (flash_bus_width == 16) {
      orig[0] = flash[0];
      orig[1] = flash[1];

      flash[0x5555] = 0x9898;  /* CFI query */
      wait(100);               /* Wait Tida  */

      if ( ((flash[0x10] & 0xff) == 'Q')
	    && ((flash[0x11] & 0xff) == 'R')
	    && ((flash[0x12] & 0xff) == 'Y')) {
	 /* CFI device */
	 flash_sdp_unprotect = &flash_sdp_null_unprotect;
	 goto flash_16bit_done;
      }

      /* Check with SDP */
      flash_sdp_unprotect = &flash_sdp_16b_unprotect;
      flash_sdp_unprotect();
      flash[0] = 0x9898;  /* CFI query */
      wait(100);          /* Wait Tida  */

      if ( ((flash[0x10] & 0xff) == 'Q')
	    && ((flash[0x11] & 0xff) == 'R')
	    && ((flash[0x12] & 0xff) == 'Y')) {
	 /* CFI device with SDP */
	 goto flash_16bit_done;
      }

      /* JEDEC ID request */
      flash[0] = 0x9090;
      if (flash[0] == 0x90) {
	 /* SRAM device */
	 flash[0]=orig[0];
	 flash[1]=orig[1];
	 flash_sdp_unprotect = &flash_sdp_null_unprotect;
	 flash_bus_width=0;
	 return 0;
      }

      /* JEDEC flash device */

flash_16bit_done:
      flash[0] = 0xffff; /* read array mode */
      return 0;
   } /* flash_bus_width=16 */

   return flash_bus_width;
}

int flash_get_info(struct mdproto_cmd_flash_info_t *dst)
{
   unsigned i;

   dst->manuf_id = 0xff;
   dst->device_id = 0xff;

   for (i=0; i<sizeof(dst->cfi_query_id_data)/sizeof(dst->cfi_query_id_data[0]); i++)
      dst->cfi_query_id_data[i] = 0xff;
   for (i=0; i<sizeof(dst->sys_int_info)/sizeof(dst->sys_int_info[0]); i++)
      dst->sys_int_info[i] = 0xff;
   for (i=0; i<sizeof(dst->dev_geometry)/sizeof(dst->dev_geometry[0]); i++)
      dst->dev_geometry[i] = 0xff;

   /* unsupported */
   if (flash_bus_width != 16)
      return 0;

   /* JEDEC ID query */
   flash_sdp_unprotect();
   flash[0x5555] = 0x9090; /* JEDEC ID query */
   wait(100);  /* Wait Tida */

   dst->manuf_id = flash[0];
   dst->device_id = flash[1];

   /* CFI query */
   flash_sdp_unprotect();
   flash[0x5555] = 0x9898; /* CFI query */
   wait(100);  /* Wait Tida */

   for (i=0; i<sizeof(dst->cfi_query_id_data)/sizeof(dst->cfi_query_id_data[0]); i++)
      dst->cfi_query_id_data[i] = flash[0x10+i];
   for (i=0; i<sizeof(dst->sys_int_info)/sizeof(dst->sys_int_info[0]); i++)
      dst->sys_int_info[i] = flash[0x1b+i];
   for (i=0; i<sizeof(dst->dev_geometry)/sizeof(dst->dev_geometry[0]); i++)
      dst->dev_geometry[i] = flash[0x27+i];

   
   flash[0] = 0xffff; /* read array mode */

   return 1;
}




/* Software data protection - unprotect prefix */
static void flash_sdp_16b_unprotect()
{
   /* Software data protection  */
   flash[0x5555] = 0xaaaa;
   flash[0x2aaa] = 0x5555;
}

static void flash_sdp_null_unprotect()
{
   return;
}




