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


struct {
	unsigned id;
	const TCHAR *name;
} static const FLASH_MODES[] = {
	{ 0x90, TEXT("0x90 Product identification") },
	{ 0x98, TEXT("0x98 CFI Query") },
	{ 0xff, TEXT("Read array (default)") },
	{ 0, NULL }
};

static int get_flash_mode_combobox_val(HWND combobox);
static int init_dialog_controls(HWND dialog, struct serial_session_t *s);

INT_PTR CALLBACK change_flash_mode_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL msg_handled = FALSE;
	BOOL close_dialog = FALSE;
	struct serial_session_t **current_session;
	SHINITDLGINFO shidi;
	unsigned new_mode;

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
					new_mode = get_flash_mode_combobox_val(GetDlgItem(dialog, IDC_FLASH_MODE));
					if ( serial_session_req_change_flash_mode(*current_session,
						new_mode) >= 0)
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
	unsigned i;
	HANDLE flash_mode_cbox;

	flash_mode_cbox = GetDlgItem(dialog, IDC_FLASH_MODE);
	assert(flash_mode_cbox);

	for (i=0; FLASH_MODES[i].name != NULL; i++) {
		DWORD item_num;

		item_num = SendMessage(flash_mode_cbox, CB_ADDSTRING, 0, (LPARAM)FLASH_MODES[i].name); 
		assert(item_num >= 0);
	}

	return 0;
}

static int get_flash_mode_combobox_val(HWND combobox)
{
	unsigned res;
	unsigned i;
	TCHAR tmp[80];
	
	res = 0xff;

	if (SendMessage(combobox, WM_GETTEXT, sizeof(tmp)/sizeof(tmp[0]), (LPARAM)tmp) < 0)
		return res;

	for (i=0; FLASH_MODES[i].name != NULL; i++) {
		if (_tcsicmp(FLASH_MODES[i].name, tmp) == 0) {
			res = FLASH_MODES[i].id;
			break;
		}
	}
	return res;
}
