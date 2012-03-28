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

#pragma once
#include "resourceppc.h"

#define SIRFMEMDUMP_VERSION TEXT("v0.3")
#define SIRFMEMDUMP_HOMEPAGE TEXT("http://github.com/littlesavage/sirfmemdump")

#define DEFAULT_COMPORT TEXT("COM1")
#define DEFAULT_BAUDRATE 38400

#define REFRESH_STATUS_WND_PERIOD 1000

#define FLASH_MAX_ERASE_BLOCK_NUM 10
/* XXX */
#define EXT_SRAM_CSN0 0x40000000


INT_PTR CALLBACK select_com_port_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK dump_mem_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK change_gps_mode_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK program_word_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK erase_sector_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK program_flash_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK change_flash_mode_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
int refresh_status_wnd(struct serial_session_t *s, HWND status_wnd);


/* Serial session */
struct serial_session_t {
	TCHAR port_name[10];
	unsigned baudrate;

	HANDLE port_handle;

	HANDLE rx_thread;
	unsigned close_thread;
	HANDLE mtx;

	DCB old_dcb;
	COMMTIMEOUTS old_tmouts;

	unsigned long rcvd_bytes;
	unsigned long open_ts;
	unsigned long last_rcvd_ts;
	unsigned long comport_errors;
	unsigned long nmea_msg_cnt;
	unsigned long sirf_msg_cnt;


	enum {
		PROTO_UNKNOWN,
		PROTO_NMEA,
		PROTO_SIRF,
		PROTO_INTERNAL_BOOT_MODE,
		PROTO_MEMDUMP
	} proto;

	DWORD last_err;
	TCHAR last_err_msg[160];

	/* dump request */
	enum {
		REQUEST_NONE,
		REQUEST_DUMP,
		REQUEST_GPS_MODE,
		REQUEST_FLASH_INFO,
		REQUEST_ERASE_SECTOR,
		REQUEST_PROGRAM_WORD,
		REQUEST_PROGRAM_FLASH,
		REQUEST_CHANGE_FLASH_MODE
	} request;
	
	union {
		struct {
			unsigned gps_mode;
			unsigned addr_from;
			unsigned addr_to;
			TCHAR dst_file[MAX_PATH];

			unsigned use_mid131;
			unsigned dump_to_screen;

			HANDLE f;
			HANDLE f_map;
			void *f_view;
		} dump;
		struct {
			unsigned from_gps_mode;
			unsigned to_gps_mode;
		} gps_mode;
		struct {
			unsigned addr;
		} erase_sector;
		struct {
			unsigned addr;
			unsigned word;
		} program_word;
		struct {
			TCHAR firmare_fname[MAX_PATH];
		} program_flash;
		struct {
			unsigned mode;
		} change_flash_mode;
	} req_ctx;
};


struct flash_erase_block_t {
   unsigned blocks;
   unsigned bytes;
};


struct serial_session_t *serial_session_create(const TCHAR *port, unsigned baudrate);
void serial_session_destroy(struct serial_session_t *s);

int serial_session_is_open(const struct serial_session_t *s);
int serial_session_open(struct serial_session_t *s);
int serial_session_close(struct serial_session_t *s);
int serial_session_reset_state(struct serial_session_t *s, unsigned baudrate);
int serial_session_read(struct serial_session_t *s, void *dst, size_t dst_size, unsigned tmout);
int serial_session_write(struct serial_session_t *s, const void *msg, size_t msg_size);

const TCHAR *serial_session_proto_name(const struct serial_session_t *s);
int serial_session_error(const struct serial_session_t *s, TCHAR *dst, size_t count);

int serial_session_mtx_lock(struct serial_session_t *s, unsigned tmout);
void serial_session_mtx_unlock(struct serial_session_t *s);

int serial_session_req_dump(struct serial_session_t *s,
							unsigned addr_from,
							unsigned addr_to,
							const TCHAR *dst_file,
							unsigned gps_mode,
							unsigned use_mid131,
							unsigned dump_to_screen
							);
int serial_session_req_change_gps_mode(struct serial_session_t *s,
									   unsigned from_mode,
									   unsigned to_mode);
int serial_session_req_flash_info(struct serial_session_t *s);
int serial_session_req_program_word(struct serial_session_t *s, unsigned addr, unsigned word);
int serial_session_req_erase_sector(struct serial_session_t *s, unsigned addr);
int serial_session_req_program_flash(struct serial_session_t *s, const TCHAR *fname);
int serial_session_req_change_flash_mode(struct serial_session_t *s, unsigned mode);

/* Serial thread commands */
int nmea_set_serial_state(struct serial_session_t *s,
						  unsigned new_baudrate,
						  unsigned switch_to_sirf);
int sirf_set_serial_rate(struct serial_session_t *s,
						  unsigned new_baudrate,
						  unsigned switch_to_nmea);
int sirf_enter_internal_boot_mode(struct serial_session_t *s);
int sirf_switch_to_nmea(struct serial_session_t *s);
int sirf_set_binary_serial_port(struct serial_session_t *s, unsigned new_baudrate);


int switch_gps_mode(struct serial_session_t *s, unsigned current, unsigned required);
int internal_boot_send_loader(struct serial_session_t *s);
int memdump_cmd_ping(struct serial_session_t *s);
int memdump_cmd_dump(struct serial_session_t *s);
int memdump_cmd_get_flash_info(struct serial_session_t *s);
int memdump_cmd_erase_sector(struct serial_session_t *s);
int memdump_cmd_program_word(struct serial_session_t *s);
int memdump_cmd_program_flash(struct serial_session_t *s);
int memdump_cmd_change_flash_mode(struct serial_session_t *s);

int sirf_is_msg(const BYTE *buf, size_t buf_size);
int nmea_is_msg(const BYTE *buf, size_t buf_size);

/* flash.c */
int flash_get_eblock_map(struct mdproto_cmd_flash_info_t *flash_info,
      struct flash_erase_block_t *res);
unsigned flash_size_from_emap(struct flash_erase_block_t *map);
unsigned flash_max_eblock_size(struct flash_erase_block_t *map);
int dump_flash_info(const struct mdproto_cmd_flash_info_t *data);

/* Logger */
#define LOG_DEBUG 100
#define LOG_INFO  200
#define LOG_ERROR 300

void logger_init();
void logger_destroy();
int logger_msg(unsigned log_level, const TCHAR *fmt, ...);
int logger_debug(const TCHAR *fmt, ...);
int logger_info(const TCHAR *fmt, ...);
int logger_error(const TCHAR *fmt, ...);
int logger_perror(const TCHAR *str);
int logger_memdump(void *src, size_t size, const TCHAR *desc, unsigned addr_from);
int logger_wnd_refresh_log(HWND wnd);

