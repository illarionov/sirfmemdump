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

#ifndef _SIRFGPSCONF_H
#define _SIRFGPSCONF_H

#define sirfgps_htons(_x) (uint16_t)((_x) << 8 | (_x) >> 8)
#define sirfgps_htonl(_x) \
              (((_x) >> 24) |                         \
              (((_x) & (0xff << 16)) >> 8) |          \
              (((_x) & (0xff << 8)) << 8) |           \
              ((_x) << 24))

#define USE_UART_A

#endif /* _SIRFGPSCONF_H */
