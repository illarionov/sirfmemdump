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
#include <string.h>

#include "mdproto.h"
#include "sirfgps.h"
#include "sirfgpsconf.h"
#include "uart.h"

static volatile uint32_t *UNK_20000020 = (uint32_t *)0x20000020;
static volatile uint32_t *UNK_20000024 = (uint32_t *)0x20000024;

static volatile uint16_t *RF_VERSION    = (uint16_t *)0x80010010;
static volatile uint16_t *GPS_VERSION   = (uint16_t *)0x80010020;
static volatile uint16_t *CLOCK_SELECT  = (uint16_t *)0x80010014;
static volatile uint16_t *CLOCK_DIVIDER = (uint16_t *)0x8001002c;
static volatile uint16_t *UNK_80010060  = (uint16_t *)0x80010060;
static volatile uint16_t *GPIO_SEL      = (uint16_t *)0x80010100;
static volatile uint16_t *GPIO0_STATE0  = (uint16_t *)0x80010104;

static volatile uint32_t *GPIO_PORTVAL0  = (uint32_t *)0x80010120;
static volatile uint32_t *GPIO_PORTDIR0  = (uint32_t *)0x80010134;

static volatile uint16_t *UNK_80050000 = (uint16_t *)0x80050000;
static volatile uint16_t *UNK_80050002 = (uint16_t *)0x80050002;

static volatile uint32_t *UNK_80080000 = (uint32_t *)0x80080000;

static volatile uint8_t  *UNK_800D120C = (uint8_t *)0x800D120C;

static volatile uint32_t *UNK_FFC00780 = (uint32_t *)0xFFC00780;

volatile enum sirfgps_version_e gps_version;

void wait(unsigned n);
inline static void init2(void);

int read_cmd(void);
int write_cmd_response(uint8_t cmd_id, void *data, size_t data_size);

static struct mdproto_cmd_buf_t buf;

int main(void)
{
   uint8_t status;

   init2();
   wait(1000);
   uart1_reset();
   uart1_write("+++", 3);

   status = MDPROTO_STATUS_OK;

   while (1) {
      wait(1000);
      uart1_reset();
      status = read_cmd();
      if (status == MDPROTO_STATUS_OK) {
	 switch (buf.data.id) {
	    case MDPROTO_CMD_PING:
	       write_cmd_response(MDPROTO_CMD_PING_RESPONSE, "PONG", strlen("PONG"));
	       break;
	    case MDPROTO_CMD_MEM_READ:
	       if (MDPROTO_CMD_SIZE(buf) != 9)
		  status = MDPROTO_STATUS_WRONG_PARAM;
	       else {
		  uint32_t from, to;
		  from = (buf.data.p[1] << 24)
		     | (buf.data.p[2] << 16)
		     | (buf.data.p[3] << 8)
		     | (buf.data.p[4]);
		  to = (buf.data.p[5] << 24)
		     | (buf.data.p[6] << 16)
		     | (buf.data.p[7] << 8)
		     | (buf.data.p[8]);

		  if (to < from)
		     status = MDPROTO_STATUS_WRONG_PARAM;
		  else
		     write_cmd_response(MDPROTO_CMD_MEM_READ_RESPONSE, (void *)from, to-from+1);
	       }
	       break;
	    case MDPROTO_CMD_EXEC_CODE:
	       if (MDPROTO_CMD_SIZE(buf) != 5*4+1)
		  status = MDPROTO_STATUS_WRONG_PARAM;
	       else {
		  unsigned i;
		  uint32_t f_p;
		  union {
		     uint8_t u8[4*4];
		     uint32_t u32[4];
		  } src;
		  uint32_t dst[4];

		  f_p = (buf.data.p[1] << 24)
		     | (buf.data.p[2] << 16)
		     | (buf.data.p[3] << 8)
		     | (buf.data.p[4]);

		  for(i=0; i<4*4; i++)
		     src.u8[i] = buf.data.p[5+i];
		  dst[0] = 0xdeadc0de;
		  dst[1] = 0xdeadc0de;
		  dst[2] = 0xdeadc0de;
		  dst[3] = 0xdeadc0de;

		  asm volatile(
			"LDMIA %[src]!, {R0-R3} \n\t"
			"MOV LR, PC \n\t"
			"BX %[f_p] \n\t"
			"STMIA %[dst]!, {R0-R3} \n\t"
			:
			: [f_p]"r"(f_p), [src]"r"(&src.u8), [dst]"r"(&dst[0])
			: "memory", "r0", "r1", "r2", "r3", "lr"
			);

		  write_cmd_response(MDPROTO_CMD_EXEC_CODE_RESPONSE, (void *)&dst[0], sizeof(dst));
	       }
	       break;
	    default:
	       status = MDPROTO_STATUS_WRONG_CMD;
	       break;
	 } /* switch  */
      } /* if  */
      if (status != MDPROTO_STATUS_OK)
	 uart1_write((const char *)&status, 1);
   } /*  while(1)  */
}

int read_cmd(void)
{
   size_t cnt;
   size_t size;

   cnt = uart1_read((void *)&buf.size, sizeof(buf.size));
   if (cnt < sizeof(buf.size))
      return MDPROTO_STATUS_READ_HEADER_TIMEOUT;

   size = MDPROTO_CMD_SIZE(buf);
   if (size > sizeof(buf.data.p))
      return MDPROTO_STATUS_TOO_BIG;

   cnt = uart1_read((void *)buf.data.p, size+1);
   if (cnt < size+1)
      return MDPROTO_STATUS_READ_DATA_TIMEOUT;

   if (buf.data.p[size] != mdproto_pkt_csum(&buf, size+2))
      return MDPROTO_STATUS_WRONG_CSUM;

   return MDPROTO_STATUS_OK;
}

int write_cmd_response(uint8_t cmd_id, void *data, size_t data_size)
{
   uint8_t *p;
   size_t size;
   int write_size;

   p = (uint8_t *)data;
   do {
      if (data_size > MDPROTO_CMD_MAX_RAW_DATA_SIZE)
	 size = MDPROTO_CMD_MAX_RAW_DATA_SIZE;
      else
	 size = data_size;

      write_size = mdproto_pkt_init(&buf,
	    cmd_id, p, size);
      p += size;
      data_size -= size;
      uart1_write((void *)&buf, write_size);
   } while (data_size > 0);

   return 1;
}

void wait(unsigned n)
{
   static volatile unsigned i;
   for (i=0; i < n; i++);
}

inline static void init2(void)
{

   gps_version = 0;

   /* XXX */
   if (*UNK_20000020 == 0xE59F0010) {
      if (*UNK_20000024 == 0xE3A01001) {
	 /* GPS2 FALLTHROUGH  */
      } else if (*UNK_20000024 == 0xE3A01030) {
	 if ((*RF_VERSION & 0xff) == GPS3LT__i)
	    gps_version = GPS3LT__i;
	 goto cont;
      }else {
	 if ((*RF_VERSION & 0xff) == GPS3BT)
	    gps_version = GPS3BT;
	 goto cont;
      }
   }else if (*UNK_20000020 == 0xE59F0014) {
      if ((*RF_VERSION & 0xff) == GPS3)
	 gps_version = GPS3;
      goto cont;
   }

   gps_version = *GPS_VERSION & 0xff;
   if (gps_version != GPS2e
	 && gps_version != GPS2a_old
	 && gps_version != GPS2e_LP
	 && gps_version != GPS2e_LPi
	 && gps_version != GPS2a
	 && gps_version != GPS2LPX) {
      if ((*RF_VERSION & 0xff) == GPS3T)
	 gps_version = GPS3T;
   }

cont:

   if (gps_version == 0)
      gps_version = GPS3;

   if (gps_version == GPS2a) {
   }else if (gps_version == GPS2LPX) {
      /* GPIO13,14,15  */
      *GPIO_SEL = 0x0700;
   }else if (gps_version == GPS3) {
      /* GPIO15,16,17,18,19,20  */
       *GPIO_SEL = 0xfc00;
       *GPIO0_STATE0 = 1;
   }else if (gps_version == GPS3LT__i) {
      *UNK_80080000 = 0x00040015;
   }else if (gps_version == GPS3BT) {
      *UNK_80080000 = 0x0100002A;
   }else if (gps_version == GPS3T) {
      *UNK_80080000 = 0xAA400056;
   }else {
      if (gps_version == GPS2a_old) {
	 *UNK_80010060 = 0x0101;
	 *UNK_FFC00780 = 0xE000000C;
      }else if ((gps_version != GPS2e_LP) && (gps_version != GPS2e_LPi)) {
	 *UNK_80050002 = 0x33ff;
	 *UNK_80050000 = 0x7590;
      }
      *CLOCK_SELECT = 0;
      *CLOCK_DIVIDER = 1;
      *UNK_800D120C = 0;
   }

}

