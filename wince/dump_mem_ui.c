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

#include "stdafx.h"
#include "sirfmemdump.h"

#define DUMP_SCREEN_MAX_SIZE 1024

const struct dump_mem_dialog_values_t {
	unsigned from_adrr;
	unsigned to_addr;
	unsigned dump_to_file;
	unsigned dump_to_screen;
	TCHAR dst_file[MAX_PATH];
};

static struct dump_mem_dialog_values_t DIALOG_VALUES = {
	0x40000000,
	0x4007ffff,
	1, 0,
	TEXT("dump0x40000000.bin")
};

struct {
	unsigned id;
	const TCHAR *name;
} static const GPS_MODES[] = {
	{ PROTO_NMEA, TEXT("NMEA") },
	{ PROTO_SIRF, TEXT("SIRF") },
	{ PROTO_INTERNAL_BOOT_MODE, TEXT("Internal boot mode") },
	{ PROTO_MEMDUMP, TEXT("Memdump") },
	{ 0, NULL }
};

static int init_dialog_controls(HWND dialog, struct serial_session_t *s);
static int request_dump(HWND dialog, struct serial_session_t *s);
static void update_dialog_controls(HWND dialog);

INT_PTR CALLBACK dump_mem_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL msg_handled = FALSE;
	BOOL close_dialog = FALSE;
	struct serial_session_t **current_session;
	SHINITDLGINFO shidi;

    switch (message)
    {
		case WM_INITDIALOG:
			shidi.dwMask = SHIDIM_FLAGS;
            shidi.dwFlags = SHIDIF_DONEBUTTON
				| SHIDIF_SIPDOWN
				| SHIDIF_SIZEDLGFULLSCREEN
				| SHIDIF_EMPTYMENU;
            shidi.hDlg = dialog;
            SHInitDialog(&shidi);

			/* Save current_session as DWL_USER */
			current_session = (struct serial_session_t **)lParam;
			assert(current_session);
			SetWindowLong(dialog, DWL_USER, (LONG)lParam);

			init_dialog_controls(dialog, *current_session);

			msg_handled = TRUE;
			break;

        case WM_COMMAND:
			current_session = (struct serial_session_t **)GetWindowLong(dialog, DWL_USER);
			assert(current_session);
			switch (LOWORD(wParam))
			{
				case IDC_DST_FILE_PICKER:
					{
						OPENFILENAME ofn;
						TCHAR fname[MAX_PATH] = TEXT("");

						memset (&ofn, 0, sizeof (ofn));
						ofn.lStructSize  = sizeof (OPENFILENAME);
						ofn.hwndOwner = dialog;
						ofn.lpstrFile = fname;
						ofn.nMaxFile = MAX_PATH;
						ofn.lpstrTitle = TEXT("Save file as");
						ofn.Flags = OFN_HIDEREADONLY; 
						GetDlgItemText(dialog, IDC_DST_FILE, fname,
							sizeof(fname)/sizeof(fname[0]));
						if (GetSaveFileName (&ofn))
							SetDlgItemText(dialog, IDC_DST_FILE, fname);

						msg_handled = TRUE;
					}
					break;
				case IDC_DUMP_TO_FILE:
					if (HIWORD(wParam) == BN_CLICKED)
						update_dialog_controls(dialog);
					break;
				case IDOK:
					if (request_dump(dialog, *current_session) >= 0)
						close_dialog = TRUE;
					msg_handled = TRUE;
					break;
				case IDCANCEL:
					msg_handled = close_dialog = TRUE;
					break;
			}

            break;

        case WM_CLOSE:
			msg_handled = close_dialog = TRUE;
			break;
    }

	if (close_dialog) {
		EndDialog(dialog, LOWORD(wParam));
	}

    return (INT_PTR)msg_handled;
}

int populate_gps_mode_combobox(HWND combobox, struct serial_session_t *s)
{
	unsigned current_gps_mode;
	unsigned i;

	if (!s)
		current_gps_mode = PROTO_SIRF;
	else {
		if (serial_session_mtx_lock(s, 100) < 0)
			current_gps_mode = PROTO_SIRF;
		else {
			current_gps_mode = s->proto;
			serial_session_mtx_unlock(s);
		}
	}

	if (!s || (s->proto == PROTO_UNKNOWN))
		current_gps_mode = PROTO_SIRF;
	else
		current_gps_mode = s->proto;

	for (i=0; GPS_MODES[i].name != NULL; i++) {
		DWORD item_num;

		item_num = SendMessage(combobox, CB_ADDSTRING, 0, (LPARAM)GPS_MODES[i].name); 
		assert(item_num >= 0);
		if (GPS_MODES[i].id == current_gps_mode)
			SendMessage(combobox, CB_SETCURSEL, (WPARAM)item_num, 0);
	}

	return 0;
}

int get_gps_mode_combobox_val(HWND combobox)
{
	unsigned res;
	unsigned i;
	TCHAR tmp[80];
	
	res = PROTO_UNKNOWN;

	if (SendMessage(combobox, WM_GETTEXT, sizeof(tmp)/sizeof(tmp[0]), (LPARAM)tmp) < 0)
		return PROTO_UNKNOWN;

	for (i=0; GPS_MODES[i].name != NULL; i++) {
		if (_tcsicmp(GPS_MODES[i].name, tmp) == 0) {
			res = GPS_MODES[i].id;
			break;
		}
	}
	return res;
}

static int init_dialog_controls(HWND dialog, struct serial_session_t *s)
{
	TCHAR tmp[20];

	/* Destination file */
	SetDlgItemText(dialog, IDC_DST_FILE, DIALOG_VALUES.dst_file);

	/* From, To address */
	_sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]),
		TEXT("0x%x"), DIALOG_VALUES.from_adrr);

	SetDlgItemText(dialog, IDC_FROM_ADDR, tmp);

	_sntprintf(tmp, sizeof(tmp)/sizeof(tmp[0]),
		TEXT("0x%x"), DIALOG_VALUES.to_addr);

	SetDlgItemText(dialog, IDC_TO_ADDR, tmp);

	populate_gps_mode_combobox(GetDlgItem(dialog, IDC_CURRRENT_GPS_MODE), s);

	CheckDlgButton(dialog, IDC_DUMP_TO_FILE, DIALOG_VALUES.dump_to_file);
	CheckDlgButton(dialog, IDC_DUMP_TO_SCREEN, DIALOG_VALUES.dump_to_screen);

	update_dialog_controls(dialog);

	return 0;
}

static void update_dialog_controls(HWND dialog)
{
	unsigned dump_to_file;

	dump_to_file = IsDlgButtonChecked(dialog, IDC_DUMP_TO_FILE);

	EnableWindow(GetDlgItem(dialog, IDC_DST_FILE), dump_to_file);
	EnableWindow(GetDlgItem(dialog, IDC_DST_FILE_PICKER), dump_to_file);
	EnableWindow(GetDlgItem(dialog, IDC_DST_FILE_STATIC), dump_to_file);
	EnableWindow(GetDlgItem(dialog, IDC_DUMP_TO_SCREEN), dump_to_file);

	if (!dump_to_file)
		CheckDlgButton(dialog, IDC_DUMP_TO_SCREEN, 1);

}

static int request_dump(HWND dialog, struct serial_session_t *s)
{
	int res;
	unsigned long addr_from, addr_to;
	const TCHAR *err_msg;
	unsigned current_proto;
	unsigned use_mid131;
	unsigned dump_to_file, dump_to_screen;
	TCHAR *endptr;
	TCHAR tmp[80];
	TCHAR dst_fname[MAX_PATH];

	res = 0;
	err_msg = NULL;

	if (!s) {
		res = -1, err_msg = TEXT("Not connected");
		goto request_dump_end;
	}

	dump_to_file = IsDlgButtonChecked(dialog, IDC_DUMP_TO_FILE);
	dump_to_screen = IsDlgButtonChecked(dialog, IDC_DUMP_TO_SCREEN);

	if (!dump_to_file && !dump_to_screen)
		dump_to_screen=1;

	/* filename */
	if (dump_to_file) {
		if (GetDlgItemText(dialog, IDC_DST_FILE, dst_fname,
			sizeof(dst_fname)/sizeof(dst_fname[0])) <= 1) {
			res = -1, err_msg = TEXT("Wrong destination filename");
			goto request_dump_end;
		}
	}else
		dst_fname[0]=0;

	/* addr_from */
	if (GetDlgItemText(dialog, IDC_FROM_ADDR, tmp, sizeof(tmp)/sizeof(tmp[0])) < 1) {
		res = -1, err_msg = TEXT("Wrong address from");
		goto request_dump_end;
	}

	addr_from = _tcstoul(tmp, &endptr, 0);
	assert(endptr != NULL);
	if ((*endptr != 0)
		|| (addr_from > 0xffffffff)
		) {
		res = -1, err_msg = TEXT("Address from is not a number");
		goto request_dump_end;
	}

	/* addr_to */
	if (GetDlgItemText(dialog, IDC_TO_ADDR, tmp, sizeof(tmp)/sizeof(tmp[0])) < 1)
	{
		res = -1, err_msg = TEXT("Wrong address to");
		goto request_dump_end;
	}

	addr_to = _tcstoul(tmp, &endptr, 0);
	assert(endptr != NULL);
	if ((*endptr != 0)
		|| (addr_to > 0xffffffff)
		) {
		res = -1, err_msg = TEXT("Address to is not a number");
		goto request_dump_end;
	}

	if (addr_from > addr_to) {
		res = -1, err_msg = TEXT("addr_from > addr_to");
		goto request_dump_end;
	}

	if (dump_to_screen &&
		(addr_to - addr_from > DUMP_SCREEN_MAX_SIZE)) {
		res = -1, err_msg = TEXT("Address range is too large for screen dump");
		goto request_dump_end;
	}

	/* current_proto */
	current_proto = get_gps_mode_combobox_val(GetDlgItem(dialog, IDC_CURRRENT_GPS_MODE));
	if (current_proto == PROTO_UNKNOWN) {
		res = -1, err_msg = TEXT("Wrong GPS mode");
		goto request_dump_end;
	}

	logger_debug(TEXT("Dump requested. 0x%x-0x%x %s (mode: %s)"),
		addr_from, addr_to, dst_fname, tmp);

	/* Use MID131 */
	use_mid131 = IsDlgButtonChecked(dialog, IDC_USE_MID131);

	/* save dialog values */
	if (dump_to_file)
		_tcsncpy(DIALOG_VALUES.dst_file, dst_fname,
			sizeof(DIALOG_VALUES.dst_file)/sizeof(DIALOG_VALUES.dst_file[0]));
	DIALOG_VALUES.dump_to_file = dump_to_file;
	DIALOG_VALUES.dump_to_screen = dump_to_screen;
	DIALOG_VALUES.from_adrr = addr_from;
	DIALOG_VALUES.to_addr = addr_to;

	/* XXX: lock s */
	res = serial_session_req_dump(s, addr_from, addr_to,
		dump_to_file ? dst_fname : NULL,
		current_proto,
		use_mid131,
		dump_to_screen
		);
	if (res == -WAIT_TIMEOUT)
		err_msg = TEXT("Busy");
	else if (res < 0) {
		tmp[0]=0;
		serial_session_error(s, tmp, sizeof(tmp)/sizeof(tmp[0]));
		err_msg = tmp;
	}

request_dump_end:
	if (err_msg)
		MessageBox(dialog, err_msg, NULL, MB_OK | MB_ICONERROR);

	return res;

}

