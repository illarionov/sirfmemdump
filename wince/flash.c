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

#include "StdAfx.h"
#include "sirfmemdump.h"
#include "mdproto.h"

const struct {
   unsigned manuf_id;
   unsigned device_id;
   TCHAR *manuf_name;
   TCHAR *dev_name;
   struct flash_erase_block_t map[FLASH_MAX_ERASE_BLOCK_NUM];
} FLASH_LIST[] = {
   {
      /* Spansion S29AL004D bottom boot block  */
      0x01, 0x22ba, TEXT("Spansion"), TEXT("AM29LV400BB"),
      { {1, 16384}, {2, 8192}, {1, 32768}, {7, 65536}, {0, 0}  }
   }
};


static struct flash_erase_block_t *flash_eblock_by_idx(struct flash_erase_block_t *map, unsigned i);
static struct flash_erase_block_t *flash_eblock_by_addr(struct flash_erase_block_t *map, unsigned addr);
static void flash_get_name(unsigned manufacturer_id, unsigned device_id,
      const TCHAR **manufacturer, const TCHAR **device);


int dump_flash_info(const struct mdproto_cmd_flash_info_t *data)
{
   unsigned block;
   unsigned i, max_erase_block;
   const TCHAR *manufacturer;
   const TCHAR *device_name;
   TCHAR tmp[80];
   assert(data);

   if ((data->manuf_id == 0xff)
	 && (data->device_id == 0xff)) {
      logger_error(TEXT("unknown flash type"));
      return 0;
   }

   flash_get_name(ntohs(data->manuf_id), ntohs(data->device_id),
	 &manufacturer, &device_name);

   logger_info(TEXT("Manufacturer: 0x%04x (%s)"),
	   (unsigned)ntohs(data->manuf_id), manufacturer);	 
   logger_info(TEXT("Device ID: 0x%04x (%s)"),
	   (unsigned)ntohs(data->device_id), device_name);

   if ( (data->cfi_id_string.q != 'Q')
	 || (data->cfi_id_string.r != 'R')
	 || (data->cfi_id_string.y != 'Y')) {
      logger_error(TEXT("Non-CFI device - wrong CFI Query-unique string (QRY): 0x%02x 0x%02x 0x%02x"),
	    (unsigned)data->cfi_id_string.q,
	    (unsigned)data->cfi_id_string.r,
	    (unsigned)data->cfi_id_string.y
	    );
      return -1;
   }

   logger_info(TEXT("Primary vendor command set code: 0x%04x"),
	   (unsigned)ntohs(data->cfi_id_string.primary_alg_id));
   logger_info(TEXT("Address for primary algotithm extended query table: 0x%04x"),
	   (unsigned)ntohs(data->cfi_id_string.primary_alg_tbl));
   logger_info(TEXT("Alternate vendor command set code: 0x%04x"),
	   (unsigned)ntohs(data->cfi_id_string.secondary_alg_id));
   logger_info(TEXT("Address for alternate algorithm extended query table: 0x%04x"),
	 (unsigned)ntohs(data->cfi_id_string.secondary_alg_tbl));

   tmp[sizeof(tmp)/sizeof(tmp[0])-1]=0;
   /* Vpp */
   if ((data->interface_info.vpp_min == 0)
	 && (data->interface_info.vpp_max == 0)) {
      _tcsncpy(tmp, TEXT("no Vpp pin present"), sizeof(tmp)/sizeof(tmp[0])-1);
   }else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.3f / %.3f"),
	 (double)((data->interface_info.vpp_min >> 4) & 0x0f) + (double)(data->interface_info.vpp_min & 0x0f) * 0.100,
	 (double)((data->interface_info.vpp_max >> 4) & 0x0f) + (double)(data->interface_info.vpp_max & 0x0f) * 0.100
	 );

   logger_info(TEXT("Vcc min/max (V): %.3f / %.3f"),
		(double)((data->interface_info.vcc_min >> 4) & 0x0f) + (double)(data->interface_info.vcc_min & 0x0f) * 0.100,
		(double)((data->interface_info.vcc_max >> 4) & 0x0f) + (double)(data->interface_info.vcc_max & 0x0f) * 0.100
	);
   logger_info(TEXT("Vpp min/max (V): %s"),tmp);

   /* Word / buffer timeouts */
   if ((data->interface_info.buf_write_tmout == 0)
	 && (data->interface_info.max_buf_write_tmout == 0))
      _tcsncpy(tmp, TEXT("not supported"), sizeof(tmp)/sizeof(tmp[0])-1);
   else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.3f / %.3f"),
	 pow(2.0, (double)data->interface_info.buf_write_tmout),
	 pow(2.0, (double)data->interface_info.max_buf_write_tmout) * pow(2.0, (double)data->interface_info.buf_write_tmout)
	 );

   logger_info(TEXT("Word write timemout typical/max (us): %.0f / %.0f"),
	 	 pow(2.0, (double)data->interface_info.word_write_tmout),
		 pow(2.0, (double)data->interface_info.max_word_write_tmout) * pow(2.0, (double)data->interface_info.word_write_tmout));

   logger_info(TEXT("Buffer write timemout typical/max (us): %s"), tmp);

   /* Erase timeouts */
   if ((data->interface_info.chip_erase_tmout == 0)
	 && (data->interface_info.max_chip_erase_tmout == 0))
      _tcsncpy(tmp, TEXT("not supported"), sizeof(tmp)/sizeof(tmp[0])-1);
   else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.0f / %.0f"),
	 pow(2.0, (double)data->interface_info.chip_erase_tmout),
	 pow(2.0, (double)data->interface_info.max_chip_erase_tmout) * pow(2.0, (double)data->interface_info.chip_erase_tmout)
	 );

   logger_info(TEXT("Block erase timeout typical/max (ms): %.0f / %.0f"),
	   pow(2.0, (double)data->interface_info.block_erase_tmout),
	 pow(2.0, (double)data->interface_info.max_block_erase_tmout) * pow(2.0, (double)data->interface_info.block_erase_tmout));
	
   logger_info(TEXT("Chip  erase timeout typical/max (ms): %s"), tmp);

   if (data->flash_geometry.max_write_buf_size == 0)
      _tcsncpy(tmp, TEXT("not supported"), sizeof(tmp)/sizeof(tmp[0])-1);
   else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.0f bytes"),
	    pow(2.0, (double)ntohs(data->flash_geometry.max_write_buf_size)));

   logger_info(TEXT("Device size: %.0fMbit"),
	 pow(2.0, (double)data->flash_geometry.size) * 8.0 / (1024.0*1024.0));
   logger_info(TEXT("Flash device interface description: 0x%04x"),
	   (unsigned)ntohs(data->flash_geometry.interface_desc));
   logger_info(TEXT("Maximum buffer size: %s"),tmp);
   logger_info(TEXT("Number of erase sectors: %u"),
	   (unsigned)data->flash_geometry.num_erase_blocks);

   max_erase_block = data->flash_geometry.num_erase_blocks;
   if (max_erase_block > sizeof(data->flash_geometry.erase_blocks)/sizeof(data->flash_geometry.erase_blocks[0]))
      max_erase_block = sizeof(data->flash_geometry.erase_blocks)/sizeof(data->flash_geometry.erase_blocks[0]);

   for (i=0; i < max_erase_block; i++) {
      block = ntohl(data->flash_geometry.erase_blocks[i]);
      logger_info(
	    TEXT("Erase sector %u: %u blocks * %u bytes"),
	    i,
	    (block & 0xffff)+1,
	    ( ((block >> 16) & 0xffff) ? ((block >> 16) & 0xffff) * 256 : 128)
      );
   }

   return 1;
}


static void flash_get_name(unsigned manufacturer_id, unsigned device_id,
						   const TCHAR **manufacturer, const TCHAR **device)
{
	assert(manufacturer);
	assert(device);

	*manufacturer = TEXT("Unknown");
	*device = TEXT("Unknown");

	switch (manufacturer_id) {
	  case 0x01:
		  *manufacturer = TEXT("AMD");
		  switch (device_id) {
				case 0x22b9:
					*device = TEXT("AM29LV400BT"); /* Spansion S29AL004D top boot block */
					break;
				case 0x22ba:
					*device = TEXT("AM29LV400BB"); /* Spansion S29AL004D bottom boot block */
					break;
				default: break;
		  }
		  break;
	  case 0x04:
		  *manufacturer = TEXT("Fujitsu");
		  switch (device_id) {
	  default: break;
		  }
	  case 0x37:
		  *manufacturer = TEXT("Amic");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0x16f:
		  *manufacturer = TEXT("Atmel");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0x7f:
		  *manufacturer = TEXT("EON");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0x89:
		  *manufacturer = TEXT("Intel");
		  switch (device_id) {
	  case 0x8890: *device =TEXT("28F160B3T"); break;
	  case 0x8891: *device =TEXT("28F160B3B"); break;
	  case 0x8892: *device =TEXT("28F800B3T"); break;
	  case 0x8893: *device =TEXT("28F800B3B"); break;
	  case 0x88c0: *device =TEXT("28F800C3T"); break;
	  case 0x88c1: *device =TEXT("28F800C3B"); break;
	  case 0x88c2: *device =TEXT("28F160C3T"); break;
	  case 0x88c3: *device =TEXT("28F160C3B"); break;
	  default: break;
		  }
		  break;
	  case 0xc2:
	  case 0x1c:
		  *manufacturer = TEXT("Macronix");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0x62:
		  *manufacturer = TEXT("Sanyo");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0xb0:
		  *manufacturer = TEXT("Sharp");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0xbf:
		  *manufacturer = TEXT("SST");
		  switch (device_id) {
	  case 0x234a: *device = TEXT("SST39VF1602"); break;
	  case 0x234b: *device = TEXT("SST39VF1601"); break;
	  case 0x272f: *device = TEXT("SST39WF400A"); break;
	  case 0x273f: *device = TEXT("SST39WF800A"); break;
	  case 0x2780: *device = TEXT("SST39VF400A"); break;
	  case 0x2781: *device = TEXT("SST39VF800"); break;
	  case 0x2782: *device = TEXT("SST39VF160"); break;
	  default: break;
		  }
		  break;
	  case 0x20:
		  *manufacturer = TEXT("ST");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  case 0x98:
		  *manufacturer = TEXT("Toshiba");
		  switch (device_id) {
	  default: break;
		  }
		  break;
	  default:
		  break;
	}

}

int flash_get_eblock_map(struct mdproto_cmd_flash_info_t *flash_info,
      struct flash_erase_block_t *res)
{
   unsigned i;
   unsigned flash_size, cur_size;
   unsigned cur_row;
   unsigned max_erase_block;
   unsigned block_val;

   assert(flash_info);
   assert(res);

   for (i=0; i<sizeof(FLASH_LIST)/sizeof(FLASH_LIST[0]); i++) {
      if ( (FLASH_LIST[i].manuf_id == htons(flash_info->manuf_id))
	    && (FLASH_LIST[i].device_id == htons(flash_info->device_id))) {
	 memcpy(res, FLASH_LIST[i].map, sizeof(FLASH_LIST[i].map));
	 return 0;
      }
   }

   if ( (flash_info->cfi_id_string.q != 'Q')
	 || (flash_info->cfi_id_string.r != 'R')
	 || (flash_info->cfi_id_string.y != 'Y')) {
      return -1;
   }

   cur_size=0;

   /* XXX  */
   if (flash_info->flash_geometry.size >= 32)
      return -1;

   /* Read flash geometry  */
   flash_size = 1 << flash_info->flash_geometry.size;
   cur_row=0;
   max_erase_block = flash_info->flash_geometry.num_erase_blocks;
   if (max_erase_block > sizeof(flash_info->flash_geometry.erase_blocks)/sizeof(flash_info->flash_geometry.erase_blocks[0]))
      max_erase_block = sizeof(flash_info->flash_geometry.erase_blocks)/sizeof(flash_info->flash_geometry.erase_blocks[0]);
   if (max_erase_block > FLASH_MAX_ERASE_BLOCK_NUM)
      max_erase_block = FLASH_MAX_ERASE_BLOCK_NUM;

   for(i=0; i<max_erase_block; ++i) {
      block_val = htonl(flash_info->flash_geometry.erase_blocks[i]);
      res[cur_row].blocks = (block_val & 0xffff)+1;
      res[cur_row].bytes = 256*((block_val >> 16) & 0xffff);
      if (res[cur_row].bytes == 0)
	 res[cur_row].bytes = 128;
      cur_size +=  res[cur_row].blocks * res[cur_row].bytes;
      ++cur_row;
      if (cur_size == flash_size) {
	 res[cur_row].bytes = res[cur_row].blocks = 0;
	 return 0;
      }else if (cur_size > flash_size)
	 break;
   }

   logger_error(TEXT("flash_get_eblock_map: incorrect sector map. ")
	 TEXT("Current summary sector size: %u. Flash size: %u\n"), cur_size, flash_size);

   return -1;
}

unsigned flash_max_eblock_size(struct flash_erase_block_t *map)
{
   unsigned max_size;

   max_size=0;
   for(; map->blocks != 0; ++map) {
      if (map->bytes > max_size)
	 max_size = map->bytes;
   }

   return max_size;
}

unsigned flash_size_from_emap(struct flash_erase_block_t *map)
{
   unsigned size;

   size=0;
   for(; map->blocks != 0; map++)
      size += map->blocks * map->bytes;

   return size;
}

static struct flash_erase_block_t *flash_eblock_by_idx(struct flash_erase_block_t *map, unsigned i)
{
   unsigned nextb_first_idx;

   nextb_first_idx=0;
   while (map->blocks != 0) {
      nextb_first_idx += map->blocks;
      if (nextb_first_idx > i)
	 break;
      ++map;
   }

   return map;
}

static struct flash_erase_block_t *flash_eblock_by_addr(struct flash_erase_block_t *map, unsigned addr)
{
   unsigned nextb_first_addr;

   nextb_first_addr=0;
   while (map->blocks != 0) {
      nextb_first_addr += map->bytes * map->blocks;
      if (nextb_first_addr > addr)
	 break;
      ++map;
   }

   return map;
}
