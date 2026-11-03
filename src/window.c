// Dark title bar and square corners on the game's window. DwmSetWindowAttribute is looked up at run time (older Windows has no such attribute: the calls then just fail).
// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 (19 on Windows 10 before 20H1), DWMWA_WINDOW_CORNER_PREFERENCE is 33 (Windows 11), DWMWCP_DONOTROUND is 1.
// A window without WS_CAPTION (borderless, full screen) has no title bar and no frame the mod could draw wrongly: it is not touched.
#include <windows.h>

#include "log.h"
#include "settings.h"
#include "window.h"

typedef HRESULT(WINAPI *dwm_set_t)(HWND, DWORD, LPCVOID, DWORD);

static BOOL CALLBACK find_game_window(HWND h, LPARAM out)
{
	DWORD pid;
	char cls[32];
	GetWindowThreadProcessId(h, &pid);
	if (pid == GetCurrentProcessId() && GetClassNameA(h, cls, sizeof(cls)) && lstrcmpA(cls, "IW4") == 0) {
		*(HWND *)out = h;
		return FALSE;
	}
	return TRUE;
}

static void apply(HWND w, dwm_set_t set)
{
	BOOL dark = TRUE;
	DWORD round = 1; // DWMWCP_DONOTROUND
	HRESULT a = set(w, 20, &dark, sizeof(dark));
	if (FAILED(a))
		a = set(w, 19, &dark, sizeof(dark));
	HRESULT b = set(w, 33, &round, sizeof(round));
	log_printf("window: dark title bar %s, square corners %s", SUCCEEDED(a) ? "on" : "not available", SUCCEEDED(b) ? "on" : "not available");
}

static DWORD WINAPI window_thread(LPVOID unused)
{
	(void)unused;
	HMODULE dwm = LoadLibraryA("dwmapi.dll");
	dwm_set_t set = dwm ? (dwm_set_t)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute") : NULL;
	if (!set) {
		log_printf("window: no dwmapi, title bar left as it is");
		return 0;
	}
	HWND last = NULL;
	LONG last_style = 0;
	for (;;) {
		HWND w = NULL;
		EnumWindows(find_game_window, (LPARAM)&w);
		if (w) {
			LONG style = GetWindowLongA(w, GWL_STYLE);
			if (w != last || style != last_style) {
				if (style & WS_CAPTION)
					apply(w, set);
				last = w;
				last_style = style;
			}
		}
		Sleep(1000);
	}
}

void window_install(void)
{
	if (!settings_darktitle())
		return;
	CreateThread(NULL, 0, window_thread, NULL, 0, NULL);
}
