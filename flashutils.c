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
/*
 * This is the SiRF-dependent part of the gpsflash program.
 *
 * If we ever compose our own S-records, dlgsp2.bin looks for this header
 * unsigned char hdr[] = "S00600004844521B\r\n";
 *
 * Here's what Carl Carter at SiRF told us when he sent us informattion
 * on how to build one of these:
 *
 * --------------------------------------------------------------------------
 * Regarding programming the flash, I will attach 2 things for you -- a
 * program called SiRFProg, the source for an older flash programming
 * utility, and a description of the ROM operation.  Note that while the
 * ROM description document is for SiRFstarIII, the interface applies to
 * SiRFstarII systems like you are using.  Here is a little guide to how
 * things work:
 *
 * 1.  The receiver is put into "internal boot" mode -- this means that it
 * is running off the code contained in the internal ROM rather than the
 * external flash.  You do this by either putting a pull-up resistor on
 * data line 0 and cycling power or by giving a message ID 148.
 * 2.  The internal ROM provides a very primitive boot loader that permits
 * you to load a program into RAM and then switch to it.
 * 3.  The program in RAM is used to handle the erasing and programming
 * chores, so theoretically you could create any program of your own
 * choosing to handle things.  SiRFProg gives you an example of how to do
 * it using Motorola S record files as the programming source.  The program
 * that resides on the programming host handles sending down the RAM
 * program, then communicating with it to transfer the data to program.
 * 4.  Once the programming is complete, you transfer to it by switching to
 * "external boot" mode -- generally this requires a pull-down resistor on
 * data line 0 and either a power cycle or toggling the reset line low then
 * back high.  There is no command that does this.
 *
 * Our standard utility operates much faster than SiRFProg by using a
 * couple tricks.  One, it transfers a binary image rather than S records
 * (which are ASCII and about 3x the size of the image).  Two, it
 * compresses the binary image using some standard compression algorithm.
 * Three, when transferring the file we boost the port baud rate.  Normally
 * we use 115200 baud as that is all the drivers in most receivers handle.
 * But when supported, we can boost up to 900 kbaud.  Programming at 38400
 * takes a couple minutes.  At 115200 it takes usually under 30 seconds.
 * At 900 k it takes about 6 seconds.
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2005 Chris Kuethe <chris.kuethe@gmail.com>
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "flashutils.h"

/* block size when writing to the serial port. related to FIFO size */
#define WRBLK 512

static void
nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
    unsigned char sum = '\0';
    char c;
    char *p = sentence;

    assert( *p == '$' || *p == '!');
    p++;
    while ( ((c = *p) != '*') && (c != '\0')) {
        sum ^= c;
        p++;
    }
    *p++ = '*';
    (void)snprintf(p, 5, "%02X\r\n", (unsigned)sum);
}

static int
binary_send(int pfd, char *data, size_t ls){
	size_t nbr, nbs, nbx;
	ssize_t r;
	static int count;
	double start = clock();

	fprintf(stderr, "gpsflash: transferring binary... \010");
	count = 0;

	nbr = ls; nbs = WRBLK ; nbx = 0;
	while(nbr){
		if(nbr > WRBLK )
			nbs = WRBLK ;
		else
			nbs = nbr;

r0:		if((r = write(pfd, data+nbx, nbs)) == -1){
			if (errno == EAGAIN){ /* retry */
				(void)tcdrain(pfd); /* wait a moment */
				errno = 0; /* clear errno */
				nbr -= r; /* number bytes remaining */
				nbx += r; /* number bytes sent */
				goto r0;
			} else {
				return -1; /* oops. bail out */
			}
		}
		nbr -= r;
		nbx += r;

		(void)fputc("-/|\\"[count % 4], stderr);
		(void)fputc('\010', stderr);
		(void)fflush(stdout);
	}

	(void)fprintf(stderr, "...done (%2.2f sec).\n", (clock()-start)/CLOCKS_PER_SEC);

	return 0;
}


static void
nmea_lowlevel_send(int fd, const char *fmt, ... )
/* ship a command to the GPS, adding * and correct checksum */
{
    char buf[BUFSIZ];
    va_list ap;
    size_t l;

    buf[0]='\0';
    va_start(ap, fmt) ;
#ifdef HAVE_VSNPRINTF
    (void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
#else
    (void)vsprintf(buf + strlen(buf), fmt, ap);
#endif
    va_end(ap);
    strncat(buf, "*", 1);
    nmea_add_checksum(buf);
    l = strlen(buf);
    if (write(fd, buf, l) != (ssize_t)l)
	(void)fputs("sirfflash: write to device failed\n", stderr);
}

static unsigned
sirf_write(int fd, unsigned char *msg)
{
   unsigned int       crc;
   size_t    i, len;
   unsigned      ok;

   len = (size_t)((msg[2] << 8) | msg[3]);

   /* calculate CRC */
   crc = 0;
   for (i = 0; i < len; i++)
        crc += (int)msg[4 + i];
   crc &= 0x7fff;

   /* enter CRC after payload */
   msg[len + 4] = (unsigned char)((crc & 0xff00) >> 8);
   msg[len + 5] = (unsigned char)( crc & 0x00ff);

   ok = (write(fd, msg, len+8) == (ssize_t)(len+8));
   (void)tcdrain(fd);
   return(ok);
}

int
sirfEnterInternalBootMode(int pfd){
	unsigned status;
	static unsigned char msg[] =	{
				0xa0,0xa2,	/* header */
				0x00,0x01,	/* message length */
				0x94,		/* 0x94: firmware update */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */
	status = sirf_write(pfd, msg);
	/* wait a moment for the receiver to switch to boot rom */
	(void)sleep(2);
	return status ? 0 : -1;
}

int
sirfSendLoader(int pfd, struct termios *term, char *loader, size_t ls){
	int r, speed = 38400;
	unsigned char boost[] = {'S', BOOST_38400};
	unsigned char *msg;

	if((msg = malloc(ls+10)) == NULL){
		return -1; /* oops. bail out */
	}

#if 0
#ifdef B115200
	speed = 115200;
	boost[1] = BOOST_115200;
#else
#ifdef B57600
	speed = 57600;
	boost[1] = BOOST_57600;
#endif
#endif
#endif

	msg[0] = 'S';
	msg[1] = (unsigned char)0;
	msg[2] = (unsigned char)((ls & 0xff000000) >> 24);
	msg[3] = (unsigned char)((ls & 0xff0000) >> 16);
	msg[4] = (unsigned char)((ls & 0xff00) >> 8);
	msg[5] = (unsigned char)(ls & 0xff);
	memcpy(msg+6, loader, ls); /* loader */
	memset(msg+6+ls, 0, 4); /* reset vector */

	/* send the command to jack up the speed */
#if 0
	if((r = (int)write(pfd, boost, 2)) != 2) {
		free(msg);
		return -1; /* oops. bail out */
	}
#endif
	/* wait for the serial speed change to take effect */
	(void)tcdrain(pfd);
	(void)usleep(1000);

	/* now set up the serial port at this higher speed */
	(void)serialSpeed(pfd, term, speed);

	/* ship the actual data */
	r = binary_send(pfd, (char *)msg, ls+10);
	free(msg);
	return r;
}

int
sirfSetProto(int pfd, struct termios *term, unsigned int speed, unsigned int proto){
	int i;
	int spd[8] = {115200, 57600, 38400, 28800, 19200, 14400, 9600, 4800};
	static unsigned char sirf[] =	{
				0xa0,0xa2,	/* header */
				0x00,0x31,	/* message length */
				0xa5,		/* message 0xa5: UART config */
				0x00,0,0, 0,0,0,0, 8,1,0, 0,0, /* port 0 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 1 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 2 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 3 */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */

	if (serialConfig(pfd, term, 38400) == -1)
		return -1;

	sirf[7] = sirf[6] = (unsigned char)proto;
	sirf[8] = (unsigned char)((speed & 0xff000000) >> 24);
	sirf[9] = (unsigned char)((speed & 0xff0000) >> 16);
	sirf[10] = (unsigned char)((speed & 0xff00) >> 8);
	sirf[11] = (unsigned char)(speed & 0xff);

	/* send at whatever baud we're currently using */
	(void)sirf_write(pfd, sirf);
	nmea_lowlevel_send(pfd, "$PSRF100,%u,%u,8,1,0", speed, proto);

	/* now spam the receiver with the config messages */
	for(i = 0; i < (int)(sizeof(spd)/sizeof(spd[0])); i++) {
		(void)serialSpeed(pfd, term, spd[i]);
		(void)sirf_write(pfd, sirf);
		nmea_lowlevel_send(pfd, "$PSRF100,%u,%u,8,1,0", speed, proto);
		(void)tcdrain(pfd);
		(void)usleep(100000);
	}

	(void)serialSpeed(pfd, term, (int)speed);
	(void)tcflush(pfd, TCIOFLUSH);

	return 0;
}

