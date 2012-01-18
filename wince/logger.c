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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "StdAfx.h"
#include "sirfmemdump.h"

#define BUF_SIZE 4096
#define LOG_MSG_MAX_LEN 320

static int logger_vslog(unsigned log_level, const TCHAR *fmt, va_list args);

struct {

	HANDLE mtx;
	unsigned log_level;
	TCHAR buf[BUF_SIZE];

	unsigned p_end;

	DWORD last_change_ts;

	DWORD last_wnd_refresh_ts;

} static logger;

void logger_init()
{
	logger.mtx = CreateMutex(NULL, FALSE, NULL);
	if (logger.mtx == NULL) {
		DEBUGMSG(TRUE, (TEXT("logger_init: CreateMutex() error 0x%x\n"), GetLastError()));
		return;
	}

	memset(logger.buf, 0xdd, sizeof(logger.buf));

	logger.log_level = LOG_DEBUG;
	logger.buf[0] = 0;
	logger.p_end = 0;
	logger.last_change_ts = 0;
	logger.last_wnd_refresh_ts = 0;

}

void logger_destroy()
{
	CloseHandle(logger.mtx);
}


int logger_msg(unsigned log_level, const TCHAR *fmt, ...)
{
	int res;
	va_list args;

	va_start( args, fmt );
	res = logger_vslog(log_level, fmt, args);
	va_end(args);

	return res;
}

int logger_debug(const TCHAR *fmt, ...)
{
	int res;
	va_list args;
	va_start( args, fmt );
	res = logger_vslog(LOG_DEBUG, fmt, args);
	va_end(args);
	return res;
}

int logger_info(const TCHAR *fmt, ...)
{
	int res;
	va_list args;
	va_start( args, fmt );
	res = logger_vslog(LOG_INFO, fmt, args);
	va_end(args);
	return res;
}

int logger_error(const TCHAR *fmt, ...)
{
	int res;
	va_list args;
	va_start( args, fmt );
	res = logger_vslog(LOG_ERROR, fmt, args);
	va_end(args);
	return res;
}

int logger_perror(const TCHAR *str)
{
	int last_err;
	TCHAR msg[LOG_MSG_MAX_LEN];

	last_err = GetLastError();

	msg[0]=0;
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL,
			last_err, 0, msg, sizeof(msg)/sizeof(msg[0]),
			NULL);

	if (msg[0] == 0) {
		return logger_error(TEXT("%s%s0x%x"),
			(str ? str : TEXT("")),
			(str ? TEXT(": ") : TEXT("")),
			 last_err);
	}else {
		return logger_error(TEXT("%s%s%s"),
			( str ? str : TEXT("")),
			( str ? TEXT(": ") : TEXT("")),
			msg);
	}

}

int logger_memdump(void *src, size_t size, const TCHAR *desc, unsigned addr_from)
{
	const BYTE *msg;
	unsigned cur_addr, dumped;
	int written;
	TCHAR buf[120];

	msg = (const BYTE *)src;

	assert(msg);

	if (desc)
		logger_info(TEXT("%s:"), desc);

	cur_addr = addr_from;
	dumped = 0;

	while (dumped < size) {
		unsigned cur_size;

		cur_size = size - dumped;
		if (cur_size > 8) {
			written = _sntprintf(buf,
				sizeof(buf) / sizeof(buf[0]),
				TEXT("%08X: %02X %02X %02X %02X  %02X %02X %02X %02X"),
				cur_addr,
				msg[0], msg[1], msg[2], msg[3],
				msg[4], msg[5], msg[6], msg[7]);
			assert(written > 0);
			dumped += 8;
			msg += 8;
			cur_addr += 8;
		}else {
			unsigned p0;
			unsigned i;

			p0 = _sntprintf(buf,
				sizeof(buf) / sizeof(buf[0]),
				TEXT("%08X:"),
				cur_addr);
			assert(p0 > 0);

			for (i=0; i < cur_size; i++) {
				written = _sntprintf(&buf[p0],
					sizeof(buf) / sizeof(buf[0]) - p0,
					TEXT(" %02X"),
					msg[i]);
				assert(written > 0);
				p0 += written;
				if (i == 3) {
					buf[p0++]=TEXT(' ');
					buf[p0]=0;
				}
			}
			dumped += cur_size;
			msg += cur_size;
			cur_addr += cur_size;
		}

		logger_info(buf);
	}

	return 0;
}


static int get_room(size_t need_chars)
{
	size_t src_pos;
	size_t free_chars;

	if (need_chars <= 0)
		return 0;

	assert(logger.p_end < BUF_SIZE);
	assert(logger.buf[logger.p_end] == 0);

	free_chars = BUF_SIZE - 1 - logger.p_end;

	if (need_chars <= free_chars)
		return 0;

	src_pos = need_chars - free_chars;

	assert(src_pos <= logger.p_end);

	for(; src_pos < logger.p_end; src_pos++) {
		if (logger.buf[src_pos] == TEXT('\n')) {
			src_pos++;
			break;
		}
	}

	if (src_pos != 0)
		memmove(logger.buf, &logger.buf[src_pos], sizeof(TCHAR)*(logger.p_end - src_pos+1));

	logger.p_end -= src_pos;
	assert(logger.p_end < BUF_SIZE);
	assert(logger.buf[logger.p_end] == 0);

	return 0;
}

static int logger_vslog(unsigned log_level, const TCHAR *fmt, va_list args)
{
	size_t len, ts_len, full_len;
	SYSTEMTIME today;
	TCHAR ts[20];

	if (log_level < logger.log_level)
		return 0;

	/* XXX */
	len = LOG_MSG_MAX_LEN;

	GetLocalTime(&today);

	ts_len = _sntprintf(ts, sizeof(ts)/sizeof(ts[0]), TEXT("%02u:%02u:%02u "),
		today.wHour, today.wMinute, today.wSecond);
	if (ts_len <= 0)
		return -1;

	full_len = ts_len + len + 3; /* \r\n\0 */

	/* XXX */
	if (full_len > BUF_SIZE)
		return -2;

	if (WaitForSingleObject(logger.mtx, INFINITE) != WAIT_OBJECT_0)
		return -3;

	get_room(full_len);

	/* timestamp */
	memcpy(&logger.buf[logger.p_end], ts, ts_len*sizeof(TCHAR));
	logger.p_end += ts_len;

	/* message */
	len = _vsntprintf(&logger.buf[logger.p_end], LOG_MSG_MAX_LEN, fmt, args);
	if (len < 0)
		len = LOG_MSG_MAX_LEN;
	else {
		/* rtrim */
		while ( (len >= 2)
			&& (logger.buf[logger.p_end+len-2] == TEXT('\r'))
			&& (logger.buf[logger.p_end+len-1] == TEXT('\n')) )
			len -= 2;
	}

	logger.p_end += len;

	/* \r\n\0 */
	logger.buf[logger.p_end++] = TEXT('\r');
	logger.buf[logger.p_end++] = TEXT('\n');
	logger.buf[logger.p_end] = 0;

	logger.last_change_ts = GetTickCount();
	assert(logger.p_end < BUF_SIZE);

	ReleaseMutex(logger.mtx);

	return 0;
}

int logger_wnd_refresh_log(HWND wnd)
{
	if (logger.last_change_ts <= logger.last_wnd_refresh_ts)
		return 0;

	if (WaitForSingleObject(logger.mtx, INFINITE) != WAIT_OBJECT_0) {
		DEBUGMSG(TRUE, (TEXT("logger_wnd_update_log: WaitForSingleObject() error 0x%x\n"), GetLastError()));
		return -3;
	}

	SendMessage(wnd, WM_SETTEXT, 0, (LPARAM)logger.buf);
	if (logger.p_end > 0) {
		SendMessage(wnd, EM_SETSEL, (WPARAM)logger.p_end, (LPARAM)logger.p_end);
		SendMessage(wnd, EM_SCROLLCARET, 0, 0);
	}

	logger.last_wnd_refresh_ts = GetTickCount();

	ReleaseMutex(logger.mtx);

	return 0;

}
