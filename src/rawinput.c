// Raw mouse input: the game reads its mouse by the cursor (GetCursorPos each frame, the difference from the window's centre, then SetCursorPos back to the centre: the poll at rva
// 0x28ed00), so the view turns by what Windows has made of the movement (pointer speed, "enhance pointer precision") and by whatever the cursor could not do at a screen edge.
// Here a thread of its own registers the mouse for raw input and sums the movement; the poll's call of the consumer of the mouse movement (rva 0xcfc30, at 0x28ee87: x, y, dx, dy)
// gets the raw movement in place of the cursor's. The cursor handling stays as the game has it (menus read x and y). When no raw movement came while the cursor moved (a remote
// desktop sends absolute positions only) the cursor's delta is used.
// [input] rawmouse=0 in settings.ini keeps the game's own way.
#include <windows.h>
#include <stdint.h>

#include "hook.h"
#include "log.h"
#include "rawinput.h"

#ifdef _WIN64
#define MOUSE_POLL_CALL 0x28ee87
static volatile LONG raw_dx, raw_dy;
static volatile LONG raw_events;
static uint64_t (*consumer_original)(int, int, int, int);

static LRESULT CALLBACK raw_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_INPUT) {
		RAWINPUT in;
		UINT size = sizeof(in);
		if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &in, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && in.header.dwType == RIM_TYPEMOUSE &&
		    !(in.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
			InterlockedAdd(&raw_dx, in.data.mouse.lLastX);
			InterlockedAdd(&raw_dy, in.data.mouse.lLastY);
			InterlockedIncrement(&raw_events);
		}
		return 0;
	}
	return DefWindowProcA(w, msg, wp, lp);
}

static DWORD WINAPI raw_thread(LPVOID unused)
{
	(void)unused;
	WNDCLASSA wc = {0};
	wc.lpfnWndProc = raw_proc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "mw2coop_rawinput";
	RegisterClassA(&wc);
	HWND w = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
	RAWINPUTDEVICE d = {0x01, 0x02, RIDEV_INPUTSINK, w}; // generic desktop, mouse; also while another window has the focus (the poll only runs for the game's foreground window)
	if (!w || !RegisterRawInputDevices(&d, 1, sizeof(d))) {
		log_printf("rawinput: could not register the mouse (error %lu)", GetLastError());
		return 0;
	}
	MSG m;
	while (GetMessageA(&m, NULL, 0, 0) > 0)
		DispatchMessageA(&m);
	return 0;
}

// the poll's call of the consumer: the cursor's delta replaced by the raw movement that came since the last poll
static uint64_t consumer(int x, int y, int dx, int dy)
{
	static LONG seen;
	LONG events = raw_events;
	LONG rdx = InterlockedExchange(&raw_dx, 0), rdy = InterlockedExchange(&raw_dy, 0);
	if (events != seen)
		dx = (int)rdx, dy = (int)rdy;
	seen = events;
	return consumer_original(x, y, dx, dy);
}

void rawinput_install(void)
{
	char ini[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, ini, MAX_PATH);
	while (n > 0 && ini[n - 1] != 0x5c)
		n--;
	ini[n] = 0;
	lstrcatA(ini, "mw2coop\\settings.ini");
	if (!GetPrivateProfileIntA("input", "rawmouse", 1, ini))
		return;
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	if (!hook_call(base + MOUSE_POLL_CALL, (void *)consumer, (void **)&consumer_original)) {
		log_printf("rawinput: the mouse poll is not as expected, the game keeps its own mouse reading");
		return;
	}
	HANDLE t = CreateThread(NULL, 0, raw_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	log_printf("rawinput: raw mouse movement in place of the cursor's");
}
#else
void rawinput_install(void)
{
}
#endif
