#ifndef __DIRECT_SHOW_PLAYER_H__
#define __DIRECT_SHOW_PLAYER_H__

#include<Windows.h>
#include <d3d9.h>
#include <dshow.h>
#include <Vmr9.h>

#pragma comment (lib,"Strmiids.lib")

#define WM_GRAPH_EVENT (WM_APP+1)

typedef void (CALLBACK* GraphEventFN)(HWND hwnd, long eventCode, LONG_PTR param1, LONG_PTR param2);

int init_player();
int open_video(HWND hwnd, LPCWSTR file_name);
HRESULT update_video_window(HWND hwnd, const LPRECT prc);
HRESULT repaint(HWND hwnd, HDC hdc);
HRESULT DisplayModeChanged();
HRESULT play();
HRESULT set_volume(long volume);
HRESULT pause();
HRESULT stop();
HRESULT HandleGraphEvent(GraphEventFN pfnOnGraphEvent, HWND hwnd);
void CALLBACK OnGraphEvent(HWND hwnd, long evCode, LONG_PTR param1, LONG_PTR param2);
void uninit_player();

#endif