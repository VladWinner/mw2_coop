// Dark title bar and square corners on the game's window. DwmSetWindowAttribute is looked up at run time (older Windows has no such attribute: the calls then just fail).
// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 (19 on Windows 10 before 20H1), DWMWA_WINDOW_CORNER_PREFERENCE is 33 (Windows 11), DWMWCP_DONOTROUND is 1.
// A window without WS_CAPTION (borderless, full screen) has no title bar and no frame the mod could draw wrongly: it is not touched.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
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
		if (!d || d->type != DVAR_TYPE_FLOAT)
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

void window_install(void)
{
	if (settings_safearea())
		CreateThread(NULL, 0, safearea_thread, NULL, 0, NULL);
	if (!settings_darktitle())
		return;
	CreateThread(NULL, 0, window_thread, NULL, 0, NULL);
}
