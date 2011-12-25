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

#ifndef _SIRFGPS_H
#define _SIRFGPS_H

enum sirfgps_version_e {
   GPSUnknown = 0,
   GPS2e     = 0x20,
   GPS2a_old = 0x21,
   GPS2e_LPi = 0x22,
   GPS2e_LP  = 0x23,
   GPS2a     = 0x25,
   GPS2LPX   = 0x26,
   GPS3      = 0x30,
   GPS3LT__i = 0x35,
   GPS3BT    = 0x36,
   GPS3T     = 0x37
};

enum sirfgrf_version_e {
     GRF3w = 0x0,
     GRF3i = 0x1,
     GRF3BT = 0x2,
     GRF3LT = 0x3,
     GRF90 = 0x4,
};

#endif /* _SIRFGPS_H */
