// Dark title bar and square corners on the game's window. DwmSetWindowAttribute is looked up at run time (older Windows has no such attribute: the calls then just fail).
// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 (19 on Windows 10 before 20H1), DWMWA_WINDOW_CORNER_PREFERENCE is 33 (Windows 11), DWMWCP_DONOTROUND is 1.
// A window without WS_CAPTION (borderless, full screen) has no title bar and no frame the mod could draw wrongly: it is not touched.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "native.h"
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

// Ultrawide safe area. The game places the menus inside a "safe area" that is a fraction of the screen's width (the dvars safeArea_horizontal and
// safeArea_adjusted_horizontal), so on a screen wider than 16:9 the same fraction is a wider strip and the UI sits further out. The fraction is lowered by 16:9 / the ratio,
// which keeps the strip as wide as on 16:9. The value the player has set is the one for 16:9 (the "base"); the game saves its dvar, so the lowered value would come back as the next
// start's base: mw2coop\safearea.ini keeps the base and the value written, and a dvar that still holds the written value is the lowered one.
#define SAFE_DVARS 2
static const char *const safe_names[SAFE_DVARS] = {"safeArea_horizontal", "safeArea_adjusted_horizontal"};

typedef struct {
	int known;
	float base;    // the player's value (for 16:9)
	float written; // what this module put into the dvar last
	int fights, left; // the game putting the same value back again and again; left: the dvar is the game's for the rest of the run
	DWORD fight_since;
} SafeState;

static char safe_ini[MAX_PATH];

static float safe_read(const char *key, float fallback)
{
	char b[32];
	GetPrivateProfileStringA("safearea", key, "", b, sizeof(b), safe_ini);
	return b[0] ? (float)atof(b) : fallback;
}

static void safe_write(const char *key, float v)
{
	char b[32];
	snprintf(b, sizeof(b), "%.5f", v);
	WritePrivateProfileStringA("safearea", key, b, safe_ini);
}

static int safe_close(float a, float b)
{
	return fabsf(a - b) < 0.0005f;
}

static void safe_set(dvar_t *d, float v)
{
	d->current.value = v;
	d->latched.value = v;
	d->modified = 1;
}

static void safe_apply(float aspect)
{
	static SafeState st[SAFE_DVARS];
	const float wide = 16.0f / 9.0f;
	float factor = aspect > wide + 0.02f ? wide / aspect : 1.0f;
	if (factor < 0.5f)
		factor = 0.5f;
	for (int i = 0; i < SAFE_DVARS; i++) {
		dvar_t *d = game_find_dvar(safe_names[i]);
		if (!d || d->type != DVAR_TYPE_FLOAT || st[i].left)
			continue;
		float cur = d->current.value;
		int dirty = 0;
		char key_base[48], key_written[48];
		snprintf(key_base, sizeof(key_base), "%s_base", safe_names[i]);
		snprintf(key_written, sizeof(key_written), "%s_written", safe_names[i]);
		if (!st[i].known) {
			float w = safe_read(key_written, -1.0f), b = safe_read(key_base, -1.0f);
			st[i].base = (w >= 0.0f && b > 0.0f && safe_close(cur, w)) ? b : cur; // (the game gave back what we wrote last time: the base is the one kept)
			st[i].written = cur;
			st[i].known = 1;
			dirty = 1;
			log_printf("window: %s is %.3f, the 16:9 value %.3f", safe_names[i], cur, st[i].base);
		} else if (!safe_close(cur, st[i].written)) {
			// The game itself can put a value back (safeArea_adjusted_horizontal is set again every quarter second in some states): taken for the
			// player's each time, it was lowered again and logged twice a quarter second for as long as the game ran, and the log that never stood still
			// hid a hung game from the QA's hang watch (audit night 2026-11-18). The same value back three times in 10 s: left to the game.
			const DWORD now = GetTickCount();
			if (safe_close(cur, st[i].base)) {
				if (!st[i].fights || now - st[i].fight_since > 10000)
					st[i].fights = 0, st[i].fight_since = now;
				if (++st[i].fights >= 3) {
					st[i].left = 1;
					log_printf("window: the game keeps putting %s back to %.3f: left to the game for this run", safe_names[i], cur);
					continue;
				}
			}
			st[i].base = cur; // set by the player (an options page, the console, a config): this is the value for 16:9 now
			st[i].written = cur;
			dirty = 1;
			log_printf("window: %s set to %.3f: taken as the 16:9 value", safe_names[i], cur);
		}
		float target = st[i].base * factor;
		if (!safe_close(target, cur)) {
			safe_set(d, target);
			st[i].written = target;
			dirty = 1;
			log_printf("window: %s %.3f -> %.3f (screen ratio %.3f, 16:9 value %.3f)", safe_names[i], cur, target, aspect, st[i].base);
		}
		if (dirty) {
			safe_write(key_base, st[i].base);
			safe_write(key_written, st[i].written);
		}
	}
}

static DWORD WINAPI safearea_thread(LPVOID unused)
{
	(void)unused;
	char *slash;
	GetModuleFileNameA(NULL, safe_ini, MAX_PATH);
	slash = strrchr(safe_ini, '\\');
	if (slash)
		lstrcpynA(slash + 1, "mw2coop\\safearea.ini", (int)(MAX_PATH - (slash + 1 - safe_ini)));
	// The standard ratios of the game's video options (4:3, 16:10, 16:9, and the narrower 5:4) are not wider than 16:9: there is nothing to lower and no work is done for them. Only after a
	// wide screen was seen (or when an earlier run left its values in safearea.ini) is there one pass at a standard ratio, to put the dvars and the UI back; then the thread sleeps again.
	int engaged = GetPrivateProfileIntA("safearea", "engaged", 0, safe_ini) != 0;
	for (;;) {
		HWND w = NULL;
		EnumWindows(find_game_window, (LPARAM)&w);
		RECT r;
		if (w && GetClientRect(w, &r) && r.bottom > 0 && r.right > 0 && !IsIconic(w)) {
			float aspect = (float)r.right / (float)r.bottom;
			int wide = aspect > 16.0f / 9.0f + 0.02f;
			if (wide || engaged) {
				safe_apply(aspect);
				if (wide != engaged) {
					engaged = wide;
					WritePrivateProfileStringA("safearea", "engaged", wide ? "1" : "0", safe_ini);
				}
			}
		}
		Sleep(250);
	}
}

// ---- a window that fits on the screen (Vlad: "windowed mode always takes the monitor's native resolution"; owner's night list 2026-11-23)
// The game's window size (rva 0x10a90, called before the window is made, on every vid_restart too): windowed mode (r_displayMode 2) takes r_customMode
// when it is "WxH" and not bigger than the monitor, else r_mode, whose list is the monitor's modes and whose default is the screen's own size: a window as
// big as the screen, whose title bar and frame then go past the screen and under the taskbar. The window's parameters after the call: +0x0c the mode,
// +0x10 / +0x14 the window's place (vid_xpos, vid_ypos), +0x18 / +0x1c and +0x20 / +0x24 the size (the device's back buffer and the window's client
// area). When the window with its frame does not fit in the monitor's work area, the largest r_mode that does is taken, the same shape first, and the
// window is moved into the work area. [display] window_fit=0: the game's size.
#ifdef _WIN64
#define REF_WND_PARMS 0x10a90
#define WND_MODE 0x0c
#define WND_X 0x10
#define WND_Y 0x14
#define WND_BACK_W 0x18
#define WND_BACK_H 0x1c
#define WND_W 0x20
#define WND_H 0x24
static void (*wnd_parms_original)(uint8_t *p);

static void wnd_parms_hook(uint8_t *p)
{
	wnd_parms_original(p);
	if (*(int *)(p + WND_MODE) != 2)
		return;
	const int w = *(int *)(p + WND_W), h = *(int *)(p + WND_H);
	RECT fr = {0, 0, 100, 100}; // the frame of the game's windowed style (WS_CAPTION | WS_SYSMENU, WS_EX_WINDOWEDGE as the game makes it)
	AdjustWindowRectEx(&fr, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_WINDOWEDGE);
	const int fw = fr.right - fr.left - 100, fh = fr.bottom - fr.top - 100;
	POINT at = {*(int *)(p + WND_X), *(int *)(p + WND_Y)};
	MONITORINFOEXA mi;
	memset(&mi, 0, sizeof(mi));
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoA(MonitorFromPoint(at, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi))
		return;
	const int ww = mi.rcWork.right - mi.rcWork.left, wh = mi.rcWork.bottom - mi.rcWork.top;
	int nw = w, nh = h;
	if (w + fw > ww || h + fh > wh) {
		// the monitor's display modes (32 bits, 640x480 and up): the list r_mode is made of (the game's dvars are not up yet for the first window)
		int best_w = 0, best_h = 0, best_same = 0, n = 0;
		DEVMODEA dm;
		memset(&dm, 0, sizeof(dm));
		dm.dmSize = sizeof(dm);
		for (DWORD i = 0; EnumDisplaySettingsExA(mi.szDevice, i, &dm, 0) && i < 4096; i++) {
			const int mw = (int)dm.dmPelsWidth, mh = (int)dm.dmPelsHeight;
			if (dm.dmBitsPerPel != 32 || mw < 640 || mh < 480)
				continue;
			n++;
			if (mw + fw > ww || mh + fh > wh)
				continue;
			const int same = abs(mw * h - mh * w) * 100 < w * mh; // (the same shape within 1%)
			if (same > best_same || (same == best_same && mw * mh > best_w * best_h))
				best_w = mw, best_h = mh, best_same = same;
		}
		if (!best_w) {
			static int told;
			if (!told++)
				log_printf("window: the %dx%d window does not fit in the work area %dx%d and none of the monitor's %d modes does: left as it is", w, h, ww, wh, n);
			return;
		}
		nw = best_w, nh = best_h;
	}
	// the place: the whole window inside the work area
	int x = at.x, y = at.y;
	if (x + nw + fw > mi.rcWork.right)
		x = mi.rcWork.right - nw - fw;
	if (y + nh + fh > mi.rcWork.bottom)
		y = mi.rcWork.bottom - nh - fh;
	if (x < mi.rcWork.left)
		x = mi.rcWork.left;
	if (y < mi.rcWork.top)
		y = mi.rcWork.top;
	if (nw == w && nh == h && x == at.x && y == at.y)
		return;
	*(int *)(p + WND_W) = *(int *)(p + WND_BACK_W) = nw;
	*(int *)(p + WND_H) = *(int *)(p + WND_BACK_H) = nh;
	*(int *)(p + WND_X) = x;
	*(int *)(p + WND_Y) = y;
	log_printf("window: windowed %dx%d at %d,%d does not fit in the work area %dx%d (frame %dx%d): %dx%d at %d,%d", w, h, (int)at.x, (int)at.y, ww, wh, fw, fh, nw, nh, x, y);
}

// Installed from d3dcap.c's Direct3DCreate9 on the game's main thread, before the window is made (window_install, after the window, is too late for the
// first window: it only covers a build whose Direct3DCreate9 did not come through d3dcap). Before game_init the address is the reference build's (XR
// is not set up yet): on another build the head bytes do not match and nothing is hooked.
static volatile LONG fit_tried;
static void window_fit_install(void)
{
	if (InterlockedExchange(&fit_tried, 1))
		return;
	if (!settings_window_fit()) {
		log_printf("window: window_fit off ([display] window_fit=0)");
		return;
	}
	if (!addr_resolved(REF_WND_PARMS)) {
		log_printf("window: the window's size code was not found in this build: no window_fit");
		return;
	}
	static const uint8_t head[15] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x70, 0x48, 0x8b, 0xd9}; // mov [rsp+18],rbx; push rbp/rsi/rdi; sub rsp,70; mov rbx,rcx
	if (!hook_function((uint8_t *)GetModuleHandleA(NULL) + XR(REF_WND_PARMS), head, sizeof(head), (void *)wnd_parms_hook, (void **)&wnd_parms_original))
		log_printf("window: the window's size code (0x10a90) is not the expected code: no window_fit");
	else
		log_printf("window: a window that does not fit on the screen gets a game resolution that does (window_fit)");
}
#else
static void window_fit_install(void)
{
}
#endif

void window_fit_early(void)
{
	window_fit_install();
}

void window_install(void)
{
	window_fit_install();
	if (settings_safearea())
		CreateThread(NULL, 0, safearea_thread, NULL, 0, NULL);
	if (!settings_darktitle())
		return;
	CreateThread(NULL, 0, window_thread, NULL, 0, NULL);
}
