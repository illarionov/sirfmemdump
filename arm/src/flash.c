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
static void flash_16b_cfi_query(void);
static void flash_16b_jedec_id_query(void);
static void flash_16b_read_array_mode(void);

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
      volatile uint16_t *unk80010000;
      unsigned v;

      unk80010000 = (uint16_t *)0x80010000;
      v = (*unk80010000 >> 2) & 0x03;
      if (v == 0x02)
	 flash_bus_width = 32;
   }

   if (flash_bus_width == 16) {
      orig[0] = flash[0];
      orig[1] = flash[1];

      flash_sdp_unprotect = &flash_sdp_null_unprotect;
      flash_16b_cfi_query();
      if ( ((flash[0x10] & 0xff) == 'Q')
	    && ((flash[0x11] & 0xff) == 'R')
	    && ((flash[0x12] & 0xff) == 'Y')) {
	 /* CFI device */
	 goto flash_16bit_done;
      }

      /* Check with SDP */
      flash_sdp_unprotect = &flash_sdp_16b_unprotect;
      flash_16b_cfi_query();

      if ( ((flash[0x10] & 0xff) == 'Q')
	    && ((flash[0x11] & 0xff) == 'R')
	    && ((flash[0x12] & 0xff) == 'Y')) {
	 /* CFI device with SDP */
	 goto flash_16bit_done;
      }

      /* JEDEC ID request */
      flash_sdp_unprotect = &flash_sdp_null_unprotect;
      flash_16b_jedec_id_query();
      if (flash[0] == 0x90) {
	 /* SRAM device */
	 flash[0]=orig[0];
	 flash[1]=orig[1];
	 flash_bus_width=0;
	 return 0;
      }

      /* JEDEC flash device */

flash_16bit_done:
      flash_16b_read_array_mode();
      return 0;
   } /* flash_bus_width=16 */

   return flash_bus_width;
}

int flash_get_info(struct mdproto_cmd_flash_info_t *dst)
{
   unsigned i, max_erase_block, eblock_addr;

   dst->manuf_id = 0xffff;
   dst->device_id = 0xffff;

   dst->cfi_id_string.q = 0xff;
   dst->cfi_id_string.r = 0xff;
   dst->cfi_id_string.y = 0xff;
   dst->cfi_id_string.primary_alg_id = 0;
   dst->cfi_id_string.primary_alg_tbl = 0;
   dst->cfi_id_string.secondary_alg_id = 0;
   dst->cfi_id_string.secondary_alg_tbl = 0;

   dst->interface_info.vcc_min = 0;
   dst->interface_info.vcc_max = 0;
   dst->interface_info.vpp_min = 0;
   dst->interface_info.vpp_max = 0;
   dst->interface_info.word_write_tmout  = 0;
   dst->interface_info.buf_write_tmout   = 0;
   dst->interface_info.block_erase_tmout = 0;
   dst->interface_info.chip_erase_tmout  = 0;
   dst->interface_info.max_word_write_tmout  = 0;
   dst->interface_info.max_buf_write_tmout   = 0;
   dst->interface_info.max_block_erase_tmout = 0;
   dst->interface_info.max_chip_erase_tmout  = 0;

   dst->flash_geometry.size = 0;
   dst->flash_geometry.interface_desc = 0;
   dst->flash_geometry.max_write_buf_size = 0;
   dst->flash_geometry.num_erase_blocks = 0;
   for(i=0; i<sizeof(dst->flash_geometry.erase_blocks)/sizeof(dst->flash_geometry.erase_blocks[0]);i++)
      dst->flash_geometry.erase_blocks[i]=0;

   /* unsupported */
   if (flash_bus_width != 16)
      return 0;

   flash_16b_cfi_query();

   flash_16b_jedec_id_query();

   dst->manuf_id = sirfgps_htons(flash[0]);
   dst->device_id = sirfgps_htons(flash[1]);


/* Result is in network byte order */
#define get_uint8(_a) (flash[_a]&0xff)
#define get_uint16(_a) (\
	 ((uint16_t)(flash[(_a)+0] & 0xff) << 8) \
	 | (uint16_t)(flash[(_a)+1] & 0xff) \
	 )
#define get_uint32(_a) ( \
	 ((uint32_t)(flash[(_a)+0] & 0xff) << 24) \
	 | ((uint32_t)(flash[(_a)+1] & 0xff) << 16) \
	 | ((uint32_t)(flash[(_a)+2] & 0xff) << 8) \
	 | (uint32_t)(flash[(_a)+3] & 0xff) \
	 )

   dst->cfi_id_string.q = get_uint8(0x10);
   dst->cfi_id_string.r = get_uint8(0x11);
   dst->cfi_id_string.y = get_uint8(0x12);
   dst->cfi_id_string.primary_alg_id = get_uint16(0x13);
   dst->cfi_id_string.primary_alg_tbl = get_uint16(0x15);
   dst->cfi_id_string.secondary_alg_id = get_uint16(0x17);
   dst->cfi_id_string.secondary_alg_tbl = get_uint16(0x17);

   dst->interface_info.vcc_min = get_uint8(0x1b);
   dst->interface_info.vcc_max = get_uint8(0x1c);
   dst->interface_info.vpp_min = get_uint8(0x1d);
   dst->interface_info.vpp_max = get_uint8(0x1e);
   dst->interface_info.word_write_tmout  = get_uint8(0x1f);
   dst->interface_info.buf_write_tmout   = get_uint8(0x20);
   dst->interface_info.block_erase_tmout = get_uint8(0x21);
   dst->interface_info.chip_erase_tmout  = get_uint8(0x22);
   dst->interface_info.max_word_write_tmout  = get_uint8(0x23);
   dst->interface_info.max_buf_write_tmout   = get_uint8(0x24);
   dst->interface_info.max_block_erase_tmout = get_uint8(0x25);
   dst->interface_info.max_chip_erase_tmout  = get_uint8(0x26);

   dst->flash_geometry.size = get_uint8(0x27);
   dst->flash_geometry.interface_desc = get_uint16(0x28);
   dst->flash_geometry.max_write_buf_size = get_uint16(0x2a);
   dst->flash_geometry.num_erase_blocks = get_uint8(0x2c);

   max_erase_block = dst->flash_geometry.num_erase_blocks;
   if (max_erase_block > sizeof(dst->flash_geometry.erase_blocks)/sizeof(dst->flash_geometry.erase_blocks[0]))
      max_erase_block = sizeof(dst->flash_geometry.erase_blocks)/sizeof(dst->flash_geometry.erase_blocks[0]);

   for (i=0, eblock_addr=0x2d; i < dst->flash_geometry.size; i++) {
      dst->flash_geometry.erase_blocks[i]=get_uint32(eblock_addr);
      eblock_addr += 4;
   }

#undef get_uint8
#undef get_uint16
#undef get_uint32

   flash_16b_read_array_mode();

   return 1;
}

/* 98h CFI query */
static void flash_16b_cfi_query(void)
{
   flash_sdp_unprotect();
   flash[0x5555] = 0x9898;  /* CFI query */
   wait(500);          /* Wait Tida  */
}

/* 90h Soft ID query */
static void flash_16b_jedec_id_query(void)
{
   flash_sdp_unprotect();
   flash[0] = 0x9090; /* Soft ID query */
   wait(500);  /* Wait Tida */
}

/* Software ID exit / CFI exit */
static void flash_16b_read_array_mode(void)
{
   flash_sdp_unprotect();
   flash[0x5555] = 0xf0f0; /* read array mode */
   wait(500);  /* Wait Tida */
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




