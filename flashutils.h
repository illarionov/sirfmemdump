/*
 * Copyright (c) 2005-2007 Chris Kuethe <chris.kuethe@gmail.com>
 * Copyright (c) 2005-2007 Eric S. Raymond <esr@thyrsus.com>
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


#ifndef FLASHUTILS_H
#define FLAHUTILS_H

#include <termios.h>
#include <stdio.h>
#include <stdarg.h>

/* From the SiRF protocol manual... may as well be consistent */
#define PROTO_SIRF 0
#define PROTO_NMEA 1

#define BOOST_38400 0
#define BOOST_57600 1
#define BOOST_115200 2

int sirfEnterInternalBootMode(int pfd);
int sirfSendLoader(int pfd, struct termios *term, char *loader, size_t ls);
int sirfSetProto(int pfd, struct termios *term, unsigned int speed, unsigned int proto);

void gpsd_report(int errlevel, const char *fmt, ... );
int serialSpeed(int pfd, struct termios *term, int speed);
int serialConfig(int pfd, struct termios *term, int speed);

#endif /* FLASHUTILS_H */


