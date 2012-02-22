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

static TCHAR FIRMWARE_FILE[MAX_PATH] = TEXT("firmware.bin");

static int init_dialog_controls(HWND dialog, struct serial_session_t *s);
static int request_program_flash(HWND dialog, struct serial_session_t *s);

INT_PTR CALLBACK program_flash_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
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
				case IDC_SRC_FILE_PICKER:
					{
						OPENFILENAME ofn;
						TCHAR fname[MAX_PATH] = TEXT("");

						memset (&ofn, 0, sizeof (ofn));
						ofn.lStructSize  = sizeof (OPENFILENAME);
						ofn.hwndOwner = dialog;
						ofn.lpstrFile = fname;
						ofn.nMaxFile = MAX_PATH;
						ofn.lpstrFilter = TEXT("Binary\0*.bin\0All\0*.*\0");
						ofn.nFilterIndex = 1;
						ofn.lpstrTitle = TEXT("Select firmware file");
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
							OFN_HIDEREADONLY; 
						GetDlgItemText(dialog, IDC_SRC_FILE, fname,
							sizeof(fname)/sizeof(fname[0]));
						if (GetOpenFileName(&ofn))
							SetDlgItemText(dialog, IDC_SRC_FILE, fname);

						msg_handled = TRUE;
					}
					break;
				case IDOK:
					if (request_program_flash(dialog, *current_session) >= 0)
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

static int init_dialog_controls(HWND dialog, struct serial_session_t *s)
{
	SetDlgItemText(dialog, IDC_SRC_FILE, FIRMWARE_FILE);
	return 0;
}

static int request_program_flash(HWND dialog, struct serial_session_t *s)
{
	int res;
	const TCHAR *err_msg;
	TCHAR tmp[MAX_PATH];

	res = 0;
	err_msg = NULL;

	if (!s) {
		res = -1, err_msg = TEXT("Not connected");
		goto request_program_flash_end;
	}

	/* Firmware file */
	if (GetDlgItemText(dialog, IDC_SRC_FILE, tmp, sizeof(tmp)/sizeof(tmp[0])) < 1) {
		res = -1, err_msg = TEXT("Wrong firmware file");
		goto request_program_flash_end;
	}

	_tcsncpy(FIRMWARE_FILE, tmp, sizeof(FIRMWARE_FILE)/sizeof(FIRMWARE_FILE[0]));
	res = serial_session_req_program_flash(s, tmp);
	if (res == -WAIT_TIMEOUT)
		err_msg = TEXT("Busy");
	else if (res < 0) {
		tmp[0]=0;
		serial_session_error(s, tmp, sizeof(tmp)/sizeof(tmp[0]));
		err_msg = tmp;
	}

request_program_flash_end:
	if (err_msg)
		MessageBox(dialog, err_msg, NULL, MB_OK | MB_ICONERROR);

	return res;
}
