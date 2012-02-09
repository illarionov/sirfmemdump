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

static void flash_get_name(unsigned manufacturer_id, unsigned device_id,
      const TCHAR **manufacturer, const TCHAR **device);
int dump_flash_info(const struct mdproto_cmd_flash_info_t *data);



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

   if ( (data->cfi_id_string.q != 'Q')
	 || (data->cfi_id_string.r != 'R')
	 || (data->cfi_id_string.y != 'Y')) {
      logger_error(TEXT("Wrong CFI Query-unique string (QRY): 0x%2x 0x%2x 0x%2x\n"),
	    (unsigned)data->cfi_id_string.q,
	    (unsigned)data->cfi_id_string.r,
	    (unsigned)data->cfi_id_string.y
	    );
      return -1;
   }

   flash_get_name(ntohs(data->manuf_id), ntohs(data->device_id),
	 &manufacturer, &device_name);

   logger_info(
	 TEXT("Manufactirer: 0x%04x (%s)\n")
	 TEXT("Device ID: 0x%04x (%s)\n")
	 TEXT("Primary vendor command set code: 0x%04x\n")
	 TEXT("Address for primary algotithm extended query table: 0x%04x\n")
	 TEXT("Alternate vendor command set code: 0x%04x\n")
	 TEXT("Address for alternate algorithm extended query table: 0x%04x\n"),
	 (unsigned)ntohs(data->manuf_id), manufacturer,
	 (unsigned)ntohs(data->device_id), device_name,
	 (unsigned)ntohs(data->cfi_id_string.primary_alg_id),
	 (unsigned)ntohs(data->cfi_id_string.primary_alg_tbl),
	 (unsigned)ntohs(data->cfi_id_string.secondary_alg_id),
	 (unsigned)ntohs(data->cfi_id_string.secondary_alg_tbl)
	 );

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

   logger_info(
	 TEXT("Vcc min/max (V): %.3f / %.3f\n")
	 TEXT("Vpp min/max (V): %s\n"),
	 (double)((data->interface_info.vcc_min >> 4) & 0x0f) + (double)(data->interface_info.vcc_min & 0x0f) * 0.100,
	 (double)((data->interface_info.vcc_max >> 4) & 0x0f) + (double)(data->interface_info.vcc_max & 0x0f) * 0.100,
	 tmp
   );

   /* Word / buffer timeouts */
   if ((data->interface_info.buf_write_tmout == 0)
	 && (data->interface_info.max_buf_write_tmout == 0))
      _tcsncpy(tmp, TEXT("not supported"), sizeof(tmp)/sizeof(tmp[0])-1);
   else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.3f / %.3f"),
	 pow(2.0, (double)data->interface_info.buf_write_tmout),
	 pow(2.0, (double)data->interface_info.max_buf_write_tmout) * pow(2.0, (double)data->interface_info.buf_write_tmout)
	 );

   logger_info(
	 TEXT("Word   write timemout typical/max (us): %.0f / %.0f \n")
	 TEXT("Buffer write timemout typical/max (us): %s \n"),
	 pow(2.0, (double)data->interface_info.word_write_tmout),
	 pow(2.0, (double)data->interface_info.max_word_write_tmout) * pow(2.0, (double)data->interface_info.word_write_tmout),
	 tmp
	 );

   /* Erase timeouts */
   if ((data->interface_info.chip_erase_tmout == 0)
	 && (data->interface_info.max_chip_erase_tmout == 0))
      _tcsncpy(tmp, TEXT("not supported"), sizeof(tmp)/sizeof(tmp[0])-1);
   else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.0f / %.0f"),
	 pow(2.0, (double)data->interface_info.chip_erase_tmout),
	 pow(2.0, (double)data->interface_info.max_chip_erase_tmout) * pow(2.0, (double)data->interface_info.chip_erase_tmout)
	 );

   logger_info(
	 TEXT("Block erase timeout typical/max (ms): %.0f / %.0f \n")
	 TEXT("Chip  erase timeout typical/max (ms): %s\n"),
	 pow(2.0, (double)data->interface_info.block_erase_tmout),
	 pow(2.0, (double)data->interface_info.max_block_erase_tmout) * pow(2.0, (double)data->interface_info.block_erase_tmout),
	 tmp
   );

   if (data->flash_geometry.max_write_buf_size == 0)
      _tcsncpy(tmp, TEXT("not supported"), sizeof(tmp)/sizeof(tmp[0])-1);
   else
      _sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), TEXT("%.0f bytes"),
	    pow(2.0, (double)ntohs(data->flash_geometry.max_write_buf_size)));

   logger_info(
	 TEXT("Device size: %.0fMbit\n")
	 TEXT("Flash device interface description: 0x%04x\n")
	 TEXT("Maximum buffer size: %s\n")
	 TEXT("Number of erase sectors: %u\n"),
	 pow(2.0, (double)data->flash_geometry.size) * 8.0 / (1024.0*1024.0),
	 (unsigned)ntohs(data->flash_geometry.interface_desc),
	 tmp,
	 (unsigned)data->flash_geometry.num_erase_blocks
      );

   max_erase_block = data->flash_geometry.num_erase_blocks;
   if (max_erase_block > sizeof(data->flash_geometry.erase_blocks)/sizeof(data->flash_geometry.erase_blocks[0]))
      max_erase_block = sizeof(data->flash_geometry.erase_blocks)/sizeof(data->flash_geometry.erase_blocks[0]);

   for (i=0; i < max_erase_block; i++) {
      block = ntohl(data->flash_geometry.erase_blocks[i]);
      logger_info(
	    TEXT("Erase sector %u: %u blocks * %u bytes\n"),
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

