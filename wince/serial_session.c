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

#define DEBUG_READ_TIMEOUTS 1

static DWORD WINAPI serial_session_rx_thread(LPVOID s_p);
static int serial_session_open_thread(struct serial_session_t *s);
static int serial_session_close_thread(struct serial_session_t *s);
static void serial_session_clear_stats(struct serial_session_t *s);
static void serial_session_purge(struct serial_session_t *s);
static void serial_session_wakeup_rx_thread(struct serial_session_t *s);

int serial_session_set_perror(struct serial_session_t *s, const TCHAR *str);
int serial_session_set_error(struct serial_session_t *s, int last_err, const TCHAR *str);

static int count_nmea_msg(struct serial_session_t *s, BYTE *msg, int msg_size);
static int count_sirf_msg(struct serial_session_t *s, BYTE *msg, int msg_size);

struct serial_session_t *serial_session_create(const TCHAR *port, unsigned baudrate)
{
	struct serial_session_t *s;

	s = (struct serial_session_t *)malloc(sizeof(*s));
	if (s == NULL)
		return NULL;

	_tcsncpy(s->port_name, port ? port : DEFAULT_COMPORT,
		sizeof(s->port_name)/sizeof(s->port_name[0]));
	s->port_name[sizeof(s->port_name)/sizeof(s->port_name[0])-1] = 0;
	s->baudrate = baudrate ? baudrate:  DEFAULT_BAUDRATE;
	s->request = REQUEST_NONE;
	s->last_err = 0;
	s->port_handle = INVALID_HANDLE_VALUE;
	s->last_err_msg[0] = 0;
	s->mtx = CreateMutex(NULL, FALSE, NULL);
	if (s->mtx == NULL) {
		free(s);
		return NULL;
	}
	s->close_thread = 0;
	s->rx_thread = NULL;

	s->proto = PROTO_UNKNOWN;
	serial_session_clear_stats(s);

	return s;
}

void serial_session_destroy(struct serial_session_t *s)
{
	serial_session_close(s);
	CloseHandle(s->mtx);
	free(s);
}

int serial_session_is_open(const struct serial_session_t *s)
{
	return s->port_handle != INVALID_HANDLE_VALUE;
}


int serial_session_open(struct serial_session_t *s)
{
	int written;
	const TCHAR *err_msg;
	TCHAR comport_name[15];

	assert(s);

	if (serial_session_is_open(s))
		return serial_session_reset_state(s, s->baudrate);

	err_msg = NULL;
	serial_session_set_error(s, 0, NULL);
	s->last_err = 0;
	s->last_err_msg[0] = 0;
	s->port_handle = INVALID_HANDLE_VALUE;

	written = _sntprintf(comport_name, sizeof(comport_name),
		TEXT("%s:"), s->port_name);
	if (written <= 0 || (comport_name[0] == 0)) {
		serial_session_set_error(s, 0, TEXT("Wrong COM port name"));
		return -1;
	}

	s->port_handle = CreateFile(
		comport_name,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
		);

	if (s->port_handle == INVALID_HANDLE_VALUE) {
		err_msg = TEXT("CreateFile() error");
		goto session_open_error;
	}

	/* Read DCB, timeouts */
	if (!GetCommState(s->port_handle, &s->old_dcb)) {
		err_msg = TEXT("GetCommState() error");
		goto session_open_error;
	}

	if (!GetCommTimeouts(s->port_handle, &s->old_tmouts)) {
		err_msg = TEXT("GetCommTimeouts() error");
		goto session_open_error;
	}

	s->proto = PROTO_UNKNOWN;

	if (serial_session_reset_state(s, s->baudrate) < 0)
		goto session_open_error;

	Sleep(2000);

	if (serial_session_open_thread(s) < 0) {
		err_msg = TEXT("CreateThread() error");
		goto session_open_error;
	}

	return 0;

session_open_error:
	if(err_msg)
		serial_session_set_perror(s, err_msg);
	if (s->port_handle != INVALID_HANDLE_VALUE) {
		/* XXX: check errors */
		CloseHandle(s->port_handle);
		s->port_handle = INVALID_HANDLE_VALUE;
	}
	return -1;
}

int serial_session_close(struct serial_session_t *s)
{
	assert(s);

	s->last_err = 0;
	s->last_err_msg[0] = 0;

	if (!serial_session_is_open(s))
		return 1;

	assert(s->rx_thread);

	serial_session_close_thread(s);

	SetCommTimeouts(s->port_handle, &s->old_tmouts);

	if (!SetCommState(s->port_handle, &s->old_dcb))
		serial_session_set_perror(s, TEXT("SetCommState() error"));

	if (!CloseHandle(s->port_handle)) {
		serial_session_set_perror(s, TEXT("CloseHandle() error"));
		/* XXX */
	}

	s->port_handle = INVALID_HANDLE_VALUE;
	return 0;
}

int serial_session_reset_state(struct serial_session_t *s, unsigned baudrate)
{
	unsigned changed;
	DCB dcb;
	COMMTIMEOUTS tmouts;

	assert(s);
	assert(serial_session_is_open(s));

	s->last_err = 0;
	s->last_err_msg[0] = 0;

	changed = 0;

	memcpy(&dcb, &s->old_dcb, sizeof(dcb));
	memcpy(&tmouts, &s->old_tmouts, sizeof(tmouts));

	dcb.BaudRate = baudrate;
	dcb.fBinary = TRUE;
	dcb.fParity = FALSE;
	dcb.fOutX = dcb.fInX = FALSE;
	dcb.fNull = FALSE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fAbortOnError = TRUE;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	if (!SetCommState(s->port_handle, &dcb)){
		serial_session_set_perror(s, TEXT("SetCommState() error"));
		return -1;
	}

	s->baudrate = baudrate;

	tmouts.ReadIntervalTimeout = 10;
	tmouts.ReadTotalTimeoutConstant = 50;
	tmouts.ReadTotalTimeoutMultiplier = 0;

	tmouts.WriteTotalTimeoutConstant = 10;
	tmouts.WriteTotalTimeoutMultiplier = 1000;

	Sleep(500);

	/* XXX: check for errors */
	SetCommTimeouts(s->port_handle, &tmouts);

	serial_session_clear_stats(s);
	s->open_ts = GetTickCount();

	return 0;
}

int serial_session_set_perror(struct serial_session_t *s, const TCHAR *str)
{
	int last_err;

	assert(s);

	last_err = GetLastError();

	logger_perror(str);

	return serial_session_set_error(s, last_err, str);
}

int serial_session_set_error(struct serial_session_t *s, int last_err, const TCHAR *str)
{
	s->last_err = last_err;
	if (str != NULL) {
		_sntprintf(s->last_err_msg, sizeof(s->last_err_msg)/sizeof(s->last_err_msg[0]), str);
		s->last_err_msg[sizeof(s->last_err_msg)/sizeof(s->last_err_msg[0])-1]=0;
	}else
		s->last_err_msg[0]=0;

	return last_err;
}

int serial_session_error(const struct serial_session_t *s, TCHAR *dst, size_t count)
{
	int written;
	TCHAR last_err_str[80];

	assert(s);
	assert(dst);
	assert(count > 0);

	last_err_str[0]=0;
	dst[0] = 0;

	if (s->last_err) {
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL,
			s->last_err, 0, last_err_str, sizeof(last_err_str)/sizeof(last_err_str[0]),
			NULL);
	}

	if (s->last_err && (last_err_str[0] == 0))
	{
		/* No text for error code */
		written = _sntprintf(dst,
			count,
			TEXT("%s: %s%s0x%x"),
			s->port_name,
			s->last_err_msg,
			s->last_err_msg[0] == 0 ? TEXT("") : TEXT(" "),
			s->last_err);
	}else {
		written = _sntprintf(dst,
			count,
			TEXT("%s: %s%s%s"),
			s->port_name,
			s->last_err_msg,
			s->last_err_msg[0] == 0 || last_err_str[0] == 0 ? TEXT("") : TEXT(" "),
			last_err_str);
	}

	if (written < 0)
		dst[count-1] = 0;

	return written;
}


static void serial_session_clear_stats(struct serial_session_t *s)
{
	assert(s);

	s->rcvd_bytes = 0;
	s->last_rcvd_ts = 0;
	s->open_ts = 0;
	s->comport_errors = 0;
	s->sirf_msg_cnt = 0;
	s->nmea_msg_cnt = 0;

}

static void serial_session_purge(struct serial_session_t *s)
{
	assert(s);
	assert(s->port_handle != INVALID_HANDLE_VALUE);
	PurgeComm(s->port_handle,
		PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
}

const TCHAR *serial_session_proto_name(const struct serial_session_t * s)
{
	const TCHAR *res;

	if (s == NULL || !serial_session_is_open(s))
		return TEXT("Not connected");

	switch (s->proto) {
		case PROTO_UNKNOWN:
			res = TEXT("Unknown");
			break;
		case PROTO_NMEA:
			res = TEXT("NMEA");
			break;
		case PROTO_SIRF:
			res = TEXT("SIRF");
			break;
		case PROTO_INTERNAL_BOOT_MODE:
			res = TEXT("Internal boot mode");
			break;
		case PROTO_MEMDUMP:
			res = TEXT("Memdump");
			break;
		default:
			assert(0);
			break;
	}

	return res;

}

static int serial_session_clear_com_error(struct serial_session_t *s)
{
	TCHAR err_msg[120];
	DWORD errors;

	assert(GetLastError() == ERROR_OPERATION_ABORTED);
	if (!ClearCommError(s->port_handle, &errors, NULL)) {
		serial_session_set_perror(s, TEXT("ClearCommError() error"));
		return -1;
	}

	DEBUGMSG( (errors & ~CE_FRAME) , (TEXT("ClearCommError errors=%x"), errors));

	err_msg[0]=0;
	_sntprintf(err_msg, sizeof(err_msg)/sizeof(err_msg[0]),
		TEXT("Com port error:%s%s%s%s%s"),
			errors & CE_BREAK    ? " BREAK"    : "",
			errors & CE_FRAME    ? " FRAME"    : "",
			errors & CE_OVERRUN  ? " OVERRUN"  : "",
			errors & CE_RXOVER   ? " RXOVER"   : "",
			errors & CE_RXPARITY ? " RXPARITY" : ""
			);
	err_msg[sizeof(err_msg)/sizeof(err_msg[0])-1]=0;

	s->comport_errors++;
	logger_debug(err_msg);

	serial_session_set_error(s, 0, err_msg);

	return errors;
}

int serial_session_write(struct serial_session_t *s, const void *msg, size_t msg_size)
{
	int written;

	assert(s);
	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		return -1;
	}

	DEBUGMSG(TRUE, (TEXT("writefile() msg_size=%u\n"), msg_size));
	if (!WriteFile(s->port_handle, msg, msg_size, &written, NULL)) {
		if (GetLastError() == ERROR_OPERATION_ABORTED)
			serial_session_clear_com_error(s);
		else
			serial_session_set_perror(s, TEXT("WriteFile() error"));
		return -1;
	}

	if (written != msg_size) {
		logger_debug(TEXT("Requested: %u, written: %u bytes"), msg_size, written);
	}

	return written;
}

int serial_session_read(struct serial_session_t *s, void *dst, size_t dst_size, unsigned tmout)
{
	unsigned rcvd_total;
	unsigned rcvd;
	DWORD read_start_tm, read_end_tm, iter_cnt;

	assert(s);
	if (!serial_session_is_open(s)) {
		serial_session_set_error(s, 0, TEXT("Not connected"));
		return -1;
	}

	read_start_tm = GetTickCount();
	iter_cnt = 0;
	rcvd_total = 0;
	while(rcvd_total < dst_size) {
		iter_cnt++;
		if (!ReadFile(s->port_handle, &((BYTE *)dst)[rcvd_total],
			dst_size-rcvd_total, &rcvd, NULL)) {
			if (GetLastError() == ERROR_OPERATION_ABORTED)
				serial_session_clear_com_error(s);
			else
				serial_session_set_perror(s, TEXT("ReadFile() error"));
			break;
		}

		if (tmout) {
			read_end_tm = GetTickCount();
			if (read_start_tm  + tmout <= read_end_tm)
				break;
		}else {
			if (rcvd == 0)
				break;
		}

		rcvd_total += rcvd;
		assert(rcvd_total <= dst_size);
	}

	if (rcvd_total > 0)
		s->last_rcvd_ts = GetTickCount();

	s->rcvd_bytes += rcvd_total;

#ifdef DEBUG_READ_TIMEOUTS
	read_end_tm = GetTickCount();
	DEBUGMSG(TRUE, (TEXT("%u: session_read(). %u ms, %u iters, %i bytes rcvd\n"),
		read_end_tm, read_end_tm-read_start_tm, iter_cnt, (int)rcvd_total));
#endif

	return (int)rcvd_total;
}


int serial_session_mtx_lock(struct serial_session_t *s, unsigned tmout)
{
	int res;
	DWORD wait_res;

	assert(s);

	wait_res = WaitForSingleObject(s->mtx, tmout);
	switch (wait_res) {
		case WAIT_TIMEOUT:
			res = -WAIT_TIMEOUT;
			break;
		case WAIT_FAILED:
			logger_perror(TEXT("WaitForSingleObject() error"));
			res = -1;
			break;
		case WAIT_OBJECT_0:
			res = 0;
			break;
		default:
			logger_error(TEXT("WaitForSingleObject() error %x"), wait_res);
			res = -1 * wait_res;
			break;
	}

	return res;
}

void serial_session_mtx_unlock(struct serial_session_t *s)
{
	assert(s);
	ReleaseMutex(s->mtx);
}

int serial_session_req_change_gps_mode(struct serial_session_t *s,
									   unsigned from_mode,
									   unsigned to_mode)
{
	int lock_res;
	const TCHAR *err;

	assert(s);

	if ( (lock_res = serial_session_mtx_lock(s, 3000)) < 0)
		return lock_res;

	if (s->request != REQUEST_NONE) {
		err = TEXT("Request in queue");
		serial_session_set_error(s, 0, err);
		return -1;
	}

	s->request = REQUEST_GPS_MODE;
	s->req_ctx.gps_mode.from_gps_mode = from_mode;
	s->req_ctx.gps_mode.to_gps_mode = to_mode;

	serial_session_mtx_unlock(s);

	serial_session_wakeup_rx_thread(s);

	return 0;
}

int serial_session_req_dump(struct serial_session_t *s,
							unsigned addr_from,
							unsigned addr_to,
							const TCHAR *dst_file,
							unsigned gps_mode,
							unsigned use_mid131,
							unsigned dump_to_screen
							)
{
	int lock_res;
	unsigned size0;
	SYSTEM_INFO sysinfo;
	const TCHAR *err;

	assert(s);

	if ( (lock_res = serial_session_mtx_lock(s, 3000)) < 0)
		return lock_res;

	if (s->request != REQUEST_NONE) {
		err = TEXT("Request in queue");
		serial_session_set_error(s, 0, err);
		return -1;
	}

	s->req_ctx.dump.addr_from = addr_from;
	s->req_ctx.dump.addr_to = addr_to;
	s->req_ctx.dump.gps_mode = gps_mode;
	s->req_ctx.dump.use_mid131 = use_mid131;
	s->req_ctx.dump.dump_to_screen = dump_to_screen;
	if (dst_file) {
		_tcsncpy(s->req_ctx.dump.dst_file, dst_file,
			sizeof(s->req_ctx.dump.dst_file)/sizeof(s->req_ctx.dump.dst_file[0]));
		s->req_ctx.dump.dst_file[sizeof(s->req_ctx.dump.dst_file)/sizeof(s->req_ctx.dump.dst_file[0])-1]=0;

		s->req_ctx.dump.f = CreateFileForMapping(s->req_ctx.dump.dst_file,
			GENERIC_READ | GENERIC_WRITE,
			0,
			0,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (s->req_ctx.dump.f == INVALID_HANDLE_VALUE) {
			serial_session_set_perror(s, TEXT("Unable to create file"));
			serial_session_mtx_unlock(s);
			return -1;
		}
	}else {
		s->req_ctx.dump.dst_file[0] = 0;
		s->req_ctx.dump.f = INVALID_HANDLE_VALUE;
	}

	GetSystemInfo(&sysinfo);
	size0 = sysinfo.dwAllocationGranularity * (
			(addr_to - addr_from  + 1 + sysinfo.dwAllocationGranularity - 1) / sysinfo.dwAllocationGranularity);
	DEBUGMSG(TRUE, (TEXT("size0=%u"), size0));

	s->req_ctx.dump.f_map = CreateFileMapping(s->req_ctx.dump.f,
		NULL,
		PAGE_READWRITE | SEC_COMMIT,
		0,
		size0,
		NULL);

	if (s->req_ctx.dump.f_map == NULL) {
		serial_session_set_perror(s, TEXT("Unable to create file mapping"));
		CloseHandle(s->req_ctx.dump.f);
		if (s->req_ctx.dump.dst_file[0] != 0)
			DeleteFile(s->req_ctx.dump.dst_file);
		serial_session_mtx_unlock(s);
		return -1;
	}

	s->req_ctx.dump.f_view = MapViewOfFile(s->req_ctx.dump.f_map,
		FILE_MAP_READ | FILE_MAP_WRITE,
		0, 0,
		addr_to - addr_from + 1
		);

	if (s->req_ctx.dump.f_view == NULL) {
		serial_session_set_perror(s, TEXT("MapViewOfFile() error"));
		CloseHandle(s->req_ctx.dump.f_map);
		/* CloseHandle(s->req_ctx.dump.f); */
		if (s->req_ctx.dump.dst_file[0] != 0)
			DeleteFile(s->req_ctx.dump.dst_file);
		serial_session_mtx_unlock(s);
		return -1;
	}

	s->request = REQUEST_DUMP;
	logger_info(TEXT("dump request queued"));
	serial_session_mtx_unlock(s);

	serial_session_wakeup_rx_thread(s);

	return 0;
}

static int serial_session_open_thread(struct serial_session_t *s)
{
	DWORD th_id;

	assert(s);
	assert(s->rx_thread == NULL);

	s->close_thread = 0;
	s->rx_thread = CreateThread(NULL, 0, serial_session_rx_thread,
		(LPVOID)s, 0, &th_id);
	if (s->rx_thread == NULL)
		return -1;

	return 0;
}

static int serial_session_close_thread(struct serial_session_t *s)
{
	DWORD exit_code;
	DWORD tm;
	int lock_res;

	assert(s);
	assert(s->rx_thread != NULL);

	if (lock_res = serial_session_mtx_lock(s, INFINITE) < 0)
		return lock_res;
	s->close_thread = 1;
	serial_session_mtx_unlock(s);

	tm = GetTickCount() + 1000 * 10;
	for(;;) {
		/* reset WaitCommEvent() */
		serial_session_wakeup_rx_thread(s);

		Sleep(100);

		if (GetExitCodeThread(s->rx_thread, &exit_code)) {
			if (exit_code != STILL_ACTIVE)
				break;
			DEBUGMSG(TRUE, (TEXT("serial_session_close_thread STILL_ACTIVE\n")));
		}
		if (tm < GetTickCount()) {
			DEBUGMSG(TRUE, (TEXT("serial_session_close_thread: wait timeout\n")));
			logger_debug(TEXT("close_thread: wait timeout"));
			TerminateThread(s->rx_thread, 0);
			break;
		}
	}
	CloseHandle(s->rx_thread);
	s->rx_thread = NULL;

	return 0;
}

static void serial_session_wakeup_rx_thread(struct serial_session_t *s)
{
	assert(s);

	if (serial_session_mtx_lock(s, 100) < 0) {
		logger_debug(TEXT("wakeup_rx_thread() skipped"));
		return;
	}

	logger_debug(TEXT("wakeup_rx_thread()"));

	if (!SetCommMask(s->port_handle, 0)) {
		DEBUGMSG(TRUE, (TEXT("serial_session_wakeup_rx_thread(): SetCommMask() error %x"),
			GetLastError()));
	}

	serial_session_mtx_unlock(s);
}

static DWORD WINAPI serial_session_rx_thread(LPVOID s_p)
{
	struct serial_session_t *s;
	unsigned res;
	DWORD status;
	unsigned wait_comm_event_err;
	int rxbuf_pos;
	BYTE rx_buf[16384];

	s = (struct serial_session_t *)s_p;
	res=0;

	DEBUGMSG(TRUE, (TEXT("serial_session_rx_thread()\n")));

	serial_session_purge(s);

	rxbuf_pos=0;

	for (;;) {
		wait_comm_event_err = 0;
		/* XXX */
		/*
		SetCommMask(s->port_handle, EV_RXFLAG | EV_RXCHAR | EV_ERR);

		if (!WaitCommEvent(s->port_handle, &status, 0)) {
			wait_comm_event_err = GetLastError();
			logger_perror(TEXT("WaitCommEvent() error"));
			Sleep(100);
		}
		*/
		Sleep(300);

		if (serial_session_mtx_lock(s, INFINITE) < 0) {
			res = 1;
			break;
		}

		serial_session_set_error(s,
			wait_comm_event_err,
			wait_comm_event_err ? TEXT("WaitCommEvent() error") : NULL);

		if (s->close_thread) {
			serial_session_mtx_unlock(s);
			break;
		}

		if (1 || (status & (EV_RXFLAG | EV_RXCHAR | EV_ERR))) {
			int p;
			int msg_size;
			int rcvd;
			int last_msg_end;
			int first_truncated_msg_p;
			int read_time;

			/* rcv */
			read_time = GetTickCount();
			if (!ReadFile(s->port_handle, 
				&rx_buf[rxbuf_pos],
				sizeof(rx_buf)-rxbuf_pos,
				&rcvd, NULL)){
				if (GetLastError() == ERROR_OPERATION_ABORTED)
					serial_session_clear_com_error(s);
				else
					serial_session_set_perror(s, TEXT("ReadFile() error"));
				rcvd = 0;
			}else {
				unsigned t2 = GetTickCount();
				DEBUGMSG(DEBUG_READ_TIMEOUTS, (TEXT("%u: ReadFile() %u ms %u bytes\n"), t2, t2 - read_time, rcvd));
			}

			if (rcvd > 0) {
				p = 0;
				last_msg_end=0;
				first_truncated_msg_p = -1;

				s->last_rcvd_ts = GetTickCount();
				s->rcvd_bytes += rcvd;
				while (p < rcvd+rxbuf_pos) {
					msg_size = sirf_is_msg(&rx_buf[p], rcvd + rxbuf_pos - p);
					if (msg_size > 0) {
						/* Sirf MSG found*/
						DEBUGMSG(last_msg_end != p,
							(TEXT("skipped %d bytes\n"), p - last_msg_end));
						count_sirf_msg(s, &rx_buf[p], msg_size);
						p += msg_size;
						last_msg_end = p;
						first_truncated_msg_p = -1;
						continue;
					}else if (msg_size < 0) {
						/* truncated */
						if (first_truncated_msg_p == -1)
							first_truncated_msg_p = p;
					}

					msg_size = nmea_is_msg(&rx_buf[p], rcvd + rxbuf_pos - p);
					if (msg_size > 0) {
						/* NMEA msg found */
						DEBUGMSG(last_msg_end != p, (TEXT("nmea skipped %d bytes\n"), p - last_msg_end));
						count_nmea_msg(s, &rx_buf[p], msg_size);
						p += msg_size;
						last_msg_end = p;
						first_truncated_msg_p = -1;
						continue;
					}else if (msg_size < 0) {
						/* truncated */
						if (first_truncated_msg_p == -1)
							first_truncated_msg_p = p;
					}
					p++;
				} /* while */

				/* Handle truncated message */
				if ( (first_truncated_msg_p == 0)
					&& (rcvd+rxbuf_pos == sizeof(rx_buf))) {
						/* Buffer full. search for next truncated msg */
						first_truncated_msg_p = -1;
						for (p=1; p < sizeof(rx_buf); p++) {
							msg_size = sirf_is_msg(&rx_buf[p], sizeof(rx_buf)-p);
							assert(msg_size <= 0);
							if (msg_size < 0) {
								first_truncated_msg_p = p;
								break;
							}
							msg_size = nmea_is_msg(&rx_buf[p], sizeof(rx_buf)-p);
							assert(msg_size <= 0);
							if (msg_size < 0) {
								first_truncated_msg_p = p;
								break;
							}
						} /* for */
						DEBUGMSG(TRUE, (TEXT("skpped %u garbage bytes"),
							first_truncated_msg_p == -1 ? sizeof(rx_buf) : first_truncated_msg_p));
				} /* if */

				if (first_truncated_msg_p == -1) {
					rxbuf_pos = 0;
				}else {
					if (first_truncated_msg_p == 0) {
						rxbuf_pos += rcvd;
					}else {
						memmove(rx_buf, &rx_buf[first_truncated_msg_p],
							rcvd + rxbuf_pos - first_truncated_msg_p + 1);
						rxbuf_pos = rcvd + rxbuf_pos - first_truncated_msg_p;
						assert (rxbuf_pos < sizeof(rx_buf));
					}
				}
			} /* if (rcvd < 0) */
		}else {
			DEBUGMSG(TRUE, (TEXT("WaitCommEvent status: %x\n"), status));
		}

		/* dump request */
		switch (s->request) {
			case REQUEST_NONE:
				break;
			case REQUEST_DUMP:
				memdump_cmd_dump(s);
				break;
			case REQUEST_GPS_MODE:
				switch_gps_mode(s, s->req_ctx.gps_mode.from_gps_mode,
					s->req_ctx.gps_mode.to_gps_mode);
				s->request = REQUEST_NONE;
				break;
			default:
				assert(0);
				break;
		}	

		serial_session_mtx_unlock(s);
	} /* for(;;) */

	serial_session_purge(s);

	return res;
}

static int count_sirf_msg(struct serial_session_t *s, BYTE *msg, int msg_size)
{
	unsigned msg_id;
	unsigned err_id;
	unsigned data_size;
	unsigned ack_id, nack_id;

	msg_id = msg[4];
	switch (msg_id) {
		case 10:
		/* Error ID */		
			err_id = msg[5];
			data_size = 4 * ( (msg[6] << 8) | msg[7]);
			logger_debug(TEXT("SIRF error id data %d"), err_id);
		break;
		case 11:
			/* Command ack */
			ack_id = msg[5];
			logger_debug(TEXT("SIRF cmd ack for msg %d"), ack_id);
			break;
		case 12:
			/* Command negative ack */
			nack_id = msg[5];
			logger_debug(TEXT("SIRF cmd reject for msg %d"), nack_id);
			break;
		default:
			break;
	} /* switch */

	s->sirf_msg_cnt++;
	s->proto = PROTO_SIRF;

	return msg_size;
}

static int count_nmea_msg(struct serial_session_t *s, BYTE *msg, int msg_size)
{
	s->nmea_msg_cnt++;
	s->proto = PROTO_NMEA;
	return msg_size;
}