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

extern HINSTANCE			g_hInst;			// current instance

/* serial_session.c */
int serial_session_set_perror(struct serial_session_t *s, const TCHAR *str);
int serial_session_set_error(struct serial_session_t *s, int last_err, const TCHAR *str);

/* NMEA/SIRF */
#define MAX_NMEA_MSG_SIZE 1023
#define SIRF_MAX_PAYLOAD_SIZE 1023

#define PSRF100_PROTO_SIRF    0
#define PSRF100_PROTO_NMEA    1
#define PSRF100_PROTO_ASCII   2
#define PSRF100_PROTO_RTCM    3
#define PSRF100_PROTO_USER1   4
#define PSRF100_PROTO_NOPROTO 5

#define BOOST_38400 0
#define BOOST_57600 1
#define BOOST_115200 2

/* memdump */
enum mdproto_cmd_t {
   MDPROTO_CMD_PING               = 'z',
   MDPROTO_CMD_PING_RESPONSE      = 'Z',
   MDPROTO_CMD_MEM_READ           = 'x',
   MDPROTO_CMD_MEM_READ_RESPONSE  = 'X',

   MDPROTO_STATUS_OK                  = '+',
   MDPROTO_STATUS_WRONG_CMD           = '?',
   MDPROTO_STATUS_READ_HEADER_TIMEOUT = '.',
   MDPROTO_STATUS_READ_DATA_TIMEOUT   = ',',
   MDPROTO_STATUS_TOO_BIG             = '>',
   MDPROTO_STATUS_WRONG_CSUM          = '#',
   MDPROTO_STATUS_WRONG_PARAM         = '-'
};
#define MDPROTO_CMD_MAX_RAW_DATA_SIZE 508

struct mdproto_cmd_buf_t {
   unsigned __int16 size;
   union {
      unsigned __int8 id;
      unsigned __int8 p[512];
   } data;
   unsigned __int8 _csum_buf;
};


static int nmea_snprintf(BYTE *dst, size_t dst_size, const TCHAR *fmt, ...);
static int sirf_snprintf(BYTE *dst, size_t dst_size, const TCHAR *fmt, ...);
static int sirf_vsnprintf_payload(BYTE *dst, size_t dst_size, const TCHAR *fmt, va_list args);
static unsigned sirf_payload_csum(const BYTE *payload, unsigned payload_size);

static int mdproto_pkt_init(struct mdproto_cmd_buf_t *buf,
      unsigned cmd_id, const void *raw_data, unsigned raw_data_size);
static unsigned __int8 mdproto_pkt_csum(const void *buf, size_t size);
static int read_mdproto_pkt(struct serial_session_t *s, struct mdproto_cmd_buf_t *dst);

static int close_dump_request(struct serial_session_t *s);


/* Nmea */

static int nmea_snprintf(BYTE *dst, size_t dst_size, const TCHAR *fmt, ...)
{
	unsigned csum;
	int i;
	int written;
	int written2;
	int converted;
	TCHAR msg[MAX_NMEA_MSG_SIZE];

	va_list args;

	assert(dst_size > 0);
	assert (fmt);

	va_start(args, fmt);
	written = _vsntprintf( msg, MAX_NMEA_MSG_SIZE, fmt, args);
	va_end(args);

	if (written < 2)
		return -1;

	assert( (msg[0] == TEXT('$')) || (msg[0] == TEXT('!')));

	csum=0;
	for(i=1; i<written;i++)
		csum = csum ^ (msg[i] & 0x7f);

	written2 = _sntprintf(&msg[written], MAX_NMEA_MSG_SIZE-written,
		TEXT("*%02X\r\n"), (unsigned)csum);

	if (written2 < 0)
		return -1;

	DEBUGMSG(TRUE, ( TEXT("nmea_sprintf msg: %s"), msg));

	converted = WideCharToMultiByte(
		CP_ACP,
		0,
		msg,
		written+written2,
		dst,
		dst_size,
		NULL,
		NULL);

	if (converted == 0) {
		ERRORMSG(TRUE, (TEXT("WideCharToMultiByte error %x\n"), GetLastError()));
		return -1;
	}

	return converted;
}

/* Return value: 0 - not NMEA; <0 - maybe truncated; >0 - NMEA, message size */
int nmea_is_msg(const BYTE *buf, size_t buf_size)
{
	unsigned p;

	assert(buf);

	p=0;

	if (buf_size < 1)
		return -1;

	if (buf[p++] != '$')
		return 0;

	/* $GPxx, $PSxx */
	for(; p<5; p++) {
		if (p >= buf_size)
			return -1;
		if (!isalpha(buf[p]))
			return 0;
	}
	assert(p == 5);

	/* content */
	for(;;p++) {
		if (p >= buf_size)
			return -1;
		if (buf[p] == '\r')
			break;
		if (buf[p] == '*') {
			TCHAR csum[3];
			TCHAR *endptr;
			unsigned int_csum, calc_csum;
			unsigned i;

			/* Checksum */
			if (buf_size - p <= 4)
				return -1;
			if (buf[p+3] != '\r')
				return 0;

			csum[0] = buf[p+1] & 0xff;
			csum[1] = buf[p+2] & 0xff;
			csum[2] = 0;

			int_csum = _tcstoul(csum, &endptr, 16);
			assert(int_csum < 0xff);
			if (*endptr != 0)
				return 0;

			calc_csum=0;
			for (i=1; i<p; i++)
				calc_csum = calc_csum ^ (buf[i] & 0x7f);
			if (int_csum != calc_csum) {
				DEBUGMSG(TRUE, (TEXT("wrong csum %x != %x\n"), int_csum, calc_csum));
				return 0;
			}
			p += 3;
			break;
		} /* if */

		if (!isascii(buf[p]))
			return 0;

	} /* for */

	/* \r\n */
	assert(buf[p] == '\r');
	assert(p < buf_size);

	if (++p >= buf_size)
		return -1;

	if (buf[p] != '\n')
		return 0;

	return p+1;
}

/* Sirf */

static int sirf_snprintf(BYTE *dst, size_t dst_size, const TCHAR *fmt, ...)
{
	int payload_size;
	unsigned csum;
	va_list args;

	assert(dst);

	va_start(args, fmt);
	payload_size = sirf_vsnprintf_payload(NULL, 0, fmt, args);
	va_end(args);

	if (payload_size < 0)
		return payload_size;
	if (payload_size > SIRF_MAX_PAYLOAD_SIZE)
		return -1;

	if (8 + payload_size > (int)dst_size)
		return -1;

	/* header */
	dst[0] = 0xa0;
	dst[1] = 0xa2;

	/* length */
	dst[2] = (payload_size >> 8) & 0xff;
	dst[3] = payload_size & 0xff;

	/* data */
	va_start(args, fmt);
	sirf_vsnprintf_payload(&dst[4], payload_size, fmt, args);
	va_end(args);

	/* csum */
	csum = sirf_payload_csum(&dst[4], payload_size);
	dst[4 + payload_size] = (csum >> 8) & 0xff;
	dst[4 + payload_size + 1] = csum & 0xff;

	/* footer */
	dst[4 + payload_size + 2] = 0xb0;
	dst[4 + payload_size + 3] = 0xb3;

	return 8 + payload_size;
}

static int sirf_vsnprintf_payload(BYTE *dst, size_t dst_size, const TCHAR *fmt, va_list args)
{
	const TCHAR *p;
	unsigned dst_p;

	union {
		signed   __int8 i8;
		unsigned __int8 u8;
		signed   __int16 i16;
		unsigned __int16 u16;
		signed   __int32 i32;
		unsigned __int32 u32;
		float    f;
		double d;
		unsigned __int32 u32u32[2];
	} val;

	if (fmt == NULL)
		return 0;

	dst_p = 0;
	p = fmt;
	while (*p) {
		if (_istspace(*p)) {
			p++;
			continue;
		}

		if ( (*p == TEXT('1')) && (p[1] == TEXT('U')) ) {
			/* 1U */
			val.u8 = va_arg(args, unsigned) & 0xff;
			if (dst && (dst_p < dst_size))
				dst[dst_p] = val.u8;
			p += 2;
			dst_p += 1;
		}else if ( (*p == TEXT('1')) && (p[1] == TEXT('S')) ) {
			/* 1S */
			val.i8 = va_arg(args, signed) & 0xff;
			if (dst && (dst_p < dst_size))
				dst[dst_p] = val.i8;
			p += 2;
			dst_p += 1;
		}else if ( (*p == TEXT('2')) && (p[1] == TEXT('U')) ) {
			/* 2U */
			val.u16 = va_arg(args, unsigned) & 0xffff;
			if (dst && ((dst_p  + 1) < dst_size)) {
				dst[dst_p] = 0xff & (val.u16 >> 8);
				dst[dst_p+1] = 0xff & val.u16;
			}
			p += 2;
			dst_p += 2;
		}else if ( (*p == TEXT('2')) && (p[1] == TEXT('S')) ) {
			/* 2S */
			val.i16 = va_arg(args, signed) & 0xffff;
			if (dst && ((dst_p  + 1) <= dst_size)) {
				dst[dst_p] = 0xff & (val.u16 >> 8);
				dst[dst_p+1] = 0xff & val.u16;
			}
			p += 2;
			dst_p += 2;
		}else if ( (*p == TEXT('4')) && (p[1] == TEXT('U')) ) {
			/* 4U */
			val.u32 = va_arg(args, unsigned) & 0xffffffff;
			if (dst && ((dst_p  + 3) < dst_size)) {
				dst[dst_p] = 0xff & (val.u32 >> 24);
				dst[dst_p+1] = 0xff & (val.u32 >> 16);
				dst[dst_p+2] = 0xff & (val.u32 >> 8);
				dst[dst_p+3] = 0xff & val.u32;
			}
			p += 2;
			dst_p += 4;
		}else if ( (*p == TEXT('4')) && (p[1] == TEXT('S')) ) {
			/* 4S */
			val.i32 = va_arg(args, signed) & 0xffffffff;
			if (dst && ((dst_p  + 3) < dst_size)) {
				dst[dst_p] = 0xff & (val.u32 >> 24);
				dst[dst_p+1] = 0xff & (val.u32 >> 16);
				dst[dst_p+2] = 0xff & (val.u32 >> 8);
				dst[dst_p+3] = 0xff & val.u32;
			}
			p += 2;
			dst_p += 4;
		}else if ( (*p == TEXT('4')) && (p[1] == TEXT('F')) ) {
			/* 4F XXX */
			assert ( sizeof(val.f) == 4);
			val.f = va_arg(args, float);
			if (dst && ((dst_p  + 3) < dst_size)) {
				dst[dst_p] = 0xff & (val.u32 >> 24);
				dst[dst_p+1] = 0xff & (val.u32 >> 16);
				dst[dst_p+2] = 0xff & (val.u32 >> 8);
				dst[dst_p+3] = 0xff & val.u32;
			}
			p += 2;
			dst_p += 4;
		}else if ( (*p == TEXT('8')) && (p[1] == TEXT('F')) ) {
			/* 8F XXX XXX */
			assert ( sizeof(val.d) == 8);
			val.d = va_arg(args, double);
			if (dst && ((dst_p  + 7) < dst_size)) {
				unsigned __int32 *p0 = (unsigned __int32 *)&dst[dst_p];
				p0[0] = htonl(val.u32u32[1]);
				p0[1] = htonl(val.u32u32[0]);
			}
			p += 2;
			dst_p += 8;
		}else if ( *p == TEXT('S')) {
			/* S - null-terminated string */
			const TCHAR *str;
			unsigned str_len;

			str = va_arg(args, const TCHAR *);
			if (str == NULL) {
				str = TEXT("");
				str_len = 0;
			}else {
				str_len = _tcslen(str);
			}

			if (dst && ( dst_p + str_len + 1 < dst_size)) {
				int converted;
				converted = WideCharToMultiByte(
					CP_ACP,
					0,
					str,
					-1,
					&dst[dst_p],
					dst_size - dst_p,
					NULL,
					NULL);

				if (converted == 0) {
					ERRORMSG(TRUE, (TEXT("WideCharToMultiByte error %x\n"), GetLastError()));
					dst_p = (unsigned)-1;
					break;
				}
				assert(converted == str_len + 1);
			}
			p += 1;
			dst_p += str_len + 1;
		}else {
			DEBUGMSG(TRUE, ( TEXT("Unsupported token %s"), p));
			dst_p = (unsigned)-1;
			break;
		}
	}

	return (int)dst_p;
}

/* Return value: 0 - not Sirf; <0 - maybe truncated; >0 - Sirf, message size */
int sirf_is_msg(const BYTE *buf, size_t buf_size)
{
	unsigned csum, calculated_csum;
	unsigned payload_size;

	assert(buf);

	if (buf_size < 2)
		return -1;

	if ( (buf[0] != 0xa0)
		|| (buf[1] != 0xa2))
		return 0;

	if (buf_size < 8)
		return -1;

	payload_size = (buf[2] << 8) | buf[3];
	if (payload_size > 1023) {
		DEBUGMSG(TRUE, (TEXT("payload_size %u > 1023."), payload_size));
		return 0;
	}

	if (payload_size + 8 > buf_size)
		return -2;

	csum = (buf[4+payload_size] << 8) | buf[4+payload_size+1];

	if (buf[4+payload_size+2] != 0xb0
			|| buf[4+payload_size+3] != 0xb3) {
		DEBUGMSG(TRUE, (TEXT("wrong end seq: %x %x\n"),
			buf[4+payload_size+2], buf[4+payload_size+3]));
		return 0;
	}

	calculated_csum = sirf_payload_csum(&buf[4], payload_size);

	if (calculated_csum != csum) {
		DEBUGMSG(TRUE, (TEXT("calculated_csum %u != csum %u\n"), calculated_csum, csum));
		return 0;
	}

	return payload_size + 8;
}

static unsigned sirf_payload_csum(const BYTE *payload, unsigned payload_size)
{
	unsigned i;
	unsigned csum;

	csum=0;

	for (i=0; i < payload_size; i++)
		csum = 0x7fff & (csum + payload[i]);

	return csum;
}


/* Mdproto */

static int mdproto_pkt_init(struct mdproto_cmd_buf_t *buf,
      unsigned cmd_id,
      const void *raw_data,
      unsigned raw_data_size)
{
   unsigned data_size;
   unsigned i;

   if (raw_data_size > MDPROTO_CMD_MAX_RAW_DATA_SIZE)
      return -1;

   /* raw_data_size + id  */
   data_size = raw_data_size+1;

   buf->data.id = cmd_id;
   buf->size = htons(data_size);

   for (i=0; i < raw_data_size; i++)
      buf->data.p[i+1] = ((const BYTE *)raw_data)[i];

   buf->data.p[raw_data_size+1] = mdproto_pkt_csum(buf, data_size+2);

   /* size, id, data, csum  */
   return raw_data_size+4;
}

static unsigned __int8 mdproto_pkt_csum(const void *buf, size_t size)
{
   unsigned __int8 csum = 0;
   size_t i;

   for (i=0; i < size ; i++)
      csum += ((const unsigned __int8 *)buf)[i];
   return (unsigned __int8)(0 - csum);
}

static int read_mdproto_pkt(struct serial_session_t *s, struct mdproto_cmd_buf_t *dst)
{
	const TCHAR *err;
	int rcvd;
    INT size;

	/* Header */
	rcvd = serial_session_read(s, &dst->size, sizeof(dst->size), 20 * 1000);
	if (rcvd < 0)
		return -1;

	if (rcvd < sizeof(dst->size)) {
		err = TEXT("mdproto read header timeout");
	    serial_session_set_error(s, 0, err);
		logger_error(TEXT("mdproto read header timeout. rcvd: %u"), rcvd);
		return -1;
	}

    size = ntohs(dst->size);
	if (size > sizeof(dst->data.p)) {
		err = TEXT("mdproto message too big");
		serial_session_set_error(s, 0, err);
		logger_error(err);
		return -1;
	}

	/* Data, csum */
	rcvd = serial_session_read(s, dst->data.p, size+1, 20 * 1000);
	if (rcvd < 0)
		return -1;

	if (rcvd < size+1) {
		err = TEXT("mdproto read data timeout");
		serial_session_set_error(s, 0, err);
		logger_error(TEXT("mdproto read data timeout. rcvd: %u"), rcvd);
		return -1;
	}

	if (dst->data.p[size] != mdproto_pkt_csum(dst, size+2)) {
		err = TEXT("mdproto wrong csum");
		serial_session_set_error(s, 0, err);
		logger_error(err);
		return -2;
	}

   return 0;
}



/* Serial thread commands */

int nmea_set_serial_state(struct serial_session_t *s,
						  unsigned new_baudrate,
						  unsigned switch_to_sirf)
{
	const unsigned data_bits = 8;
	const unsigned stop_bits = 1;
	const unsigned parity = 0;
	unsigned protocol;
	unsigned lock_res;

	size_t msg_len;
	BYTE msg[120];

	assert(s);

	protocol = switch_to_sirf ? PSRF100_PROTO_SIRF : PSRF100_PROTO_NMEA;

	logger_debug(TEXT("NMEA SetSerialPort baudrate=%u, proto=%s"), new_baudrate,
		switch_to_sirf ? TEXT("SIRF") : TEXT("NMEA")
		);

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	msg_len = nmea_snprintf(msg, sizeof(msg),
		TEXT("$PSRF100,%u,%u,%u,%u,%u"), protocol, new_baudrate, data_bits, stop_bits, parity);

	assert(msg_len > 0);

	PurgeComm(s->port_handle,
		PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	if (serial_session_write(s, msg, msg_len) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	Sleep(1000);

	if (s->baudrate != new_baudrate) {
		if (serial_session_reset_state(s, new_baudrate) < 0) {
			/* XXX */
			logger_perror(TEXT("Cannot change local com-port baudrate"));
			serial_session_mtx_unlock(s);
			return -1;
		}
	}

	/* XXX: check new proto, baudrate */

	s->proto = switch_to_sirf ? PROTO_SIRF : PROTO_NMEA;

	serial_session_set_error(s, 0, NULL);
	serial_session_mtx_unlock(s);
	return 0;
}

int sirf_switch_to_nmea(struct serial_session_t *s)
{
	int lock_res;
	size_t msg_len;
	BYTE msg[120];

	logger_debug(TEXT("SIRF switch_to_nmea"));

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	msg_len = sirf_snprintf(msg, sizeof(msg),
		TEXT("1U 1U"),
		/* 135 Set Protocol */
		135,
		/* 0 - null, 1 - Sirf, 2 - NMEA, 3 - ASCII, 4 - RTCM, 5 - USER1, 6- SIRFLoc, 7 - Statistic */
		2
		);

	assert(msg_len > 0);

	if (serial_session_write(s, msg, msg_len) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	/* XXX: check NMEA */

	serial_session_set_error(s, 0, NULL);
	serial_session_mtx_unlock(s);
	return 0;
}

int sirf_set_binary_serial_port(struct serial_session_t *s, unsigned new_baudrate)
{
		int lock_res;
	size_t msg_len;
	BYTE msg[120];

	logger_debug(TEXT("SIRF set_binary_serial_port new_baudrate=%u"), new_baudrate);

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	msg_len = sirf_snprintf(msg, sizeof(msg),
		TEXT("1U 4U1U1U1U1U "),
		/* 134 Set Binary serial port */
		134,
		new_baudrate,
		8, 1, 0, 0
		);

	assert(msg_len > 0);

	if (serial_session_write(s, msg, msg_len) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	/* XXX: check new baudrate */

	serial_session_set_error(s, 0, NULL);
	serial_session_mtx_unlock(s);
	return 0;
}

int sirf_set_serial_rate(struct serial_session_t *s,
						  unsigned new_baudrate,
						  unsigned switch_to_nmea)
{
	unsigned protocol;
	int lock_res;
	size_t msg_len;
	BYTE msg[512];

	assert(s);

	protocol = switch_to_nmea ? PSRF100_PROTO_NMEA : PSRF100_PROTO_SIRF;

	logger_debug(TEXT("SIRF SetSerialPort baudrate=%u, proto=%s"), new_baudrate,
		switch_to_nmea ? TEXT("NMEA") : TEXT("SIRF")
		);

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	msg_len = sirf_snprintf(msg, sizeof(msg),
		TEXT("1U 1U1U1U4U1U1U1U1U1U 1U1U1U4U1U1U1U1U1U 1U1U1U4U1U1U1U1U1U 1U1U1U4U1U1U1U1U1U"),
		/* 165 Set UART Configuration */
		165,
		/* port_no in_poroto out_proto bitrate data_bits stop_bits parity reserved reserved */
		0, protocol, protocol, new_baudrate, 8, 1, 0, 0, 0,
		0xff, 0, 0, 0, 0, 0, 0, 0, 0,
		0xff, 0, 0, 0, 0, 0, 0, 0, 0,
		0xff, 0, 0, 0, 0, 0, 0, 0, 0
		);

	assert(msg_len > 0);

	logger_memdump(msg, msg_len, TEXT("mid165"), 0);

	PurgeComm(s->port_handle,
		PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	if (serial_session_write(s, msg, msg_len) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	Sleep(1000);

	if (s->baudrate != new_baudrate) {
		if (serial_session_reset_state(s, new_baudrate) < 0) {
			/* XXX */
			logger_perror(TEXT("Cannot change local com-port baudrate"));
			serial_session_mtx_unlock(s);
			return -1;
		}
	}

	/* XXX: check new baudrate */

	s->proto = switch_to_nmea ? PROTO_NMEA : PROTO_SIRF;

	serial_session_set_error(s, 0, NULL);
	serial_session_mtx_unlock(s);
	return 0;
}

int sirf_enter_internal_boot_mode(struct serial_session_t *s)
{
	size_t msg_len;
	unsigned rcvd;
	int lock_res;
	BYTE msg[10];

	assert(s);

	logger_debug(TEXT("SIRF enter internal boot mode"));

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	/* XXX: check for current mode */

	if (serial_session_is_open(s))
		PurgeComm(s->port_handle,
			PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	/* 148 Flash update */
	msg_len = sirf_snprintf(msg, sizeof(msg), TEXT("1U"), 148);

	assert(msg_len == 9);
	if (serial_session_write(s, msg, msg_len) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	Sleep(1000);
	PurgeComm(s->port_handle, PURGE_RXABORT  | PURGE_RXCLEAR);

	rcvd = serial_session_read(s, msg, sizeof(msg), 3000);
	if (rcvd < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	if (rcvd > 0) {
		logger_error(TEXT("Received data after switching"));
		serial_session_set_error(s, 0, TEXT("Received data after enter"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	s->proto = PROTO_INTERNAL_BOOT_MODE;
	logger_info(TEXT("Done"));

	serial_session_set_error(s, 0, NULL);
	serial_session_mtx_unlock(s);
	return 0;
}


int sirf_mid131_dump(struct serial_session_t *s)
{
	unsigned addr_from, addr_to;
	int lock_res;
	int res;
	unsigned msg_found;
	unsigned msg_len;
	int rcvd, p;
	DWORD tm;
	BYTE msg[4096];

	logger_debug(TEXT("sirf_mid131_dump()..."));

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	assert(s->request == REQUEST_DUMP);
	assert(s->req_ctx.dump.use_mid131);

	if (switch_gps_mode(s, s->proto, PROTO_SIRF) < 0)
		goto sirf_dump_cmd_end;

	addr_from = s->req_ctx.dump.addr_from;
	addr_to = s->req_ctx.dump.addr_to;

	/* 131 Handle formatted dump data */
	msg_len = sirf_snprintf(msg,
		sizeof(msg),
		TEXT("1U 1U 4U 1U 1U1U1U1U S S S"),
			/* MID 131 */
			131,
			/* elements in array to dump */
			1,
			/* data address */
			addr_from,
			/* members */
			4,
			/* List of element sizes */
			4,4,4,4,
			/* Header */
			TEXT("Mid131"),
			/* Format string */
			TEXT("%x %x %x %x"),
			/* Trailer */
			NULL
		);

	if (serial_session_is_open(s))
		PurgeComm(s->port_handle,
			PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	assert(msg_len > 0);

	logger_debug(TEXT("Send MID131 request"));

	if (serial_session_write(s, msg, msg_len) < 0)
		goto sirf_dump_cmd_end;

	serial_session_set_error(s, 0, NULL);

	logger_debug(TEXT("Waiting for MID10 Error 255 ack"));

	msg_found=0;
	tm = GetTickCount() + 1000 * 10;
	for (; tm > GetTickCount() ;) {
		int last_msg_end;
		int msg_size;

		rcvd = serial_session_read(s, msg, sizeof(msg), 0);

		last_msg_end = 0;
		p = 0;
		while (p < rcvd) {
			unsigned msg_id;

			msg_size = sirf_is_msg(&msg[p], rcvd - p);
			if (msg_size < 0) {
				p++;
				continue;
			}
			/* msg found */
			if (last_msg_end != p)
				logger_debug(TEXT("skipped %d garbage bytes"), p - last_msg_end);
			last_msg_end = p + msg_size;

			msg_id = msg[p+4];
			switch (msg_id) {
				case 10:
					/* Error ID */
					{
						unsigned err_id;
						unsigned data_size;

						err_id = msg[p+5];
						data_size = 4 * ( (msg[p+6] << 8) | msg[p+7]);

						logger_debug(TEXT("SIRF error id data %d"), err_id);
						if (err_id == 255) {
							msg_found = 1;
							logger_info(TEXT("MSG 255 found!"));
						}
					}
					break;
				case 11:
					/* Command ack */
					{
						unsigned ack_id;
						ack_id = msg[p+5];
						logger_debug(TEXT("SIRF cmd ack for msg %d"), ack_id);
					}
					break;
				case 12:
					/* Command negative ack */
					{
						unsigned nack_id;
						nack_id = msg[p+5];
						logger_debug(TEXT("SIRF cmd reject for msg %d"), nack_id);
					}
					break;
				default:
					DEBUGMSG(TRUE, (TEXT("SIRF msg %d\n"), msg_id));
					break;
			}

			p = last_msg_end;
		}

		if (msg_found)
			break;

		if (last_msg_end != p)
			logger_debug(TEXT("skipped %d garbage bytes"), p - last_msg_end);
	} /* for */

sirf_dump_cmd_end:

	res = close_dump_request(s);
	serial_session_mtx_unlock(s);
	return res;
}


int internal_boot_send_loader(struct serial_session_t *s)
{
#pragma pack(push, 1)
	struct {
		unsigned __int8 s;
		unsigned __int8 boost;
		unsigned __int32 size;
	} header;
#pragma pack(pop)

	unsigned __int32 footer;
	int rcvd;
	int lock_res;
	const BYTE *loader;
	unsigned loader_size;
	unsigned reset_vector;
	HRSRC loader_resource;
	HGLOBAL loader_loaded;
	BYTE ack[10];

	assert(sizeof(header) == 6);

	logger_debug(TEXT("Internal boot: send loader"));

	reset_vector = 0;
	loader_resource = FindResource(
		g_hInst,
		MAKEINTRESOURCE(IDR_LOADER),
		TEXT("Loader")
		);
	if (loader_resource == NULL) {
		serial_session_set_perror(s, TEXT("FindResource() error"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	loader_loaded = LoadResource(NULL, loader_resource);
	assert(loader_loaded);
	loader = LockResource(loader_loaded);
	assert(loader);
	loader_size = SizeofResource(NULL, loader_resource);
	assert(loader_size);

	header.s = 'S';
	header.boost =  0;
	header.size = htonl(loader_size);
	footer = htonl(reset_vector);

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	serial_session_set_error(s, 0, NULL);

	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	if (s->baudrate != 38400) {
		serial_session_set_error(s, 0, TEXT("Unsupported baudrate"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	PurgeComm(s->port_handle,
		PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	if (switch_gps_mode(s, s->proto, PROTO_INTERNAL_BOOT_MODE) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	/* Header */
	if (serial_session_write(s, &header, sizeof(header)) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	/* Loader */
	if (serial_session_write(s, loader, loader_size) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	/* Footer */
	if (serial_session_write(s, &footer, sizeof(footer)) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	logger_debug(TEXT("Waiting for ack"));


	if ( (rcvd = serial_session_read(s, ack, 3, 20 * 1000)) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	if (rcvd < 3) {
		logger_error(TEXT("No response from loader"));
		serial_session_set_error(s, 0, TEXT("No response from loader"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	if ( ! ( (ack[0] == '+') && (ack[1] == '+') && (ack[2] == '+'))) {
		logger_error(TEXT("Received wrong response: %02x%02x%02x"), ack[0], ack[1], ack[2]);
		serial_session_set_error(s, 0, TEXT("Received wrong response"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	s->proto = PROTO_MEMDUMP;

	logger_info(TEXT("Loader successfully launched"));

	serial_session_mtx_unlock(s);

	Sleep(1000);

	return 0;
}



int memdump_cmd_ping(struct serial_session_t *s)
{
	int msg_size;
	int lock_res;
	struct mdproto_cmd_buf_t cmd;

	assert(s);

	logger_debug(TEXT("Memdump: PING"));

	msg_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_PING, NULL, 0);

	assert(msg_size > 0);

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	if (serial_session_is_open(s))
		PurgeComm(s->port_handle,
			PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	if (switch_gps_mode(s, s->proto, PROTO_MEMDUMP) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	if (serial_session_write(s, &cmd, msg_size) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

	if (read_mdproto_pkt(s, &cmd) < 0) {
		serial_session_mtx_unlock(s);
		return -1;
	}

    if (cmd.data.id != MDPROTO_CMD_PING_RESPONSE) {
		logger_error(TEXT("received wrong response code `0x%x`"), cmd.data.id);
		serial_session_set_error(s, 0, TEXT("received wrong response code"));
		serial_session_mtx_unlock(s);
		return -1;
	}

	logger_info(TEXT("PONG"));
	serial_session_set_error(s, 0, NULL);
	serial_session_mtx_unlock(s);
	return 0;
}

int memdump_cmd_dump(struct serial_session_t *s)
{
	unsigned addr_from, addr_to;
	int write_size;
	int cur_size;
	int lock_res;
	int res;
	BYTE *dst;
	struct mdproto_cmd_buf_t cmd;

#pragma pack(push, 1)
	struct {
		unsigned __int32 src;
		unsigned __int32 dst;
	} req;
#pragma pack(pop)

	logger_debug(TEXT("memdump_cmd_dump()..."));

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	assert(s->request == REQUEST_DUMP);
	assert(s->req_ctx.dump.f_view);

	if (s->req_ctx.dump.use_mid131) {
		int res;
		res = sirf_mid131_dump(s);
		serial_session_mtx_unlock(s);
		return res;
	}

	dst = s->req_ctx.dump.f_view;
	addr_from = s->req_ctx.dump.addr_from;
	addr_to = s->req_ctx.dump.addr_to;
	req.src = htonl(addr_from);
	req.dst = htonl(addr_to);
	write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_MEM_READ, &req, sizeof(req));

	if (serial_session_is_open(s))
		PurgeComm(s->port_handle,
			PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	if (switch_gps_mode(s, s->req_ctx.dump.gps_mode, PROTO_MEMDUMP) < 0)
		goto dump_cmd_end;

	if (serial_session_write(s, &cmd, write_size) < 0)
		goto dump_cmd_end;

	serial_session_set_error(s, 0, NULL);
	while (addr_from <= addr_to) {
		int read_res;

		logger_info(TEXT("0x%x..."), addr_from);
		read_res = read_mdproto_pkt(s, &cmd);

		if (read_res < 0
			/* wrong csum */
			&& (read_res != -2)) {
			/* XXX */
			break;
		}

		if (cmd.data.id != MDPROTO_CMD_MEM_READ_RESPONSE) {
			logger_error(TEXT("Received wrong response code `0x%x`"), cmd.data.id);
			serial_session_set_error(s, 0, TEXT("Received wrong response code"));
			break;
		}

		cur_size = ntohs(cmd.size) - 1;
		if (cur_size > 0)
			memcpy(dst, &cmd.data.p[1], cur_size);
		dst += cur_size;
		addr_from += cur_size;
	}

dump_cmd_end:
	res = close_dump_request(s);
	serial_session_mtx_unlock(s);
	return res;
}

static int close_dump_request(struct serial_session_t *s)
{
	assert(s);

	/* Close request */

	if (s->req_ctx.dump.dump_to_screen && (s->last_err_msg[0] == 0)) {
		logger_memdump(s->req_ctx.dump.f_view,
			s->req_ctx.dump.addr_to - s->req_ctx.dump.addr_from + 1,
			TEXT("dump:"),
			s->req_ctx.dump.addr_from);
	}

	s->request = REQUEST_NONE;
	if (UnmapViewOfFile(s->req_ctx.dump.f_view) == 0)
		serial_session_set_perror(s, TEXT("UnmapViewOfFile() error"));
	else
		s->req_ctx.dump.f_view = NULL;

	if (CloseHandle(s->req_ctx.dump.f_map) == 0)
		serial_session_set_perror(s, TEXT("CloseHandle(f_map) error"));
	else
		s->req_ctx.dump.f_map = INVALID_HANDLE_VALUE;

	/*
	if (CloseHandle(s->req_ctx.dump.f) == 0)
		serial_session_set_perror(s, TEXT("CloseHandle(f) error"));
	else
	*/

	/* Truncate file */
	if (s->req_ctx.dump.dst_file[0] && (s->last_err_msg[0] == 0)) {
		HANDLE f;
		f = CreateFile(s->req_ctx.dump.dst_file,
			GENERIC_READ | GENERIC_WRITE, 0,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (f) {
			if (SetFilePointer(f,
				s->req_ctx.dump.addr_to - s->req_ctx.dump.addr_from + 1,
				NULL, FILE_BEGIN) == 0xFFFFFFFF) {
				logger_perror(TEXT("SetFilePointer() error"));
			}else {
				if (!SetEndOfFile(f)) {
					logger_perror(TEXT("SetEndOfFile() error"));
				}
			}
			CloseHandle(f);
		}
	}


	s->req_ctx.dump.f = INVALID_HANDLE_VALUE;

	if (s->last_err_msg[0] == 0) {
		logger_info(TEXT("Done"));
		return 0;
	}

	if (s->req_ctx.dump.dst_file[0] != 0)
			DeleteFile(s->req_ctx.dump.dst_file);

	logger_info(TEXT("Dump fails"));
	return -1;
}


int switch_gps_mode(struct serial_session_t *s, unsigned current, unsigned required)
{
	const TCHAR *err_msg;
	int res, lock_res;

	if (current == required)
		return 0;

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;

	err_msg = NULL;

	res = 0;
	switch (current) {
		case PROTO_UNKNOWN:
			s->proto = required;
			break;
		case PROTO_NMEA:
			switch (required) {
				case PROTO_SIRF:
					res = nmea_set_serial_state(s, s->baudrate, 1);
					break;
				case PROTO_INTERNAL_BOOT_MODE:
				case PROTO_MEMDUMP:
					res = nmea_set_serial_state(s, 38400, 1);
					if (res < 0)
						break;
					res = switch_gps_mode(s, PROTO_SIRF, required);
					break;
				default:
					err_msg = TEXT("Unable switch from NMEA");
					break;
			}
			break;
		case PROTO_SIRF:
			switch (required) {
				case PROTO_NMEA:
					res = sirf_switch_to_nmea(s);
					break;
				case PROTO_INTERNAL_BOOT_MODE:
					/* XXX */
					if (s->baudrate != 38400) {
						/* Switch to NMEA */
						res = sirf_switch_to_nmea(s);
						if (res < 0)
							break;
						Sleep(1000);
						/* Switch back to sirf */
						res = nmea_set_serial_state(s, 38400, 1);
						if (res < 0)
							break;
					}

					res = sirf_enter_internal_boot_mode(s);
					break;
				case PROTO_MEMDUMP:
					res = switch_gps_mode(s, PROTO_SIRF, PROTO_INTERNAL_BOOT_MODE);
					if (res < 0)
						break;
					res = switch_gps_mode(s, PROTO_INTERNAL_BOOT_MODE, PROTO_MEMDUMP);
					break;
				default:
					err_msg = TEXT("Unable switch from SIRF");
					break;
			}
			break;
		case PROTO_INTERNAL_BOOT_MODE:
			if (required != PROTO_MEMDUMP) {
				err_msg = TEXT("Unable switch from PROTO_MEMDUMP");
				break;
			}
			res = internal_boot_send_loader(s);
			break;
		case PROTO_MEMDUMP:
			err_msg = TEXT("Unable switch from memdump mode");
			break;
		default:
			assert(0);
			break;
	}


	if (err_msg) {
		res = -1;
		logger_error(err_msg);
		serial_session_set_error(s, 0, err_msg);
	}

	serial_session_mtx_unlock(s);
	return res;
}
