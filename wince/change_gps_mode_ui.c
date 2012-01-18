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

/* XXX: comport_ui.c */
int populate_gps_mode_combobox(HWND combobox, struct serial_session_t *s);
int get_gps_mode_combobox_val(HWND combobox);

static int init_dialog_controls(HWND dialog, struct serial_session_t *s);


INT_PTR CALLBACK change_gps_mode_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL msg_handled = FALSE;
	BOOL close_dialog = FALSE;
	struct serial_session_t **current_session;
	SHINITDLGINFO shidi;
	unsigned from_mode, to_mode;

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
				case IDOK:
					from_mode = get_gps_mode_combobox_val(GetDlgItem(dialog, IDC_CURRRENT_GPS_MODE));
					to_mode = get_gps_mode_combobox_val(GetDlgItem(dialog, IDC_NEW_GPS_MODE));
					if ( serial_session_req_change_gps_mode(*current_session,
						from_mode, to_mode) >= 0)
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
	populate_gps_mode_combobox(GetDlgItem(dialog, IDC_CURRRENT_GPS_MODE), s);
	populate_gps_mode_combobox(GetDlgItem(dialog, IDC_NEW_GPS_MODE), s);

	return 0;
}

