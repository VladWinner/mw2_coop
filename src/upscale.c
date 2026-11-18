// The output size of the upscalers, FSR 3 and DLSS ([display] fsr3=1 / dlss=1, with upscale_width / upscale_height; fsr3.c on the Direct3D 9 on 12 layer, src/dx/natfsr3.c on the
// native Direct3D 12 renderer). The game renders at a lower size than the window (the size its own video settings say: the "internal" size); the back buffer or swap chain and the
// window get the output size, and the upscaler makes the output from the internal picture.
// FSR 1 (AMD's EASU + RCAS on the game's Direct3D 9 picture, [display] upscale=1), which this file held, was removed on 2026-11-18 (owner: "remove FSR1 altogether, it is superfluous");
// an old upscale=1 is ignored (settings.c logs it). What is left is the sizing FSR 3 and DLSS share.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>

#include "log.h"
#include "settings.h"
#include "upscale.h"

static int out_w, out_h;      // the output size ([display] upscale_width / upscale_height)
static int in_w, in_h;        // the size the game renders at (what it asked the device for)

int upscale_enabled(void)
{
	// FSR 3 and DLSS (settings_fsr3() is 1 for both); settings.c leaves it 0 where they cannot run (DirectX 11 and lower, the 32-bit builds)
	return settings_fsr3() && settings_upscale_width() > 0 && settings_upscale_height() > 0;
}

// the native Direct3D 12 renderer (natbring.c): the game renders in_w x in_h, the swap chain (and the window) is out_w x out_h; 0 when nothing is upscaled
int upscale_native_sizes(int game_w, int game_h, int *out_width, int *out_height)
{
	if (!upscale_enabled())
		return 0;
	const int w = settings_upscale_width(), h = settings_upscale_height();
	if (game_w == w && game_h == h) {
		log_printf("upscale: the game asked for the output size %dx%d itself: nothing to upscale (set the game's own resolution lower)", w, h);
		return 0;
	}
	out_w = w;
	out_h = h;
	in_w = game_w;
	in_h = game_h;
	*out_width = w;
	*out_height = h;
	log_printf("upscale: native renderer: the game renders %dx%d, the swap chain is %dx%d (%s)", in_w, in_h, w, h, settings_dlss() ? "DLSS" : "FSR 3");
	return 1;
}

int upscale_adjust_params(D3DPRESENT_PARAMETERS *pp)
{
	if (!pp || !upscale_enabled())
		return 0;
	int w = settings_upscale_width(), h = settings_upscale_height();
	if ((int)pp->BackBufferWidth == w && (int)pp->BackBufferHeight == h) {
		log_printf("upscale: the game asked for the output size %dx%d itself: nothing to upscale (set the game's own resolution lower)", w, h);
		return 0;
	}
	out_w = w;
	out_h = h;
	in_w = (int)pp->BackBufferWidth;
	in_h = (int)pp->BackBufferHeight;
	pp->BackBufferWidth = (UINT)w;
	pp->BackBufferHeight = (UINT)h;
	log_printf("upscale: the game renders %dx%d, the back buffer is %dx%d (%s)", in_w, in_h, w, h, settings_dlss() ? "DLSS" : "FSR 3");
	return 1;
}

// The game believes the window is the internal size: it reads the cursor in client coordinates (ScreenToClient) and recentres it for mouse look (ClientToScreen). Its own two imports
// are given the scaled positions (the mod's code, which has its own calls, is not touched), both ways, so the menu cursor lands where it is and the recentring stays consistent.
static HWND game_window;
typedef BOOL(WINAPI *cursor_fn_t)(HWND, POINT *);
static cursor_fn_t real_screen_to_client, real_client_to_screen;

static BOOL WINAPI screen_to_client_hook(HWND h, POINT *pt)
{
	BOOL ok = real_screen_to_client(h, pt);
	if (ok && pt && h == game_window && out_w > 0 && in_w > 0) {
		pt->x = (LONG)((long long)pt->x * in_w / out_w);
		pt->y = (LONG)((long long)pt->y * in_h / out_h);
	}
	return ok;
}

static BOOL WINAPI client_to_screen_hook(HWND h, POINT *pt)
{
	if (pt && h == game_window && out_w > 0 && in_w > 0) {
		POINT scaled = {(LONG)((long long)pt->x * out_w / in_w), (LONG)((long long)pt->y * out_h / in_h)};
		BOOL ok = real_client_to_screen(h, &scaled);
		if (ok)
			*pt = scaled;
		return ok;
	}
	return real_client_to_screen(h, pt);
}

static void **find_import_slot(HMODULE exe, const char *dll, const char *func)
{
	uint8_t *base = (uint8_t *)exe;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return NULL;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir->VirtualAddress)
		return NULL;
	for (IMAGE_IMPORT_DESCRIPTOR *id = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); id->Name; id++) {
		if (lstrcmpiA((const char *)(base + id->Name), dll))
			continue;
		IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk));
		IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + id->FirstThunk);
		for (; names->u1.AddressOfData; names++, thunk++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
			if (!lstrcmpA((const char *)by_name->Name, func))
				return (void **)&thunk->u1.Function;
		}
	}
	return NULL;
}

static void hook_import(const char *func, void *hook, void **orig)
{
	void **slot = find_import_slot(GetModuleHandleA(NULL), "user32.dll", func);
	DWORD old;
	if (!slot || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
		log_printf("upscale: the game's import of %s was not found: the menu cursor is off by the scale", func);
		return;
	}
	*orig = *slot;
	*slot = hook;
	VirtualProtect(slot, sizeof(void *), old, &old);
	log_printf("upscale: the game's %s gets the cursor in its own (internal) coordinates", func);
}

// the window fits the output size
void upscale_device_made(HWND window)
{
	if (!window || out_w <= 0)
		return;
	game_window = window;
	if (!real_screen_to_client) {
		hook_import("ScreenToClient", (void *)screen_to_client_hook, (void **)&real_screen_to_client);
		hook_import("ClientToScreen", (void *)client_to_screen_hook, (void **)&real_client_to_screen);
	}
	RECT client, want = {0, 0, out_w, out_h};
	if (!GetClientRect(window, &client) || (client.right == out_w && client.bottom == out_h))
		return;
	AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtrA(window, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrA(window, GWL_EXSTYLE));
	SetWindowPos(window, NULL, 0, 0, want.right - want.left, want.bottom - want.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
	log_printf("upscale: window client area %ldx%ld made %dx%d", client.right, client.bottom, out_w, out_h);
}

int upscale_internal_size(int *w, int *h)
{
	if (out_w <= 0 || in_w <= 0)
		return 0;
	*w = in_w;
	*h = in_h;
	return 1;
}
