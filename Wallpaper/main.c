#include"player.h"

static HWND worker_w = NULL;

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

void OnPaint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC hdc;

	hdc = BeginPaint(hwnd, &ps);

	repaint(hwnd, hdc);

	EndPaint(hwnd, &ps);
}

void OnSize(HWND hwnd)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	update_video_window(hwnd, &rc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
		InitAllArgs(hwnd);
		open_video(hwnd, L"D:\\ffmpeg4.4\\bin\\2.avi");
		break;
	case WM_DISPLAYCHANGE:
		DisplayModeChanged();
		break;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		OnPaint(hwnd);
		play();
		return 0;
	case WM_SIZE:
		OnSize(hwnd);
		return 0;
	case WM_GRAPH_EVENT:
		HandleGraphEvent(OnGraphEvent, hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow)
{
	static TCHAR szAppName[] = TEXT("wallpaper");
	HWND hwnd;
	MSG msg;
	WNDCLASS wnd_class;

	init_player();

	wnd_class.style = CS_HREDRAW | CS_VREDRAW;
	wnd_class.lpfnWndProc = WndProc;
	wnd_class.cbClsExtra = 0;
	wnd_class.cbWndExtra = 0;
	wnd_class.hInstance = hInstance;

	wnd_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wnd_class.hCursor = LoadCursor(NULL, IDC_ARROW);
	wnd_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wnd_class.lpszMenuName = NULL;
	wnd_class.lpszClassName = szAppName;

	if (!RegisterClass(&wnd_class))
	{
		MessageBox(NULL, TEXT("此程序必须运行在NT下!"), szAppName, MB_ICONERROR);
		return 0;
	}

	hwnd = CreateWindow(szAppName, NULL, WS_DLGFRAME | WS_THICKFRAME | WS_POPUP,
		0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
		NULL, NULL, hInstance, NULL);
	ShowWindow(hwnd, SW_SHOWMAXIMIZED);
	UpdateWindow(hwnd);
	ShowCursor(FALSE);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	ShowCursor(TRUE); //显示鼠标光标 

	uninit_player();

	return msg.wParam;
}