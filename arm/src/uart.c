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

#include "sirfgps.h"
#include "sirfgpsconf.h"
#include "uart.h"

static volatile uint32_t *UNK_E000500C = (uint32_t *)0xE000500C;

#ifdef USE_UART_A
volatile struct uart_t *UART_A = (struct uart_t *)0x80030000;
#endif
#ifdef USE_UART_B
volatile struct uart_t *UART_B = (struct uart_t *)0x80030010;
#endif

extern volatile enum sirfgps_version_e gps_version; /* sirfmemdump.c */

void uart1_reset(void)
{
   if (gps_version == GPS2a) {
      *UNK_E000500C &= ~0x0180;
      *UNK_E000500C |= 0x0180;
   }else {
      UART_A->ctl = UART_CTL_BREAK_INT_EN
	 | UART_CTL_RXA_DATA_READY_EN
	 | UART_CTL_RXA_FULL_EN
	 | UART_CTL_TXA_EMPTY_EN;
      UART_A->ctl |=  UART_CTL_RESET;
      UART_A->ctl &= ~UART_CTL_RESET;
   }
}

ssize_t uart1_write(const char *src, size_t size)
{
   size_t send;
   unsigned j;

   send = 0;
   for(;;) {
      if (send >= size)
	 break;
      for (j=0; j<1000; j++) {
	 if (UART_A->status & UART_STATUS_TXA_EMPTY)
	    break;
      }
      UART_A->tx = *src++;
      send++;
   }

   return (ssize_t)send;
}

ssize_t uart1_read(char *dst, size_t size)
{
   ssize_t rcvd;
   unsigned tmout;

   rcvd = 0;
   tmout = UART_READ_TIMEOUT;
   while (tmout--) {
      if (!(UART_A->status & UART_STATUS_RXA_READY))
	 continue;
      *dst++ = (char)(UART_A->rx & 0xff);
      if (++rcvd >= (ssize_t)size)
	 break;
      tmout = UART_READ_TIMEOUT;
   }

   return rcvd;
}



