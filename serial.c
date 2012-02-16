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

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "flashutils.h"
#include "arm/include/mdproto.h"

int serialSpeed(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	switch(speed){
#ifdef B115200
	case 115200:
		speed = B115200;
		break;
#endif
#ifdef B57600
	case 57600:
		speed = B57600;
		break;
#endif
	case 38400:
		speed = B38400;
		break;
#ifdef B28800
	case 28800:
		speed = B28800;
		break;
#endif
	case 19200:
		speed = B19200;
		break;
#ifdef B14400
	case 14400:
		speed = B14400;
		break;
#endif
	case 9600:
		speed = B9600;
		break;
	case 4800:
		speed = B9600;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	(int)tcgetattr(pfd, term);
	cfsetispeed(term, speed);
	cfsetospeed(term, speed);
	while (((rv = tcsetattr(pfd, TCSAFLUSH, term)) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		(void)usleep(1000);
		r++;
	}

	return rv == -1 ? -1 : 0;
}


int serialConfig(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	/* get the current terminal settings */
	(void)tcgetattr(pfd, term);
	/* set the port into "raw" mode. */
	/*@i@*/cfmakeraw(term);
	term->c_lflag &=~ (ICANON);
	/* Enable serial I/O, ignore modem lines */
	term->c_cflag |= (CLOCAL | CREAD);
	/* No output postprocessing */
	term->c_oflag &=~ (OPOST);
	/* 8 data bits */
	term->c_cflag |= CS8;
	term->c_iflag &=~ (ISTRIP);
	/* No parity */
	term->c_iflag &=~ (INPCK);
	term->c_cflag &=~ (PARENB | PARODD);
	/* 1 Stop bit */
	term->c_cflag &=~ (CSIZE | CSTOPB);
	/* No flow control */
	term->c_iflag &=~ (IXON | IXOFF);
#if defined(CCTS_OFLOW) && defined(CRTS_IFLOW) && defined(MDMBUF)
	term->c_oflag &=~ (CCTS_OFLOW | CRTS_IFLOW | MDMBUF);
#endif
#if defined(CRTSCTS)
	term->c_oflag &=~ (CRTSCTS);
#endif

	term->c_cc[VMIN] = 1;
	term->c_cc[VTIME] = 2;

	/* apply all the funky control settings */
	while (((rv = tcsetattr(pfd, TCSAFLUSH, term)) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		(void)usleep(1000);
		r++;
	}

	if(rv == -1)
		return -1;

	/* and if that all worked, try change the UART speed */
	return serialSpeed(pfd, term, speed);
}

int expect(int pfd, const char *str, size_t len, time_t timeout)
/* keep reading till we see a specified expect string or time out */
{
    size_t got = 0;
    char ch;
    ssize_t read_cnt;
    double start = clock();

    for (;;) {
        read_cnt = read(pfd, &ch, 1);
	if (read_cnt <  0)
	    return 0;		/* I/O failed */
	if (read_cnt > 0)
	   gpsd_report(LOG_RAW, "I see %zd: %02x\n", got, (unsigned)(ch & 0xff));
	if ((clock() - start) > (double)timeout*CLOCKS_PER_SEC)
	    return 0;		/* we're timed out */
	if (read_cnt > 0) {
	   if (ch == str[got])
	       got++;			/* match continues */
	   else
	       got = 0;			/* match fails, retry */
	    if (got == len)
	       return 1;
	}
    }
}

int read_full(int d, void *buf, size_t nbytes)
{
    size_t got = 0;
    ssize_t read_cnt;

    while(got < nbytes) {
       read_cnt = read(d, &((uint8_t *)buf)[got], nbytes-got);
       if (read_cnt < 0)
	  return -1;
       got += read_cnt;
    }

    return (int)got;
}

int read_mdproto_pkt(int pfd, struct mdproto_cmd_buf_t *dst)
{
   ssize_t cnt;
   uint16_t size;

   cnt = read_full(pfd, (void *)&dst->size, sizeof(dst->size));
   if (cnt < 0) {
      gpsd_report(LOG_PROG, "read() error: %s\n", strerror(errno));
      return MDPROTO_STATUS_READ_HEADER_TIMEOUT;
   }

   if (cnt < (ssize_t)sizeof(dst->size))
      return MDPROTO_STATUS_READ_HEADER_TIMEOUT;

   size = ntohs(dst->size);
   if (size > sizeof(dst->data.p))
      return MDPROTO_STATUS_TOO_BIG;

   cnt = read_full(pfd, (void *)dst->data.p, size+1);
   if (cnt < size+1)
      return MDPROTO_STATUS_READ_DATA_TIMEOUT;

   if (dst->data.p[size] != mdproto_pkt_csum(dst, size+2))
      return MDPROTO_STATUS_WRONG_CSUM;

   return MDPROTO_STATUS_OK;
}



