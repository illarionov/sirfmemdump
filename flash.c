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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "flashutils.h"
#include "arm/include/mdproto.h"

#define FLASH_MAX_ERASE_BLOCK_NUM 10

/* XXX */
#define EXT_SRAM_CSN0 0x40000000

struct flash_erase_block_t {
   unsigned blocks;
   unsigned bytes;
};

const struct {
   unsigned manuf_id;
   unsigned device_id;
   const char *manuf_name;
   const char *dev_name;
   struct flash_erase_block_t map[FLASH_MAX_ERASE_BLOCK_NUM];
} FLASH_LIST[] = {
   {
      /* Spansion S29AL004D bottom boot block  */
      0x22b9, 0x22ba, "Spansion", "AM29LV400BB",
      { {1, 16384}, {2, 8192}, {1, 32768}, {7, 65536}, {0, 0}  }
   }//,
//   {
      /* SST SST39VF1601 */
//      0xbf, 0x234b, "SST", "SST39VF1601",
//      { {512, 4096}, {0,0} }
//   }
};

static int flash_get_eblock_map(struct mdproto_cmd_flash_info_t *flash_info,
      struct flash_erase_block_t *res);
static unsigned flash_size_from_emap(struct flash_erase_block_t *map);
static unsigned flash_max_eblock_size(struct flash_erase_block_t *map);
static struct flash_erase_block_t *flash_eblock_by_idx(struct flash_erase_block_t *map, unsigned i);
static struct flash_erase_block_t *flash_eblock_by_addr(struct flash_erase_block_t *map, unsigned addr);

static int dump_mem(int pfd, unsigned src_addr, unsigned size, uint8_t *res);
static int program_sector(int pfd, unsigned addr, uint8_t *data, unsigned data_size);


void flash_get_name(unsigned manufacturer_id, unsigned device_id,
      const char **manufacturer, const char **device)
{
   assert(manufacturer);
   assert(device);

   *manufacturer = "Unknown";
   *device = "Unknown";

   switch (manufacturer_id) {
      case 0x01:
	 *manufacturer = "AMD";
	 switch (device_id) {
	    case 0x22b9:
	       *device = "AM29LV400BT"; /*  Spansion S29AL004D top boot block */
	       break;
	    case 0x22ba:
	       *device = "AM29LV400BB"; /*  Spansion S29AL004D bottom boot block */
	       break;
	    default:
	       break;
	 }
	 break;
      case 0x04:
	 *manufacturer = "Fujitsu";
	 switch (device_id) {
	    default: break;
	 }
      case 0x37:
	 *manufacturer = "Amic";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0x16f:
	 *manufacturer = "Atmel";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0x7f:
	 *manufacturer = "EON";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0x89:
	 *manufacturer = "Intel";
	 switch (device_id) {
	    case 0x8890: *device ="28F160B3T"; break;
	    case 0x8891: *device ="28F160B3B"; break;
	    case 0x8892: *device ="28F800B3T"; break;
	    case 0x8893: *device ="28F800B3B"; break;
	    case 0x88c0: *device ="28F800C3T"; break;
	    case 0x88c1: *device ="28F800C3B"; break;
	    case 0x88c2: *device ="28F160C3T"; break;
	    case 0x88c3: *device ="28F160C3B"; break;
	    default: break;
	 }
	 break;
      case 0xc2:
      case 0x1c:
	 *manufacturer = "Macronix";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0x62:
	 *manufacturer = "Sanyo";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0xb0:
	 *manufacturer = "Sharp";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0xbf:
	 *manufacturer = "SST";
	 switch (device_id) {
	    case 0x234a: *device = "SST39VF1602"; break;
	    case 0x234b: *device = "SST39VF1601"; break;
	    case 0x272f: *device = "SST39WF400A"; break;
	    case 0x273f: *device = "SST39WF800A"; break;
	    case 0x2780: *device = "SST39VF400A"; break;
	    case 0x2781: *device = "SST39VF800"; break;
	    case 0x2782: *device = "SST39VF160"; break;
	    default: break;
	 }
	 break;
      case 0x20:
	 *manufacturer = "ST";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      case 0x98:
	 *manufacturer = "Toshiba";
	 switch (device_id) {
	    default: break;
	 }
	 break;
      default:
	 break;
   }

}

int dump_flash_info(const struct mdproto_cmd_flash_info_t *data)
{
   unsigned block;
   unsigned i, max_erase_block;
   const char *manufacturer;
   const char *device_name;
   char tmp[80];
   assert(data);

   if ((data->manuf_id == 0xff)
	 && (data->device_id == 0xff)) {
      gpsd_report(LOG_PROG, "unknown flash type\n");
      return 0;
   }

   flash_get_name(ntohs(data->manuf_id), ntohs(data->device_id),
	 &manufacturer, &device_name);

   printf(
	 "Manufacturer: 0x%04x (%s)\n"
	 "Device ID: 0x%04x (%s)\n",
	 (unsigned)ntohs(data->manuf_id), manufacturer,
	 (unsigned)ntohs(data->device_id), device_name
   );

   if ( (data->cfi_id_string.q != 'Q')
	 || (data->cfi_id_string.r != 'R')
	 || (data->cfi_id_string.y != 'Y')) {
      gpsd_report(LOG_PROG, "Non-CFI device or wrong CFI Query-unique string (QRY): 0x%2x 0x%2x 0x%2x\n",
	    data->cfi_id_string.q,
	    data->cfi_id_string.r,
	    data->cfi_id_string.y
	    );
      return -1;
   }

   printf(
	 "Primary vendor command set code: 0x%04x\n"
	 "Address for primary algotithm extended query table: 0x%04x\n"
	 "Alternate vendor command set code: 0x%04x\n"
	 "Address for alternate algorithm extended query table: 0x%04x\n",
	 (unsigned)ntohs(data->cfi_id_string.primary_alg_id),
	 (unsigned)ntohs(data->cfi_id_string.primary_alg_tbl),
	 (unsigned)ntohs(data->cfi_id_string.secondary_alg_id),
	 (unsigned)ntohs(data->cfi_id_string.secondary_alg_tbl)
	 );

   tmp[sizeof(tmp)-1]='\0';
   /* Vpp */
   if ((data->interface_info.vpp_min == 0)
	 && (data->interface_info.vpp_max == 0)) {
      strncpy(tmp, "no Vpp pin present", sizeof(tmp)-1);
   }else
      snprintf(tmp, sizeof(tmp), "%.3f / %.3f",
	 (double)((data->interface_info.vpp_min >> 4) & 0x0f) + (double)(data->interface_info.vpp_min & 0x0f) * 0.100,
	 (double)((data->interface_info.vpp_max >> 4) & 0x0f) + (double)(data->interface_info.vpp_max & 0x0f) * 0.100
	 );

   printf(
	 "Vcc min/max (V): %.3f / %.3f\n"
	 "Vpp min/max (V): %s\n",
	 (double)((data->interface_info.vcc_min >> 4) & 0x0f) + (double)(data->interface_info.vcc_min & 0x0f) * 0.100,
	 (double)((data->interface_info.vcc_max >> 4) & 0x0f) + (double)(data->interface_info.vcc_max & 0x0f) * 0.100,
	 tmp
   );

   /* Word / buffer timeouts */
   if ((data->interface_info.buf_write_tmout == 0)
	 && (data->interface_info.max_buf_write_tmout == 0))
      strncpy(tmp, "not supported", sizeof(tmp)-1);
   else
      snprintf(tmp, sizeof(tmp), "%.3f / %.3f",
	 pow(2.0, (double)data->interface_info.buf_write_tmout),
	 pow(2.0, (double)data->interface_info.max_buf_write_tmout) * pow(2.0, (double)data->interface_info.buf_write_tmout)
	 );

   printf(
	 "Word   write timemout typical/max (us): %.0f / %.0f \n"
	 "Buffer write timemout typical/max (us): %s \n",
	 pow(2.0, (double)data->interface_info.word_write_tmout),
	 pow(2.0, (double)data->interface_info.max_word_write_tmout) * pow(2.0, (double)data->interface_info.word_write_tmout),
	 tmp
	 );

   /* Erase timeouts */
   if ((data->interface_info.chip_erase_tmout == 0)
	 && (data->interface_info.max_chip_erase_tmout == 0))
      strncpy(tmp, "not supported", sizeof(tmp)-1);
   else
      snprintf(tmp, sizeof(tmp), "%.0f / %.0f",
	 pow(2.0, (double)data->interface_info.chip_erase_tmout),
	 pow(2.0, (double)data->interface_info.max_chip_erase_tmout) * pow(2.0, (double)data->interface_info.chip_erase_tmout)
	 );

   printf(
	 "Block erase timeout typical/max (ms): %.0f / %.0f \n"
	 "Chip  erase timeout typical/max (ms): %s\n",
	 pow(2.0, (double)data->interface_info.block_erase_tmout),
	 pow(2.0, (double)data->interface_info.max_block_erase_tmout) * pow(2.0, (double)data->interface_info.block_erase_tmout),
	 tmp
   );

   if (data->flash_geometry.max_write_buf_size == 0)
      strncpy(tmp, "not supported", sizeof(tmp)-1);
   else
      snprintf(tmp, sizeof(tmp), "%.0f bytes",
	    pow(2.0, (double)ntohs(data->flash_geometry.max_write_buf_size)));

   printf(
	 "Device size: %.0fMbit\n"
	 "Flash device interface description: 0x%04x\n"
	 "Maximum buffer size: %s\n"
	 "Number of erase sectors: %u\n",
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
      printf(
	    "Erase sector %u: %u blocks * %u bytes\n",
	    i,
	    (block & 0xffff)+1,
	    ( ((block >> 16) & 0xffff) ? ((block >> 16) & 0xffff) * 256 : 128)
      );
   }

   return 1;
}

static int get_flash_info(int pfd, struct mdproto_cmd_flash_info_t *res)
{
  int write_size;
  unsigned read_status;
  struct mdproto_cmd_buf_t cmd;

  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_FLASH_INFO, NULL, 0);
  gpsd_report(LOG_PROG, "FLASH-INFO...\n");

  tcflush(pfd, TCIOFLUSH);
  usleep(10000);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return -1;
  }

  read_status = read_mdproto_pkt(pfd, &cmd);
  if (read_status != MDPROTO_STATUS_OK) {
     gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
     return -1;
  }

  if (cmd.data.id != MDPROTO_CMD_FLASH_INFO_RESPONSE) {
     gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
     return -1;
  }

  if (ntohs(cmd.size) != sizeof(struct mdproto_cmd_flash_info_t)+1) {
     gpsd_report(LOG_PROG, "received wrong response size `0x%x`\n", ntohs(cmd.size));
     return -1;
  }

  memcpy(res, &cmd.data.p[1], sizeof(*res));

  return 0;
}

static int dump_mem(int pfd, unsigned src_addr, unsigned size, uint8_t *res)
{
  unsigned read_status;
  int write_size;
  int cur_size;
  unsigned dst_addr;
  struct mdproto_cmd_buf_t cmd;
  struct {
     uint32_t src;
     uint32_t dst;
  } __attribute__((packed)) req;


  dst_addr = src_addr+size-1;
  req.src = htonl(src_addr);
  req.dst = htonl(dst_addr);
  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_MEM_READ, &req, sizeof(req));

  tcflush(pfd, TCIOFLUSH);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return 1;
  }

  while (src_addr <= dst_addr) {
     read_status = read_mdproto_pkt(pfd, &cmd);
     if (read_status != MDPROTO_STATUS_OK) {
	gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
	return 1;
     }
     if (cmd.data.id != MDPROTO_CMD_MEM_READ_RESPONSE) {
	gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
	return 1;
     }
     cur_size = ntohs(cmd.size) - 1;
     if (src_addr+cur_size > dst_addr)
	cur_size = dst_addr - src_addr + 1;
     memcpy(res, &cmd.data.p[1], cur_size);
     res += cur_size;
     src_addr += cur_size;
  }

  return 0;
}


int cmd_flash_info(int pfd)
{
  struct mdproto_cmd_flash_info_t flash_info;

  if (get_flash_info(pfd, &flash_info) != 0)
     return 1;

  dump_flash_info(&flash_info);

  return 0;
}

int cmd_erase_sector(int pfd, unsigned addr)
{
  unsigned read_status;
  int write_size;
  int8_t res;
  uint32_t addr_ui32;
  struct mdproto_cmd_buf_t cmd;

  addr_ui32 = ntohl((uint32_t)addr);
  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_FLASH_ERASE_SECTOR, &addr_ui32, sizeof(addr_ui32));
  gpsd_report(LOG_PROG, "FLASH-ERASE 0x%x...\n", addr);

  tcflush(pfd, TCIOFLUSH);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return 1;
  }

  read_status = read_mdproto_pkt(pfd, &cmd);
  if (read_status != MDPROTO_STATUS_OK) {
     gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
     return 1;
  }

  if (cmd.data.id != MDPROTO_CMD_FLASH_ERASE_SECTOR_RESPONSE) {
     gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
     return 1;
  }

  if (ntohs(cmd.size) != 1+1) {
     gpsd_report(LOG_PROG, "received wrong response size `0x%x`\n", ntohs(cmd.size));
     return 1;
  }

  res = (int8_t)cmd.data.p[1];
  if (res==0) {
     gpsd_report(LOG_PROG, "OK\n");
  }else {
     gpsd_report(LOG_PROG, "error %i\n", (int)res);
  }

  return (int)res;
}

static int program_sector(int pfd, unsigned addr, uint8_t *data, unsigned data_size)
{
  int res;
  int write_size;
  int read_status;
  unsigned chunk_size;
  struct {
     uint32_t addr;
     uint8_t payload[MDPROTO_CMD_MAX_RAW_DATA_SIZE-4];
  } __packed t_req;
  struct mdproto_cmd_buf_t cmd;

  assert((sizeof(t_req.payload) % 4) == 0);
  assert(sizeof(t_req.payload) >= 4);

  res = cmd_erase_sector(pfd, addr);
  if (res != 0)
     return res;

  while (data_size != 0) {

     t_req.addr = ntohl((uint32_t)addr);

     if (data_size >= sizeof(t_req.payload)) {
	chunk_size = sizeof(t_req.payload);
	memcpy(t_req.payload, data, chunk_size);
	gpsd_report(LOG_PROG, "programming 0x%08x: %u bytes\n", addr, chunk_size);

	write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_FLASH_PROGRAM,
	      &t_req, sizeof(t_req));
	data_size -= chunk_size;
	addr += chunk_size;
	data += chunk_size;
     }else {
	chunk_size = data_size;
	memcpy(t_req.payload, data, chunk_size);
	gpsd_report(LOG_PROG, "programming 0x%08x: %u bytes\n", addr, chunk_size);

	addr += chunk_size;
	data_size = 0;

	if (chunk_size % 2)
	   t_req.payload[chunk_size++] = 0xff;
	write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_FLASH_PROGRAM,
	      &t_req, chunk_size+4);
     }


     tcflush(pfd, TCIOFLUSH);
     if (write(pfd, (void *)&cmd, write_size) < write_size) {
	gpsd_report(LOG_PROG, "write() error\n");
	return 1;
     }

     read_status = read_mdproto_pkt(pfd, &cmd);
     if (read_status != MDPROTO_STATUS_OK) {
	gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
	return 1;
     }

     if (cmd.data.id != MDPROTO_CMD_FLASH_PROGRAM_RESPONSE) {
	gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
	return 1;
     }

     if (ntohs(cmd.size) != 1+1) {
	gpsd_report(LOG_PROG, "received wrong response size `0x%x`\n", ntohs(cmd.size));
	return 1;
     }

     res = (int8_t)cmd.data.p[1];
     if (res != 0) {
	gpsd_report(LOG_PROG, "error %i\n", (int)res);
	return 1;
     }
  }


  return 0;
}

int cmd_program_word(int pfd, unsigned addr, uint16_t word)
{
  int8_t res;
  int write_size;
  int read_status;
  struct {
     uint32_t addr;
     uint16_t payload;
  } __packed t_req;
  struct mdproto_cmd_buf_t cmd;

  gpsd_report(LOG_PROG, "FLASH-PROGRAM 0x%x = 0x%04x...\n", addr, (unsigned)word);

  t_req.addr = ntohl((uint32_t)addr);
  t_req.payload = htons(word);
  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_FLASH_PROGRAM,
   &t_req, sizeof(t_req));

  usleep(10000);
  tcflush(pfd, TCIOFLUSH);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return 1;
  }

  read_status = read_mdproto_pkt(pfd, &cmd);
  if (read_status != MDPROTO_STATUS_OK) {
     gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
     return 1;
  }

  if (cmd.data.id != MDPROTO_CMD_FLASH_PROGRAM_RESPONSE) {
     gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
     return 1;
  }

  if (ntohs(cmd.size) != 1+1) {
     gpsd_report(LOG_PROG, "received wrong response size `0x%x`\n", ntohs(cmd.size));
     return 1;
  }

  res = (int8_t)cmd.data.p[1];
  if (res==0) {
     gpsd_report(LOG_PROG, "OK\n");
  }else {
     gpsd_report(LOG_PROG, "error %i\n", (int)res);
  }

  return (int)res;
}


int cmd_program_flash(int pfd, const char *prom_fname)
{
  int res;
  int prom_fd;
  uint8_t *flash_sector, *file_sector;
  struct flash_erase_block_t *eblock;
  unsigned eblock_num, eblock_addr;
  unsigned sector_size, max_sector_size;
  off_t prom_file_size;
  ssize_t read_size;
  struct mdproto_cmd_flash_info_t flash_info;
  struct flash_erase_block_t sector_map[FLASH_MAX_ERASE_BLOCK_NUM];

  res = -1;
  flash_sector = file_sector = NULL;
  prom_fd = open(prom_fname, O_RDONLY);
  if (prom_fd < 0) {
     perror(NULL);
     return res;
  }

  prom_file_size=lseek(prom_fd, SEEK_END, 0);
  if (prom_file_size == -1) {
     perror("Can't determine firmware file size");
     goto cmd_program_flash_exit;
  }

  if (lseek(prom_fd, SEEK_SET, 0) < 0) {
     perror("lseek() error");
     goto cmd_program_flash_exit;
  }

  if (get_flash_info(pfd, &flash_info) != 0)
     goto cmd_program_flash_exit;

  if (flash_get_eblock_map(&flash_info, sector_map) < 0) {
     gpsd_report(LOG_PROG, "No sector map\n");
     goto cmd_program_flash_exit;
  }
  if ((sector_map[0].blocks) == 0 || (sector_map[0].bytes == 0)) {
     gpsd_report(LOG_PROG, "Wrong sector map\n");
     goto cmd_program_flash_exit;
  }

  if (flash_size_from_emap(sector_map) < prom_file_size) {
     gpsd_report(LOG_PROG, "firmware size larger (%lu) than flash size (%lu)\n",
	   (unsigned long)prom_file_size,
	   (unsigned long)flash_size_from_emap(sector_map)
	   );
     goto cmd_program_flash_exit;
  }

  max_sector_size = flash_max_eblock_size(sector_map);
  assert(max_sector_size > 0);

  flash_sector = (uint8_t *)malloc(max_sector_size);
  file_sector = (uint8_t *)malloc(max_sector_size);

  if ((flash_sector == NULL) || (file_sector == NULL))
     goto cmd_program_flash_exit;

  eblock = &sector_map[0];
  eblock_num=0;
  eblock_addr=0;
  while(1) {
     sector_size = eblock->bytes;
     assert(sector_size <= max_sector_size);

     /* Read sector from firmware file  */
     read_size = read(prom_fd, file_sector, sector_size);
     if (read_size < 0) {
	perror(NULL);
	goto cmd_program_flash_exit;
     }else if (read_size == 0)
	break;

     gpsd_report(LOG_PROG, "0x%08x: sector_size: %u bytes\n", eblock_addr, sector_size);

     /* Read sector from flash  */
     if (dump_mem(pfd, EXT_SRAM_CSN0+eblock_addr, sector_size, flash_sector) != 0) {
	gpsd_report(LOG_PROG, "Can't dump flash. Address: %u size: %u\n", eblock_addr, sector_size);
	goto cmd_program_flash_exit;
     }

     if ((unsigned)read_size < sector_size)
	memcpy(&file_sector[read_size], &flash_sector[read_size], sector_size-read_size);

     if (memcmp(file_sector, flash_sector, read_size) == 0) {
	gpsd_report(LOG_PROG, "Match.\n");
     }else {
	gpsd_report(LOG_PROG, "Reprogramming sector...\n");
	if (program_sector(pfd, eblock_addr, file_sector, sector_size) != 0)
	   goto cmd_program_flash_exit;
     }

     if ((unsigned)read_size < sector_size)
	break;

     /* next sector  */
     eblock_addr += sector_size;
     eblock_num += 1;
     if (eblock_num == eblock->blocks) {
	eblock_num=0;
	eblock++;
	if (eblock->blocks == 0)
	   break;
     }
  }


  res = 0;

cmd_program_flash_exit:
  free(flash_sector);
  free(file_sector);
  close(prom_fd);
  return res;
}

static int flash_get_eblock_map(struct mdproto_cmd_flash_info_t *flash_info,
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

   gpsd_report(LOG_PROG, "flash_get_eblock_map: incorrect sector map. "
	 "Current summary sector size: %u. Flash size: %u\n", cur_size, flash_size);

   return -1;
}

static unsigned flash_max_eblock_size(struct flash_erase_block_t *map)
{
   unsigned max_size;

   max_size=0;
   for(; map->blocks != 0; ++map) {
      if (map->bytes > max_size)
	 max_size = map->bytes;
   }

   return max_size;
}

static unsigned flash_size_from_emap(struct flash_erase_block_t *map)
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




