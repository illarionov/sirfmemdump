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

#define MAX_COMPORT_NUM 9
#define DEFAULT_COMPORTS_BUF 15

#define IDT_UPDATE_STATUS 1

struct comport_t {
	TCHAR *name;
	TCHAR *desc;
};

struct comports_t {
	/* selected com port */
	int selected;
	/* selected baudrate */
	unsigned baudrate;

	struct comport_t *ports;
	int buf_size;
};

static unsigned const baudrates[] = {
	1200,
	2400,
	4800,
	9600,
	38400,
	57600,
	115200,
	0
};

static int init_dialog_controls(HWND dialog, struct serial_session_t *s);
static int update_dialog_controls(HWND dialog, struct serial_session_t *s, unsigned disable_all);
static int populate_comport_combobox(struct comports_t *comports, HWND combobox);
static int populate_baudrate_combobox(struct comports_t *comports, HWND combobox);

static struct comports_t *comports_create();
static void comports_destroy(struct comports_t *comports);
static int comports_find_n_create(struct comports_t *comports, const TCHAR *name);

static int load_comports_from_registry(struct comports_t *comports);
static int read_active_driver_branch(struct comports_t *comports, HKEY key_h,  const TCHAR *branch_name);
static int read_driver_branch(struct comports_t *comports, HKEY key_h,  const TCHAR *branch_name);

static int open_com_port(HWND dialog, struct serial_session_t **p_session);
static int close_com_port(HWND dialog, struct serial_session_t **p_session);


struct comports_t *comports_create()
{
	unsigned i;
	int written;
	TCHAR buf[60];
	struct comports_t *comports;

	assert(DEFAULT_COMPORTS_BUF > MAX_COMPORT_NUM);

	comports = (struct comports_t *)malloc(sizeof(struct comports_t));
	if (comports == NULL)
		return NULL;
	comports->ports = (struct comport_t *)malloc(
		DEFAULT_COMPORTS_BUF*sizeof(struct comport_t));
	if (comports->ports == NULL) {
		free(comports);
		return NULL;
	}

	comports->buf_size = DEFAULT_COMPORTS_BUF;
	comports->baudrate = DEFAULT_BAUDRATE;

	for (i=0; i<=DEFAULT_COMPORTS_BUF; i++) {
		comports->ports[i].name = NULL;
		comports->ports[i].desc = NULL;
	}
	for (i=0; i<=MAX_COMPORT_NUM; i++) {
		written = _sntprintf(buf,
			sizeof(buf)/sizeof(buf[0]),
			TEXT("COM%u"),
			i);
		if (written <= 0) {
			comports_destroy(comports);
			return NULL;
		}
		comports->ports[i].name = _tcsdup(buf);
		if (comports->ports[i].name == NULL) {
			comports_destroy(comports);
			return NULL;
		}
	}

	comports->selected = comports_find_n_create(comports, DEFAULT_COMPORT);
	if (comports->selected == -1) {
		comports_destroy(comports);
		return NULL;
	}

	load_comports_from_registry(comports);

	return comports;
}

static void comports_destroy(struct comports_t *comports)
{
	int i;
	if (comports == NULL)
		return;
	assert(comports->buf_size > 0);
	for (i=0; i <= comports->buf_size; i++) {
		free(comports->ports[i].desc);
		free(comports->ports[i].name);
	}
	free(comports->ports);
	free(comports);
}

static int comports_find_n_create(struct comports_t *comports, const TCHAR *name)
{
	int i;

	assert(comports);
	assert(name);

	i=0;
	while (i<comports->buf_size)
	{
	   if (comports->ports[i].name == NULL)
		   break;
	   if (_tcsicmp(comports->ports[i].name, name) == 0)
			return i;
	   i++;
	}

	if (i == comports->buf_size) {
		/* realloc */
		unsigned j;
		unsigned new_size;
		struct comport_t *new_ports;

		new_size = comports->buf_size+5;
		new_ports = realloc(comports->ports, new_size*sizeof(comports->ports[0]));
		if (new_ports == NULL)
			return -1;

		for (j=comports->buf_size; j<new_size; j++) {
			new_ports[j].name = NULL;
			new_ports[j].desc = NULL;
		}
		comports->ports = new_ports;
		comports->buf_size = new_size;
	}

	comports->ports[i].name = _tcsdup(name);
	if (comports->ports[i].name == NULL)
		return -1;
	_tcsupr(comports->ports[i].name);

	return i;
}


static int read_active_driver_branch(struct comports_t *comports, HKEY key_h,  const TCHAR *branch_name)
{
	int res;
	HKEY branch_h;
	DWORD type;
	DWORD size;
	LONG ret_code;
	unsigned comport_num;
	TCHAR tmp[255];

	res = 0;

	ret_code = RegOpenKeyEx(key_h,
		branch_name,
		0,
		KEY_READ,
		&branch_h);
	if (ret_code != ERROR_SUCCESS)
		return -1;

	/* Name */
	size = sizeof(tmp);
	ret_code = RegQueryValueEx(branch_h,
		TEXT("Name"),
		NULL,
		&type,
		(LPBYTE)tmp, 
		&size);
	if (ret_code != ERROR_SUCCESS)
		goto read_active_driver_branch_exit;
	if (type != REG_SZ)
		goto read_active_driver_branch_exit;
	if (size <= 2)
		goto read_active_driver_branch_exit;
	if (_stscanf(tmp, TEXT("COM%u:"), &comport_num) != 1)
		goto read_active_driver_branch_exit;

	/* Key */
	size = sizeof(tmp);
	ret_code = RegQueryValueEx(branch_h,
		TEXT("Key"),
		NULL,
		&type,
		(LPBYTE)tmp,
		&size);
	if (ret_code != ERROR_SUCCESS)
		goto read_active_driver_branch_exit;
	if (type != REG_SZ)
		goto read_active_driver_branch_exit;
	if (size <= 2)
		goto read_active_driver_branch_exit;

	res = read_driver_branch(comports, HKEY_LOCAL_MACHINE, tmp);

read_active_driver_branch_exit:
	RegCloseKey (branch_h);
	return res;

}


static int read_driver_branch(struct comports_t *comports, HKEY key_h,  const TCHAR *branch_name)
{
	int res = 0;
	HKEY hKey;
	LONG ret_code;
	DWORD size;
	DWORD type;
	DWORD index;
	const TCHAR *desc;
	int port_idx;
	struct comport_t *port;
	TCHAR port_name[10];
	TCHAR tmp[255];

	ret_code = RegOpenKeyEx(key_h,
		branch_name,
		0,
		KEY_READ,
		&hKey);
	if (ret_code != ERROR_SUCCESS)
		return -1;

	/* Prefix */
	size = sizeof(tmp);
	ret_code = RegQueryValueEx(hKey,
		TEXT("Prefix"),
		NULL,
		&type,
		(LPBYTE)tmp,
		&size);
	if (ret_code != ERROR_SUCCESS)
		goto read_driver_subkey_exit;
	if (type != REG_SZ)
		goto read_driver_subkey_exit;
	if (size <= 2)
		goto read_driver_subkey_exit;
	if (_tcscmp(tmp, TEXT("COM")) != 0)
		goto read_driver_subkey_exit;

	/* Index */
	size = sizeof(index);
	ret_code = RegQueryValueEx(hKey,
		TEXT("Index"),
		NULL,
		&type,
		(LPBYTE)&index,
		&size
		);
	if (ret_code != ERROR_SUCCESS)
		goto read_driver_subkey_exit;
	if (type != REG_DWORD)
		goto read_driver_subkey_exit;

	/* FriendlyName */
	size = sizeof(tmp);
	ret_code = RegQueryValueEx(hKey,
		TEXT("FriendlyName"),
		NULL,
		&type,
		(LPBYTE)tmp,
		&size
		);
	if ( (ret_code == ERROR_SUCCESS)
		&& (type == REG_SZ)
		&& (size > 0))
		desc = tmp;
	else {
		desc = branch_name;
		size = sizeof(TCHAR)*(_tcslen(branch_name)+2);
	}

	_stprintf(port_name, TEXT("COM%u"), index);
	port_idx = comports_find_n_create(comports, port_name);
	if (port_idx < 0)
		goto read_driver_subkey_exit;

	port = &comports->ports[port_idx];
	free(port->desc);

	/* XXX */
	size += 20*sizeof(TCHAR);
	port->desc = (TCHAR *)malloc(size);
	if (port->desc == NULL)
		goto read_driver_subkey_exit;
	if (_sntprintf(port->desc,
		size / sizeof(TCHAR),
		TEXT("%s: %s"),
		port->name, desc) < 0)
			port->desc[size-1]=0;
	logger_debug(TEXT("Found COM port %s"), port->desc);

	res=1;

read_driver_subkey_exit:
	RegCloseKey (hKey);
	return res;
}

static int load_comports_from_registry(struct comports_t *comports)
{
	int res;
	unsigned i;
	LONG ret_code;
	HKEY builtin_drivers_key, active_drivers_key;
	DWORD dwSize;
	DWORD subkeys_cnt;
	DWORD max_subkey_len;
	DWORD max_value_len;
	TCHAR *key;

	assert(comports);

/* Search through active drivers */
	ret_code = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		TEXT("Drivers\\Active\\"),
		0,
		KEY_READ,
		&active_drivers_key);
	if (ret_code != ERROR_SUCCESS)
		return -1;

	ret_code = RegQueryInfoKey(
		active_drivers_key,
		NULL,
		NULL,
		NULL,
		&subkeys_cnt,
		&max_subkey_len,
		NULL,
		NULL,
		NULL,
		&max_value_len,
		NULL,
		NULL);

	if (ret_code != ERROR_SUCCESS) {
		RegCloseKey (active_drivers_key);
		return -1;
	}

	key = (TCHAR *)malloc(sizeof(TCHAR)*(max_subkey_len+1));
	if (key == NULL) {
		RegCloseKey (active_drivers_key);
		return -1;
	}

	for (i=0; i < subkeys_cnt; i++) {
		dwSize = max_subkey_len+1;
		ret_code = RegEnumKeyEx(active_drivers_key,
			i,
			key,
			&dwSize,
			NULL, NULL, NULL, NULL);

		if (ret_code  != ERROR_SUCCESS)
			continue;

		read_active_driver_branch(comports, active_drivers_key,  key);

	}
	free(key);
	RegCloseKey (active_drivers_key);

	/* Search throuch builtind drivers */
	ret_code = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		TEXT("Drivers\\BuiltIn\\"),
		0,
		KEY_READ,
		&builtin_drivers_key);
	if (ret_code != ERROR_SUCCESS)
		return -1;

	ret_code = RegQueryInfoKey(
		builtin_drivers_key,
		NULL,
		NULL,
		NULL,
		&subkeys_cnt,
		&max_subkey_len,
		NULL,
		NULL,
		NULL,
		&max_value_len,
		NULL,
		NULL);

	if (ret_code != ERROR_SUCCESS) {
		RegCloseKey (builtin_drivers_key);
		return -1;
	}

	key = (TCHAR *)malloc(sizeof(TCHAR)*(max_subkey_len+1));
	if (key == NULL) {
		RegCloseKey (builtin_drivers_key);
		return -1;
	}

	for (i=0; i < subkeys_cnt; i++) {
		dwSize = max_subkey_len+1;
		ret_code = RegEnumKeyEx(builtin_drivers_key,
			i,
			key,
			&dwSize,
			NULL, NULL, NULL, NULL);

		if (ret_code  != ERROR_SUCCESS)
			continue;

		read_driver_branch(comports, builtin_drivers_key,  key);

	}
	free(key);
	res = 0;
	RegCloseKey (builtin_drivers_key);

	return res;
}

static int populate_comport_combobox(struct comports_t *comports, HWND combobox)
{
	int i;
	int item_num;
	void *name;

	assert(comports);
	assert(comports->selected);
	assert(combobox);

	for (i=0; i <= comports->buf_size; i++)
	{
		if (comports->ports[i].name == NULL)
			break;

		if (comports->ports[i].desc)
			name = comports->ports[i].desc;
		else
			name = comports->ports[i].name;
		assert(name);

		item_num = SendMessage(combobox, CB_ADDSTRING, 0,
			(LPARAM)name);
		assert(item_num >= 0);

		if (i == comports->selected)
		{
			int res;
			res = SendMessage(combobox, CB_SETCURSEL, (WPARAM)item_num, 0);
			assert(res);
		}
	}

	return 0;
}

static int populate_baudrate_combobox(struct comports_t *comports, HWND combobox)
{
	int written;
	int item_num;
	const unsigned *b;
	TCHAR buf[20];

	for (b=baudrates; *b; b++)
	{
		written = _sntprintf(buf, sizeof(buf)/sizeof(buf[0]), TEXT("%u"), *b);
		assert(written > 0);
		item_num = SendMessage(combobox, CB_ADDSTRING, 0, (LPARAM)buf); 
		assert(item_num >= 0);
		if (*b == comports->baudrate)
		{
			int res;
			res = SendMessage(combobox, CB_SETCURSEL, (WPARAM)item_num, 0);
			assert(res);
		}
	}
	return 0;
}


static int init_dialog_controls(HWND dialog, struct serial_session_t *s)
{
	HWND combobox;
	struct comports_t *comports;

	comports = comports_create();

	if (comports == NULL)
		return -1;

	if (s) {
		comports->baudrate = s->baudrate;
		comports->selected = comports_find_n_create(comports, s->port_name);
		if (comports->selected == -1) {
			comports_destroy(comports);
			return -1;
		}
	}

	/* comport combobox */
	combobox = GetDlgItem(dialog, IDC_COM_PORT);
	assert(combobox);
	populate_comport_combobox(comports, combobox);

	/* baudrate combobox */
	combobox = GetDlgItem(dialog, IDC_BAUDRATE);
	assert(combobox);
	populate_baudrate_combobox(comports, combobox);

	update_dialog_controls(dialog, s, 0);

	comports_destroy(comports);

	return 0;
}

static int update_dialog_controls(HWND dialog, struct serial_session_t *s, unsigned disable_all)
{
	HWND btn;
	unsigned i;
	unsigned enable;

	struct
	{
		DWORD id;
		unsigned enable_when_session;
	} items[] =
	{
		{IDC_BUTTON_OPEN_COM_PORT,  0},
		{IDC_BUTTON_CLOSE_COM_PORT, 1},
		{IDC_COM_PORT, 0},
		{IDC_BAUDRATE, 0}
	};

	for (i=0; i<sizeof(items)/sizeof(items[0]); i++) {
		btn = GetDlgItem(dialog, items[i].id);
		assert(btn);
		if (disable_all)
			enable = 0;
		else
			enable = items[i].enable_when_session ?  s != NULL  : s == NULL;
		EnableWindow(btn, enable);
	}

	if (!disable_all)
		refresh_status_wnd(s, dialog);

	return 0;
}

static int open_com_port(HWND dialog, struct serial_session_t **p_session)
{
	unsigned baudrate;
	unsigned written;
	TCHAR comport_str[10];
	TCHAR *p;

	assert(p_session);
	assert(*p_session == NULL);

	/* read COM port */
	written = GetDlgItemText(dialog, IDC_COM_PORT, comport_str,
		sizeof(comport_str)/sizeof(comport_str[0]));
	if (written <= 0)
		comport_str[0]=0;
	comport_str[sizeof(comport_str)/sizeof(comport_str[0])-1]=0;
	for(p=comport_str; *p; p++) {
		if (!_istalnum(*p)) {
			*p = 0;
			break;
		}
	}

	if (comport_str[0] == 0) {
		MessageBox(dialog, TEXT("Wrong COM port"),
			NULL, MB_OK | MB_ICONERROR);
		return -1;
	}

	baudrate = GetDlgItemInt(dialog, IDC_BAUDRATE, NULL, FALSE);
	if (baudrate == 0) {
		MessageBox(dialog, TEXT("Wrong baudrate"),
			NULL, MB_OK | MB_ICONERROR);
		return -1;
	}

	*p_session = serial_session_create(comport_str, baudrate);
	if (*p_session == NULL) {
		MessageBox(dialog, TEXT("serial_session_create() error"),
			NULL, MB_OK | MB_ICONERROR);
		return -1;
	}

	logger_debug(TEXT("Connecting to %s (%u)..."), (*p_session)->port_name, (*p_session)->baudrate);

	if (serial_session_open(*p_session) < 0) {
		TCHAR err[160];
		serial_session_error(*p_session, err, sizeof(err)/sizeof(err[0]));
		MessageBox(dialog, err, NULL, MB_OK | MB_ICONERROR);
		serial_session_destroy(*p_session);
		*p_session = NULL;
		return -1;
	}

	return 0;
}

static int close_com_port(HWND dialog, struct serial_session_t **p_session)
{
	assert(p_session);
	assert(*p_session != NULL);

	logger_debug(TEXT("Disconnecting %s"), (*p_session)->port_name);
	serial_session_destroy(*p_session);
	*p_session = NULL;

	return 0;
}

// Message handler for Select com port box.
INT_PTR CALLBACK select_com_port_callback(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL msg_handled = FALSE;
	BOOL close_dialog = FALSE;
	SHINITDLGINFO shidi;
	struct serial_session_t **current_session;

    switch (message)
    {
        case WM_INITDIALOG:
			// Create a Done button and size it.
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
			SetTimer(dialog, IDT_UPDATE_STATUS, REFRESH_STATUS_WND_PERIOD, NULL);
			msg_handled = TRUE;
			break;

        case WM_COMMAND:
			current_session = (struct serial_session_t **)GetWindowLong(dialog, 
				DWL_USER);
			assert(current_session);
			switch (LOWORD(wParam))
			{
				case IDOK:
					msg_handled = close_dialog = TRUE;
					break;

				case IDC_BUTTON_OPEN_COM_PORT:
					update_dialog_controls(dialog, *current_session, 1);
					SetDlgItemText(dialog, IDC_SELECT_COMPORT_STATUS, TEXT("Connecting...\n"));
					UpdateWindow(dialog);
					open_com_port(dialog, current_session);
					update_dialog_controls(dialog, *current_session, 0);
					msg_handled = TRUE;
					break;

				case IDC_BUTTON_CLOSE_COM_PORT:
					update_dialog_controls(dialog, *current_session, 1);
					UpdateWindow(dialog);
					close_com_port(dialog, current_session);
					update_dialog_controls(dialog, *current_session, 0);
					msg_handled = TRUE;
					break;
			}

            break;
		case WM_TIMER:
			current_session = (struct serial_session_t **)GetWindowLong(dialog, 
				DWL_USER);

			assert(wParam == IDT_UPDATE_STATUS);
			assert(current_session);

			refresh_status_wnd(*current_session, dialog);

			break;

        case WM_CLOSE:
            msg_handled = close_dialog = TRUE;
			break;

    }

	if (close_dialog) {
		KillTimer(dialog, IDT_UPDATE_STATUS);
		EndDialog(dialog, LOWORD(wParam));
	}

    return (INT_PTR)msg_handled;
}

