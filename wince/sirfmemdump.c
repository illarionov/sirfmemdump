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

#define MAX_LOADSTRING 100
#define IDT_UPDATE_STATUS 1

// Global Variables:
HINSTANCE			g_hInst;			// current instance
HWND				g_hWndMenuBar;		// menu bar handle

static struct serial_session_t *current_session;

// Forward declarations of functions included in this code module:
BOOL			InitInstance(HINSTANCE, int);
INT_PTR CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
static int refresh_mainmenu();


int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPTSTR    lpCmdLine,
                   int       nCmdShow)
{
	MSG msg;
	HACCEL hAccelTable;

	current_session = NULL;
	logger_init();

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_SIRFMEMDUMP));

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	logger_destroy();
	return (int) msg.wParam;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    HWND hWnd;
    TCHAR szTitle[MAX_LOADSTRING];		// title bar text

    g_hInst = hInstance; // Store instance handle in our global variable

    // SHInitExtraControls should be called once during your application's initialization to initialize any
    // of the device specific controls such as CAPEDIT and SIPPREF.
    SHInitExtraControls();

    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);


    //If it is already running, then focus on the window, and exit
    hWnd = FindWindow(NULL, szTitle);
    if (hWnd)
    {
        // set focus to foremost child window
        // The "| 0x00000001" is used to bring any owned windows to the foreground and
        // activate them.
        SetForegroundWindow((HWND)((ULONG) hWnd | 0x00000001));
        return 0;
    }

	hWnd = CreateDialog (hInstance,
                          MAKEINTRESOURCE (IDD_MAIN),
                           NULL,
                           WndProc);


    if (!hWnd)
      return FALSE;

	logger_info(TEXT("Sirf memory dumper"));

	ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

	/* Select com port dialog */
	DialogBoxParam(g_hInst,
		(LPCTSTR)IDD_SELECT_COM_PORT,
		hWnd,
		select_com_port_callback,
		(LPARAM)&current_session
	);

	refresh_status_wnd(current_session, hWnd);
	refresh_mainmenu();

    return TRUE;
}



// Message handler for MAIN box.
INT_PTR CALLBACK WndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	SHMENUBARINFO mbi;
    SHINITDLGINFO shidi;
	int msg_handled;
	static SHACTIVATEINFO s_sai;

	msg_handled = FALSE;

    switch (message)
    {
        case WM_INITDIALOG:
			shidi.dwMask = SHIDIM_FLAGS;
            shidi.dwFlags = SHIDIF_SIPDOWN | SHIDIF_SIZEDLGFULLSCREEN | SHIDIF_EMPTYMENU;
            shidi.hDlg = hDlg;
			SHInitDialog(&shidi);

			memset(&mbi, 0, sizeof(SHMENUBARINFO));
			mbi.cbSize     = sizeof(SHMENUBARINFO);
			mbi.hwndParent = hDlg;
			mbi.nToolBarId = IDR_MENU;
			mbi.hInstRes   = g_hInst;
			mbi.dwFlags = SHCMBF_HMENU;

			if (!SHCreateMenuBar(&mbi))
				g_hWndMenuBar = NULL;
			else
				g_hWndMenuBar = mbi.hwndMB;

            msg_handled = TRUE;

			refresh_status_wnd(current_session, hDlg);
			refresh_mainmenu();

			SetTimer(hDlg, IDT_UPDATE_STATUS, REFRESH_STATUS_WND_PERIOD, NULL);

            break;

		case WM_COMMAND:
            wmId    = LOWORD(wParam);
            wmEvent = HIWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
                case IDM_HELP_ABOUT:
                    DialogBox(g_hInst, (LPCTSTR)IDD_ABOUTBOX, hDlg, About);
					msg_handled = TRUE;
                    break;

				case IDM_COM_PORT:
					DialogBoxParam(g_hInst,
						(LPCTSTR)IDD_SELECT_COM_PORT,
						hDlg,
						select_com_port_callback,
						(LPARAM)&current_session
						);

					refresh_status_wnd(current_session, hDlg);
					refresh_mainmenu();

					msg_handled = TRUE;
					break;

				case IDM_DUMP_MEM:
					DialogBoxParam(g_hInst,
						(LPCTSTR)IDD_DUMP_MEM,
						hDlg,
						dump_mem_callback,
						(LPARAM)&current_session
						);
					msg_handled = TRUE;
					break;

				case IDM_CHANGE_GPS_MODE:
					DialogBoxParam(g_hInst,
						(LPCTSTR)IDD_CHANGE_GPS_MODE,
						hDlg,
						change_gps_mode_callback,
						(LPARAM)&current_session
						);
					msg_handled = TRUE;
					break;

				case IDM_PROGRAM_WORD:
					DialogBoxParam(g_hInst,
						(LPCTSTR)IDD_PROGRAM_WORD,
						hDlg,
						program_word_callback,
						(LPARAM)&current_session
						);
					msg_handled = TRUE;
					break;
				case IDM_ERASE_SECTOR:
					DialogBoxParam(g_hInst,
						(LPCTSTR)IDD_ERASE_SECTOR,
						hDlg,
						erase_sector_callback,
						(LPARAM)&current_session
						);
					msg_handled = TRUE;
					break;
				case IDM_PROGRAM_FLASH:
					DialogBoxParam(g_hInst,
						(LPCTSTR)IDD_PROGRAM_FLASH,
						hDlg,
						program_flash_callback,
						(LPARAM)&current_session
						);
					msg_handled = TRUE;
					break;
				case IDM_GET_FLASH_INFO:
					serial_session_req_flash_info(current_session);
					msg_handled = TRUE;
					break;

                case IDOK:
				case IDCANCEL:
				case IDABORT:
				case IDNO:
					KillTimer(hDlg, IDT_UPDATE_STATUS);
					EndDialog(hDlg, LOWORD(wParam));
					CommandBar_Destroy(g_hWndMenuBar);
					if (current_session)
						serial_session_close(current_session);
					PostQuitMessage(0);
					msg_handled = TRUE;
                    break;
            }
            break;
		case WM_TIMER:
			assert(wParam == IDT_UPDATE_STATUS);
			refresh_status_wnd(current_session, hDlg);
			logger_wnd_refresh_log(GetDlgItem(hDlg,IDC_LOG));

			break;

		case WM_ACTIVATE:
            // Notify shell of our activate message
            SHHandleWMActivate(hDlg, wParam, lParam, &s_sai, FALSE);
			msg_handled = TRUE;
            break;

        case WM_SETTINGCHANGE:
            SHHandleWMSettingChange(hDlg, wParam, lParam, &s_sai);
			msg_handled = TRUE;
            break;

    }

    return (INT_PTR)msg_handled;
}


// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            {
                // Create a Done button and size it.
                SHINITDLGINFO shidi;
                shidi.dwMask = SHIDIM_FLAGS;
                shidi.dwFlags = SHIDIF_DONEBUTTON | SHIDIF_SIPDOWN | SHIDIF_SIZEDLGFULLSCREEN | SHIDIF_EMPTYMENU;
                shidi.hDlg = hDlg;
                SHInitDialog(&shidi);

				SetDlgItemText(hDlg, IDC_VERSION, SIRFMEMDUMP_VERSION);
				SetDlgItemText(hDlg, IDC_HOMEPAGE, SIRFMEMDUMP_HOMEPAGE);				
            }
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(hDlg, message);
            return TRUE;

    }
    return (INT_PTR)FALSE;
}




int refresh_status_wnd(struct serial_session_t *s, HWND status_wnd)
{
	struct {
		TCHAR com_port[20];
		TCHAR proto[80];
		TCHAR rcvd[20];
		TCHAR errors[20];
		TCHAR sirf[20];
		TCHAR nmea[20];
	} stats;
	TCHAR old_res[80];
	
	assert(status_wnd);

	stats.com_port[0]=0;
	stats.proto[0]=0;
	stats.rcvd[0]=0;
	stats.errors[0]=0;
	stats.sirf[0]=0;
	stats.nmea[0]=0;

	if (s == NULL) {
		_sntprintf(stats.proto, sizeof(stats.proto)/sizeof(stats.proto[0]),
			TEXT("Not connected"));
	}else {
		if (serial_session_mtx_lock(s, 500) >= 0) {
			_sntprintf(stats.com_port,
				sizeof(stats.com_port)/sizeof(stats.com_port[0]),
				TEXT("%s (%u)"), s->port_name, (unsigned)s->baudrate);

			_tcsncpy(stats.proto,
				serial_session_proto_name(s),
				sizeof(stats.proto)/sizeof(stats.proto[0])
				);

			_sntprintf(stats.rcvd,
				sizeof(stats.rcvd)/sizeof(stats.rcvd[0]),
				TEXT("%lu"), (unsigned long)s->rcvd_bytes);

			_sntprintf(stats.errors,
				sizeof(stats.errors)/sizeof(stats.errors[0]),
				TEXT("%lu"), (unsigned long)s->comport_errors);

			_sntprintf(stats.sirf,
				sizeof(stats.sirf)/sizeof(stats.sirf[0]),
				TEXT("%lu"), (unsigned long)s->sirf_msg_cnt);

			_sntprintf(stats.nmea,
				sizeof(stats.nmea)/sizeof(stats.nmea[0]),
				TEXT("%lu"), (unsigned long)s->nmea_msg_cnt);
			serial_session_mtx_unlock(s);
		}else {
			_sntprintf(stats.proto, sizeof(stats.proto)/sizeof(stats.proto[0]),
				TEXT("Locked"));
		}
	}

	old_res[0]=0;
	GetDlgItemText(status_wnd, IDC_STATS_COM_PORT, old_res, sizeof(old_res)/sizeof(old_res[0]));
	if (_tcscmp(old_res, stats.com_port) != 0)
		SetDlgItemText(status_wnd, IDC_STATS_COM_PORT, stats.com_port);

	GetDlgItemText(status_wnd, IDC_STATS_PROTO, old_res, sizeof(old_res)/sizeof(old_res[0]));
	if (_tcscmp(old_res, stats.proto) != 0)
		SetDlgItemText(status_wnd, IDC_STATS_PROTO, stats.proto);

	GetDlgItemText(status_wnd, IDC_STATS_RECEIVED, old_res, sizeof(old_res)/sizeof(old_res[0]));
	if (_tcscmp(old_res, stats.rcvd) != 0)
		SetDlgItemText(status_wnd, IDC_STATS_RECEIVED, stats.rcvd);

	GetDlgItemText(status_wnd, IDC_STATS_ERRORS, old_res, sizeof(old_res)/sizeof(old_res[0]));
	if (_tcscmp(old_res, stats.errors) != 0)
		SetDlgItemText(status_wnd, IDC_STATS_ERRORS, stats.errors);
	
	GetDlgItemText(status_wnd, IDC_STATS_SIRF, old_res, sizeof(old_res)/sizeof(old_res[0]));
	if (_tcscmp(old_res, stats.sirf) != 0)
		SetDlgItemText(status_wnd, IDC_STATS_SIRF, stats.sirf);

	GetDlgItemText(status_wnd, IDC_STATS_NMEA, old_res, sizeof(old_res)/sizeof(old_res[0]));
	if (_tcscmp(old_res, stats.nmea) != 0)
		SetDlgItemText(status_wnd, IDC_STATS_NMEA, stats.nmea);

	return 0;
}

static int refresh_mainmenu()
{
	HMENU menu;
	const unsigned enabled_items[] = {
		IDM_LOADDUMPER,
		IDM_PING,
		IDM_GET_FLASH_INFO,
		IDM_CHANGE_GPS_MODE,
		IDM_DUMP_MEM,
		IDM_PROGRAM_WORD,
		IDM_ERASE_SECTOR,
		IDM_PROGRAM_FLASH,
		0
	};
	unsigned enabled;
	const unsigned *i;

	if (current_session != NULL)
		enabled = 0;
	else
		enabled =  MF_GRAYED;

	menu = (HMENU)SendMessage(g_hWndMenuBar, SHCMBM_GETMENU, 0, 0);

	if (menu == NULL)
		menu = (HMENU)SendMessage(g_hWndMenuBar, SHCMBM_GETSUBMENU, 0, 0);

	if (!menu) {
		logger_debug(TEXT("SendMessage(SHCMBM_GETSUBMENU) error"));
		return -1;
	}

	for(i=enabled_items; *i != 0; i++)
		EnableMenuItem(menu,  *i, MF_BYCOMMAND | enabled);

	return 0;
}

