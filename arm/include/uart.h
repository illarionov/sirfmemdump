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

#ifndef _UART_H

#include <sys/types.h>
#include <stdint.h>

struct uart_t {
   uint16_t ctl;
   uint16_t status;
   uint16_t tx;
   uint16_t rx;
   uint16_t baud;
} __attribute__((packed, aligned(__alignof__(uint16_t))));

#define UART_CTL_BREAK_INT_EN      0x01
#define UART_CTL_PARITY_ERR_EN  0x02
#define UART_CTL_FRAME_ERR_EN   0x04
#define UART_CTL_2BTORXA_FIFO_EN   0x08
#define UART_CTL_RXA_DATA_READY_EN 0x10
#define UART_CTL_RXA_FULL_EN       0x20
#define UART_CTL_TXA_FULL_EN       0x40
#define UART_CTL_TXA_EMPTY_EN      0x80
#define UART_CTL_DATA7	        0x100
#define UART_CTL_STOP2	        0x200
#define UART_CTL_ODDPAR	        0x400
#define UART_CTL_PARITY	        0x800
#define UART_CTL_LOOPBACK       0x1000
#define UART_CTL_RESETSTATUS    0x2000
#define UART_CTL_SET_TXA_BREAK  0x4000
#define UART_CTL_RESET	        0x8000

#define UART_STATUS_BREAK_INT    0x01
#define UART_STATUS_PARITY_ERR   0x02
#define UART_STATUS_FRAME_ERR    0x04
#define UART_STATUS_OVERRUN_ERR  0x08
#define UART_STATUS_RXA_READY    0x10
#define UART_STATUS_TXA_FULL     0x20
#define UART_STATUS_TXA_EMPTY    0x40
#define UART_STATUS_2BTORXA_FULL 0x80

#ifndef UART_READ_TIMEOUT
#define UART_READ_TIMEOUT 10000000
#endif

void uart1_reset(void);
ssize_t uart1_write(const char *src, size_t size);
ssize_t uart1_read(char *dst, size_t size);

#ifdef USE_UART_A
extern volatile struct uart_t *UART_A;
#endif
#ifdef USE_UART_B
extern volatile struct uart_t *UART_B;
#endif


#define _UART_H
#endif /* _UART_H */
