#include"player.h"
#include "resource.h"

#define IDR_QUIT 0x1
#define IDR_FULLSCREEN_PAUSE 0x2
#define IDR_PLAY_PAUSE 0x4
#define IDR_START_STOP 0x8
#define IDR_SWITCH 0x16
#define IDR_AUDIO 0x32
#define IDR_AUTORESTART 0x64
#define IDR_FILESTART 0x100

typedef enum video_state_enum {
	play_state, pause_state
} video_state_e;
typedef enum wallpaper_state_enum {
	wallpaper_state, system_state
} wallpaper_state_e;

static TCHAR szAppName[] = TEXT("wallpaper");
static HWND worker_w = NULL;
static UINT WM_TASKBARCREATED = 0x0;
static HMENU h_menu;
static video_state_e video_state = play_state;
static wallpaper_state_e wallpaper = wallpaper_state;
static BOOL bAudio = TRUE;
static BOOL bAutoRun = FALSE;
static BOOL bFullScreenPause = FALSE;
static WCHAR file_name[MAX_PATH];
static WCHAR current_path[MAX_PATH];

static BOOL CALLBACK EnumWindowsProCallback(HWND hwnd, LPARAM lParam)
{
	HWND hwnd_shell_dll = FindWindowEx(hwnd, NULL, L"SHELLDLL_DefView", NULL);
	if (hwnd_shell_dll != NULL)
	{
		worker_w = FindWindowEx(NULL, hwnd, L"WorkerW", NULL);
	}

	return TRUE;
}

static int InitAllArgs(HWND hwnd)
{
	HWND  hwnd_proman = FindWindow(L"Progman", L"Program Manager");
	if (hwnd_proman == NULL)
		return -1;

	// Send 0x052C to Progman. This message directs Progman to spawn a 
	// WorkerW behind the desktop icons. If it is already there, nothing 
	// happens.
	DWORD_PTR result = NULL;
	SendMessageTimeout(hwnd_proman, 0x052c, NULL, NULL, SMTO_NORMAL, 1000, &result);

	Sleep(1000);

	EnumWindows(EnumWindowsProCallback, NULL);

	if (worker_w == NULL)
		return -1;

	SetParent(hwnd, worker_w);

	return 0;
}

static void OnPaint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC hdc;

	hdc = BeginPaint(hwnd, &ps);

	repaint(hwnd, hdc);

	EndPaint(hwnd, &ps);
}

static void OnSize(HWND hwnd)
{
	RECT rc;

	GetClientRect(hwnd, &rc);
	update_video_window(hwnd, &rc);
}

static void enum_all_avi_videos(HMENU sub_menu)
{
	WIN32_FIND_DATAW wfd;
	WCHAR path_buffer[MAX_PATH];
	int item_count = 0;
	int i = 0;
	int index = IDR_FILESTART;

	if (!sub_menu)
		return;

	item_count = GetMenuItemCount(sub_menu);

	if (item_count > 0)
	{
		for (i = 0; i < item_count; i++)
			DeleteMenu(sub_menu, 0, MF_BYPOSITION);
	}

	item_count = GetMenuItemCount(sub_menu);

	if (!lstrcpy(path_buffer, current_path))
		return -1;

	if (lstrcat(path_buffer, L"\\cache\\*.*") == NULL)
		return -1;

	HANDLE hFile = FindFirstFile(path_buffer, &wfd);
	while (hFile != INVALID_HANDLE_VALUE)
	{
		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			LPWSTR extStr = PathFindExtension(wfd.cFileName);

			if (extStr && 0 == _wcsicmp(extStr, L".avi"))
			{
				AppendMenu(sub_menu, MF_STRING, index++, wfd.cFileName);
				if (_wcsicmp(wfd.cFileName, file_name) == 0)
					CheckMenuItem(sub_menu, index - 1, MF_CHECKED);
			}
		}

		if (!FindNextFile(hFile, &wfd))
			break;
	}

	item_count = GetMenuItemCount(sub_menu);
	if (item_count > 0)
		AppendMenu(sub_menu, MF_SEPARATOR, index, NULL);
	AppendMenu(sub_menu, MF_STRING, IDR_SWITCH, L"选择视频文件");
}

static int set_auto_run()
{
	WCHAR path_buffer[MAX_PATH];
	HKEY hKey;
	int result = -1;

	if (GetModuleFileName(NULL, path_buffer, MAX_PATH) <= 0)
		return result;

	LSTATUS res = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS | KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey);
	if (res == ERROR_SUCCESS)
	{
		if (RegSetValueEx(hKey, L"Wallpaper", 0, REG_SZ, path_buffer, (lstrlen(path_buffer) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS)
			result = 0;
	}

	RegCloseKey(hKey);
	return result;
}

static int unset_auto_run()
{
	HKEY hKey;
	int result = -1;

	LSTATUS res = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &hKey);
	if (res == ERROR_SUCCESS)
	{
		if (RegDeleteValue(hKey, L"Wallpaper") == ERROR_SUCCESS)
			result = 0;
	}

	RegCloseKey(hKey);
	return result;
}

static int auto_run_state()
{
	HKEY hKey;
	int result = -1;
	DWORD dwType = REG_SZ;

	LSTATUS res = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &hKey);
	if (res == ERROR_SUCCESS)
	{
		if (RegQueryValueEx(hKey, L"Wallpaper", NULL, &dwType, NULL, NULL) == ERROR_SUCCESS)
			result = 0;
	}

	RegCloseKey(hKey);
	return result;
}

static void init_menu(HINSTANCE hInstance, HWND hwnd)
{
	NOTIFYICONDATA notify_icon_data;

	notify_icon_data.cbSize = sizeof(notify_icon_data);
	notify_icon_data.hWnd = hwnd;
	notify_icon_data.uID = 0;
	notify_icon_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	notify_icon_data.uCallbackMessage = WM_USER;
	notify_icon_data.hIcon = LoadIcon(hInstance, IDI_ICON1);
	lstrcpy(notify_icon_data.szTip, szAppName);

	Shell_NotifyIcon(NIM_ADD, &notify_icon_data);

	h_menu = CreatePopupMenu();

	AppendMenu(h_menu, MF_STRING, IDR_PLAY_PAUSE, L"暂停");
	AppendMenu(h_menu, MF_STRING, IDR_AUDIO, L"静音");
	if (!bAudio)
		CheckMenuItem(h_menu, IDR_AUDIO, MF_CHECKED);
	HMENU sub_menu = CreatePopupMenu();
	AppendMenu(h_menu, MF_POPUP, sub_menu, L"切换视频文件");
	AppendMenu(h_menu, MF_STRING, IDR_START_STOP, wallpaper == wallpaper_state ? L"使用原始壁纸" : L"使用动态壁纸");
	AppendMenu(h_menu, MF_STRING, IDR_AUTORESTART, L"开机自启");
	if (bAutoRun)
		CheckMenuItem(h_menu, IDR_AUTORESTART, MF_CHECKED);
	AppendMenu(h_menu, MF_STRING, IDR_FULLSCREEN_PAUSE, L"全屏暂停");
	if (bFullScreenPause)
		CheckMenuItem(h_menu, IDR_FULLSCREEN_PAUSE, MF_CHECKED);
	AppendMenu(h_menu, MF_STRING, IDR_QUIT, L"退出");
}

static void restore_wallpaper()
{
	char path[1024];
	memset(path, 0x0, 1024);
	SystemParametersInfo(SPI_GETDESKWALLPAPER, 1024, path, NULL);
	SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, path, NULL);
}

static void play_video(HWND hwnd)
{
	WCHAR path_buffer[MAX_PATH];

	memset(path_buffer, 0x0, MAX_PATH * sizeof(WCHAR));

	if (lstrcpy(path_buffer, current_path) &&
		lstrcat(path_buffer, L"\\cache\\") &&
		lstrcat(path_buffer, file_name))
	{
		stop();
		ShowWindow(hwnd, SW_HIDE);
		open_video(hwnd, path_buffer);
		ShowWindow(hwnd, SW_SHOWMAXIMIZED);
		play();
		set_volume(bAudio ? 0 : -10000);
	}
}

static void OnFileOpen(HWND hwnd)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	WCHAR path_buffer[MAX_PATH];
	memset(path_buffer, 0x0, MAX_PATH * sizeof(WCHAR));

	WCHAR szFileName[MAX_PATH];
	szFileName[0] = L'\0';

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.hInstance = GetModuleHandle(NULL);
	ofn.lpstrFilter = L"AVI Files(*.avi)";
	ofn.lpstrFile = szFileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST;

	if (GetOpenFileName(&ofn))
	{
		LPWSTR m_file_name = PathFindFileName(szFileName);
		if (lstrcpy(path_buffer, current_path) &&
			lstrcat(path_buffer, L"\\cache\\")
			&& lstrcat(path_buffer, m_file_name))
			MoveFile(szFileName, path_buffer);
	}
}

static void save_config_file()
{
	WCHAR config[MAX_PATH * 2];
	DWORD len = 0;
	ZeroMemory(config, sizeof(config));
	lstrcat(config, bAudio ? L"1," : L"0,");
	lstrcat(config, wallpaper == wallpaper_state ? L"1," : L"0,");
	lstrcat(config, bFullScreenPause ? L"1," : L"0,");
	lstrcat(config, file_name[0] == 0x0 ? L"NULL" : file_name);

	HANDLE hFile = CreateFile(L"config.cfg", GENERIC_WRITE, FILE_SHARE_READ,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
		return;

	SetFilePointer(hFile, 0, 0, FILE_BEGIN);
	WriteFile(hFile, config, lstrlen(config) * sizeof(WCHAR), &len, NULL);
	SetEndOfFile(hFile);

	CloseHandle(hFile);
}

static void show_menu(HWND hwnd)
{
	POINT pt;
	BOOL SubItem;

	ShowCursor(TRUE);

	GetCursorPos(&pt);
	SetForegroundWindow(hwnd);
	HMENU sub_menu = GetSubMenu(h_menu, 2);
	enum_all_avi_videos(sub_menu);
	SubItem = TrackPopupMenu(h_menu, TPM_RETURNCMD, pt.x, pt.y, NULL, hwnd, NULL);

	switch (SubItem)
	{
	case IDR_QUIT:
		DestroyWindow(hwnd);
		break;
	case IDR_PLAY_PAUSE:
		video_state = video_state == play_state ? pause_state : play_state;
		ModifyMenu(h_menu, IDR_PLAY_PAUSE, MF_STRING, IDR_PLAY_PAUSE, video_state == play_state ? L"暂停" : L"播放");
		if (video_state == play_state)
			play();
		else
			pause();
		break;
	case IDR_AUDIO:
		bAudio = !bAudio;
		CheckMenuItem(h_menu, IDR_AUDIO, bAudio ? MF_UNCHECKED : MF_CHECKED);
		set_volume(bAudio ? 0 : -10000);
		break;
	case IDR_START_STOP:
		wallpaper = wallpaper == wallpaper_state ? system_state : wallpaper_state;
		ModifyMenu(h_menu, IDR_START_STOP, MF_STRING, IDR_START_STOP, wallpaper == wallpaper_state ? L"使用原始壁纸" : L"使用动态壁纸");
		if (wallpaper == wallpaper_state)
		{
			play_video(hwnd);
		}
		else
		{
			stop();
			ShowWindow(hwnd, SW_HIDE);
			restore_wallpaper();
		}
		break;
	case IDR_AUTORESTART:
	{
		bAutoRun = !bAutoRun;
		if ((bAutoRun ? set_auto_run() : unset_auto_run()) >= 0)
			CheckMenuItem(h_menu, IDR_AUTORESTART, bAutoRun ? MF_CHECKED : MF_UNCHECKED);
	}
	break;
	case IDR_SWITCH:
		// 切换视频文件
		OnFileOpen(hwnd);
		break;
	case IDR_FULLSCREEN_PAUSE:
		bFullScreenPause = !bFullScreenPause;
		CheckMenuItem(h_menu, IDR_FULLSCREEN_PAUSE, bFullScreenPause ? MF_CHECKED : MF_UNCHECKED);
		break;
	default:
		if (SubItem >= IDR_FILESTART)
		{
			memset(file_name, 0x0, MAX_PATH * sizeof(WCHAR));

			GetMenuString(sub_menu, SubItem, file_name, MAX_PATH, MF_BYCOMMAND);
			CheckMenuItem(sub_menu, SubItem, MF_CHECKED);

			play_video(hwnd);
		}
	}

	if (SubItem == 0)
		PostMessage(hwnd, WM_LBUTTONDOWN, NULL, NULL);

	ShowCursor(FALSE);
	DrawMenuBar(hwnd);
	save_config_file();
}

static int init_config()
{
	WIN32_FIND_DATA  wfd;
	WCHAR path_buffer[MAX_PATH];
	WCHAR config[MAX_PATH * 2];
	DWORD len = 0;
	int i = 0;
	ZeroMemory(config, sizeof(config));

	if (GetModuleFileName(NULL, current_path, MAX_PATH) <= 0)
		return -1;

	if (!PathRemoveFileSpec(current_path))
		return -1;

	lstrcpy(path_buffer, current_path);

	if (lstrcat(path_buffer, L"\\cache") == NULL)
		return -1;

	HANDLE hFind = FindFirstFile(path_buffer, &wfd);
	if ((hFind != INVALID_HANDLE_VALUE) && (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		FindClose(hFind);
	}
	else
	{
		if (CreateDirectory(path_buffer, NULL) == NULL)
			return -1;
	}

	HANDLE hFile = CreateFile(L"config.cfg", GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE &&
		ReadFile(hFile, config, MAX_PATH * 2, &len, NULL))
	{
		if (config[0] != 0x0)
		{
			bAudio = config[0] == '1';
			wallpaper = config[2] == '1' ? wallpaper_state : system_state;
			bFullScreenPause = config[4] == '1';
			if (lstrcmpi(&config[6], L"NULL"))
				lstrcpy(file_name, &config[6]);
		}
	}



	bAutoRun = auto_run_state() == 0;

	CloseHandle(hFile);
	return 0;
}

static int listen_fullscreen_msg(HWND hwnd)
{
	APPBARDATA abd;
	abd.cbSize = sizeof(APPBARDATA);
	abd.uCallbackMessage = WM_FULLSCREEN;
	abd.hWnd = hwnd;
	SHAppBarMessage(ABM_NEW, &abd);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
		InitAllArgs(hwnd);
		init_menu(((LPCREATESTRUCTW)lParam)->hInstance, hwnd);
		listen_fullscreen_msg(hwnd);
		break;
	case WM_USER:
		if (lParam == WM_RBUTTONDOWN)
			show_menu(hwnd);
		break;
	case WM_DISPLAYCHANGE:
		DisplayModeChanged();
		break;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		OnPaint(hwnd);
		return 0;
	case WM_SIZE:
		OnSize(hwnd);
		return 0;
	case WM_GRAPH_EVENT:
		HandleGraphEvent(OnGraphEvent, hwnd);
		return 0;
	case WM_FULLSCREEN:
		if (((UINT)wParam) == ABN_FULLSCREENAPP && bFullScreenPause)
		{
			if (TRUE == (BOOL)lParam)
				pause();
			else
				play();
		}
		break;
	case WM_DESTROY:
	{
		NOTIFYICONDATA nid;
		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
	}
	break;
	case WM_POWERBROADCAST:
		if (wParam == PBT_POWERSETTINGCHANGE &&
			strcmp(&((PPOWERBROADCAST_SETTING)lParam)->PowerSetting, &GUID_ACDC_POWER_SOURCE) == 0
			&& wallpaper == wallpaper_state
			&& file_name[0] != 0x0)
		{
			if (((SYSTEM_POWER_CONDITION) * ((PPOWERBROADCAST_SETTING)lParam)->Data) == PoAc)
			{
				play_video(hwnd);
			}
			if (((SYSTEM_POWER_CONDITION) * ((PPOWERBROADCAST_SETTING)lParam)->Data) == PoDc)
			{
				stop();
				ShowWindow(hwnd, SW_HIDE);
				restore_wallpaper();
			}
		}
		break;
	default:
		if (message == WM_TASKBARCREATED)
			SendMessage(hwnd, WM_CREATE, wParam, lParam);

		break;
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow)
{
	HWND hwnd;
	MSG msg;
	WNDCLASS wnd_class;
	HPOWERNOTIFY notify;

	if (init_config() < 0 || init_player() < 0)
	{
		MessageBox(NULL, TEXT("程序加载失败!"), szAppName, MB_ICONERROR);
		return 0;
	}

	// ffmpeg -i input.mp4 -c:v libx264 -c:a libmp3lame -b:a 384K output.avi
	// 崩溃重启消息
	WM_TASKBARCREATED = RegisterWindowMessage(TEXT("TaskbarCreated"));

	wnd_class.style = CS_HREDRAW | CS_VREDRAW;
	wnd_class.lpfnWndProc = WndProc;
	wnd_class.cbClsExtra = 0;
	wnd_class.cbWndExtra = 0;
	wnd_class.hInstance = hInstance;

	wnd_class.hIcon = LoadIcon(hInstance, IDI_ICON1);
	wnd_class.hCursor = LoadCursor(NULL, IDC_ARROW);
	wnd_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wnd_class.lpszMenuName = NULL;
	wnd_class.lpszClassName = szAppName;

	if (!RegisterClass(&wnd_class))
	{
		MessageBox(NULL, TEXT("此程序必须运行在NT下!"), szAppName, MB_ICONERROR);
		return 0;
	}

	hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, szAppName, NULL, WS_DLGFRAME | WS_THICKFRAME | WS_POPUP,
		0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
		NULL, NULL, hInstance, NULL);
	ShowWindow(hwnd, SW_SHOWMAXIMIZED);
	UpdateWindow(hwnd);
	ShowCursor(FALSE);

	notify = RegisterPowerSettingNotification(hwnd, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);

	//RedrawWindow(hwnd,NULL)
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	ShowCursor(TRUE);

	uninit_player();
	UnregisterPowerSettingNotification(notify);

	restore_wallpaper();

	return msg.wParam;
}