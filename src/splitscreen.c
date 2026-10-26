// Split screen: two copies of the game side by side on one PC (the co-op between them runs over the PC's own
// address like any other). The game has no controller support and reads the mouse by recentring the cursor,
// so a copy that is driven by a gamepad gets its input from this module instead of the keyboard and mouse:
//   settings.ini [splitscreen]
//     window  = left | right | top | bottom | full | <x>,<y>,<w>,<h>   where this copy's window goes (empty keeps it)
//     pad     = 0..3     the XInput pad that drives this copy; -1 (default): the keyboard and mouse as always
//     slot    = 0 | 1    one shared window for both copies: slot 0 makes it, both copies put their game window in it (top and
//                        bottom; stacked=0: left and right) while a level runs and take it out again in the menus
//     view    = 60..120  the horizontal view in degrees of a half while the shared window is up (default 90; the very wide halves
//                        stretch the picture at the edges when the engine widens the standard view to them)
//     delay   = seconds after a level starts before the windows join the shared window (default 12: the load is over)
//     fullscreen = 1 (default) | 0   the shared window covers the whole screen (0: 90% of it, with a title bar)
//     speed   = 1..100   the right stick's turn speed in pixels per 10 ms at full tilt (default 14)
//     fakepad = 1        QA: the pad's state is read from mw2coop\fakepad.txt ("lx ly rx ry buttons lt rt", XInput units)
// A pad copy has its own virtual cursor (the game's GetCursorPos/SetCursorPos are redirected, the real cursor stays with
// the other copy), is told that it is the active window (the game ignores the mouse of a background window), and gets
// the pad's keys as window messages (those work in a window that does not have the keyboard).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "splitscreen.h"

static DWORD started_at; // when the threads were started
static int slot = -1; // 0 first (left or top), 1 second
static int pad = -1;
static int turn_speed = 14;
static int fakepad;
static char window_spec[64];
static char ini_path[MAX_PATH];
static HWND game_window;

// the virtual cursor of a pad copy
static volatile LONG vx = 400, vy = 300;

typedef struct {
	WORD buttons;
	BYTE lt, rt;
	SHORT lx, ly, rx, ry;
} PadState;

typedef DWORD(WINAPI *xinput_get_t)(DWORD, void *);
static xinput_get_t xinput_get;

// ---------------------------------------------------------------- the import table

static void iat_patch(const char *dll, const char *func, void *replacement)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir->VirtualAddress)
		return;
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++) {
		if (lstrcmpiA((const char *)(base + d->Name), dll) != 0)
			continue;
		IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
		IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
		for (; names->u1.AddressOfData; names++, slots++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
			if (lstrcmpA((const char *)n->Name, func) != 0)
				continue;
			DWORD old;
			if (!VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old))
				return;
			slots->u1.Function = (ULONG_PTR)replacement;
			VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
			log_printf("splitscreen: %s redirected", func);
			return;
		}
	}
	log_printf("splitscreen: %s!%s is not imported", dll, func);
}

// ---------------------------------------------------------------- the redirected calls

static volatile LONG get_calls, set_calls;

static BOOL WINAPI my_get_cursor_pos(POINT *p)
{
	InterlockedIncrement(&get_calls);
	if (pad < 0) // a pad copy until a pad is given to it: the real cursor
		return GetCursorPos(p);
	if (!p)
		return FALSE;
	p->x = vx;
	p->y = vy;
	return TRUE;
}

static BOOL WINAPI my_set_cursor_pos(int x, int y)
{
	InterlockedIncrement(&set_calls);
	if (pad < 0)
		return SetCursorPos(x, y);
	vx = x;
	vy = y;
	return TRUE;
}

static HWND WINAPI my_get_foreground_window(void)
{
	return game_window && pad >= 0 ? game_window : GetForegroundWindow();
}

static HWND WINAPI my_get_active_window(void)
{
	return game_window && pad >= 0 ? game_window : GetActiveWindow();
}

// ---------------------------------------------------------------- the pad

static int read_pad(PadState *s)
{
	memset(s, 0, sizeof(*s));
	if (fakepad) {
		char path[MAX_PATH];
		lstrcpynA(path, ini_path, MAX_PATH);
		char *slash = strrchr(path, '\\');
		if (slash)
			lstrcpynA(slash + 1, "fakepad.txt", (int)(MAX_PATH - (slash + 1 - path)));
		FILE *f = fopen(path, "r");
		if (!f)
			return 1;
		int lx = 0, ly = 0, rx = 0, ry = 0, b = 0, lt = 0, rt = 0;
		if (fscanf(f, "%d %d %d %d %d %d %d", &lx, &ly, &rx, &ry, &b, &lt, &rt) >= 4) {
			s->lx = (SHORT)lx, s->ly = (SHORT)ly, s->rx = (SHORT)rx, s->ry = (SHORT)ry;
			s->buttons = (WORD)b, s->lt = (BYTE)lt, s->rt = (BYTE)rt;
		}
		fclose(f);
		return 1;
	}
	if (!xinput_get)
		return 0;
	// XINPUT_STATE is {DWORD packet; XINPUT_GAMEPAD}, XINPUT_GAMEPAD is {WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry}
	struct {
		DWORD packet;
		PadState gp;
	} raw;
	if (xinput_get((DWORD)pad, &raw) != ERROR_SUCCESS)
		return 0;
	*s = raw.gp;
	return 1;
}

#define BTN_UP 0x0001
#define BTN_DOWN 0x0002
#define BTN_START 0x0010
#define BTN_BACK 0x0020
#define BTN_LTHUMB 0x0040
#define BTN_RTHUMB 0x0080
#define BTN_LB 0x0100
#define BTN_RB 0x0200
#define BTN_A 0x1000
#define BTN_B 0x2000
#define BTN_X 0x4000
#define BTN_Y 0x8000

// virtual input slots; the game's default keys
enum { K_W, K_A, K_S, K_D, K_SPACE, K_CTRL, K_R, K_F, K_G, K_Q, K_SHIFT, K_V, K_ESC, K_TAB, K_1, K_2, K_ENTER, K_COUNT };
static const UINT key_vk[K_COUNT] = {'W', 'A', 'S', 'D', VK_SPACE, VK_CONTROL, 'R', 'F', 'G', 'Q', VK_SHIFT, 'V', VK_ESCAPE, VK_TAB, '1', '2', VK_RETURN};
#define M_LEFT K_COUNT
#define M_RIGHT (K_COUNT + 1)
#define SLOTS (K_COUNT + 2)
static char held[SLOTS];
static DWORD sent_at[SLOTS];

static void send_slot(int slot, int down)
{
	if (!game_window)
		return;
	// a key that is held is sent again every 250 ms: a level load clears the game's key states under it
	DWORD now = GetTickCount();
	if (held[slot] == down && !(down && now - sent_at[slot] > 250))
		return;
	sent_at[slot] = now;
	held[slot] = (char)down;
	if (slot == M_LEFT || slot == M_RIGHT) {
		UINT msg = slot == M_LEFT ? (down ? WM_LBUTTONDOWN : WM_LBUTTONUP) : (down ? WM_RBUTTONDOWN : WM_RBUTTONUP);
		WPARAM w = down ? (slot == M_LEFT ? MK_LBUTTON : MK_RBUTTON) : 0;
		PostMessageA(game_window, msg, w, MAKELPARAM(vx, vy));
		return;
	}
	UINT vk = key_vk[slot];
	UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
	LPARAM lp = 1 | (LPARAM)(scan << 16);
	if (!down)
		lp |= (LPARAM)1 << 30 | (LPARAM)1 << 31;
	PostMessageA(game_window, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
}

static int level_running(void);

static void apply(const PadState *s)
{
	// in the lobby (no level yet) the second player's A is the lobby's Ready button: nobody sees this copy's window, the first player's
	// Ready is pressed on his screen as usual
	static int a_was;
	int a_now = !!(s->buttons & BTN_A);
	if (slot > 0 && !level_running() && a_now && !a_was) {
		game_cbuf_addtext("mw2coop_ready\n");
		log_printf("splitscreen: the second player pressed Ready");
	}
	a_was = a_now;
	int want[SLOTS] = {0};
	want[K_W] = s->ly > 12000;
	want[K_S] = s->ly < -12000;
	want[K_D] = s->lx > 12000;
	want[K_A] = s->lx < -12000;
	want[K_SPACE] = !!(s->buttons & BTN_A);
	want[K_CTRL] = !!(s->buttons & BTN_B);
	want[K_R] = !!(s->buttons & BTN_X);
	want[K_F] = !!(s->buttons & BTN_X); // reload and use are on one button: the game takes the one that fits
	want[K_1] = !!(s->buttons & BTN_Y);
	want[K_G] = !!(s->buttons & BTN_LB);
	want[K_Q] = !!(s->buttons & BTN_RB);
	want[K_SHIFT] = !!(s->buttons & BTN_LTHUMB);
	want[K_V] = !!(s->buttons & BTN_RTHUMB);
	want[K_ESC] = !!(s->buttons & BTN_START);
	want[K_TAB] = !!(s->buttons & BTN_BACK);
	want[K_2] = !!(s->buttons & BTN_UP);
	want[K_ENTER] = !!(s->buttons & BTN_DOWN);
	want[M_LEFT] = s->rt > 100;
	want[M_RIGHT] = s->lt > 100;
	for (int i = 0; i < SLOTS; i++)
		send_slot(i, want[i]);
	if (s->rx > 6000 || s->rx < -6000)
		vx += (LONG)((long long)s->rx * turn_speed / 32768);
	if (s->ry > 6000 || s->ry < -6000)
		vy -= (LONG)((long long)s->ry * turn_speed / 32768);
}

// ---------------------------------------------------------------- the window

static BOOL CALLBACK find_window(HWND h, LPARAM out)
{
	DWORD pid;
	GetWindowThreadProcessId(h, &pid);
	if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
		return TRUE;
	char cls[64] = "";
	GetClassNameA(h, cls, sizeof(cls));
	if (strstr(cls, "Splash")) // the splash screen comes first and goes away
		return TRUE;
	RECT r;
	GetWindowRect(h, &r);
	if (r.right - r.left < 200)
		return TRUE;
	*(HWND *)out = h;
	return FALSE;
}

static void place_window(HWND w)
{
	if (!window_spec[0])
		return;
	RECT screen;
	SystemParametersInfoA(SPI_GETWORKAREA, 0, &screen, 0);
	int sw = screen.right - screen.left, sh = screen.bottom - screen.top;
	int x, y, cw, ch;
	if (sscanf(window_spec, "%d,%d,%d,%d", &x, &y, &cw, &ch) == 4) {
	} else if (!lstrcmpiA(window_spec, "left")) {
		x = screen.left, y = screen.top, cw = sw / 2, ch = sh;
	} else if (!lstrcmpiA(window_spec, "right")) {
		x = screen.left + sw / 2, y = screen.top, cw = sw / 2, ch = sh;
	} else if (!lstrcmpiA(window_spec, "top")) {
		x = screen.left, y = screen.top, cw = sw, ch = sh / 2;
	} else if (!lstrcmpiA(window_spec, "bottom")) {
		x = screen.left, y = screen.top + sh / 2, cw = sw, ch = sh / 2;
	} else if (!lstrcmpiA(window_spec, "full")) {
		x = screen.left, y = screen.top, cw = sw, ch = sh;
	} else {
		log_printf("splitscreen: unknown window '%s'", window_spec);
		return;
	}
	SetWindowPos(w, NULL, x, y, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE);
	log_printf("splitscreen: window at %d,%d size %dx%d", x, y, cw, ch);
}

// ---------------------------------------------------------------- one window for both

// slot 0 makes the container window (its own thread and message loop); every slot's game window becomes a child of it
// while a level runs, and goes back to being a window of its own in the menus
#define CONTAINER_CLASS "MW2COOPSplit"
static int stacked = 1; // 1 (default): one above the other, 0: side by side
static HWND container;
static int joined;
static void slot_size(int *w, int *h);
static DWORD join_delay_ms = 12000; // how long after the level starts the windows join
static float view_degrees = 90; // the horizontal view of a half: the picture is very wide (3.7:1), the engine widens the view with it and the edges stretch

// cg_fov that gives the half a horizontal view of view_degrees (the engine takes cg_fov for a 4:3 picture and widens it with the aspect);
// written into the dvar itself, like fov.c does: its range (65 to 80) is not for this
// The engine picks one of three set aspect ratios (4:3, 16:10, 16:9) for the screen and takes the window for a picture of that shape, so a
// window of another shape is drawn stretched (a half of the shared window is 3.7:1: everything twice as wide). The pixel aspect it works
// out from that is put right here: the aspect of the picture is the window's own and its pixels are square.
#ifdef _WIN64
#define RVA_ASPECT_DISPLAY 0x4113738 // float, set by the video init (rva 0x10550)
#define RVA_ASPECT_PIXEL 0x411373c   // float, aspect * height / width of the window
#elif !defined(MW2COOP_IW4X)
#define RVA_ASPECT_DISPLAY 0x188edb4 // the 32-bit game's video init (rva 0x10a4f0): the same two floats
#define RVA_ASPECT_PIXEL 0x188edb8
#endif

static void apply_aspect(void)
{
#ifdef RVA_ASPECT_DISPLAY
	int w, h;
	slot_size(&w, &h);
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	float *display = (float *)(base + RVA_ASPECT_DISPLAY), *pixel = (float *)(base + RVA_ASPECT_PIXEL);
	if (!(*display > 1.0f && *display < 3.0f) && *display != (float)w / h) // not what a build with these addresses holds (4:3 to 16:9 presets)
		return;
	*display = (float)w / h;
	*pixel = 1.0f;
#endif
}

static void apply_fov(void)
{
	apply_aspect();
	dvar_t *fov = game_find_dvar("cg_fov"), *min = game_find_dvar("cg_fovMin");
	if (!fov || fov->type != DVAR_TYPE_FLOAT)
		return;
	char over[32] = "";
	float want = view_degrees;
	if (game_dvar_raw("mw2coop_split_fov", over, sizeof(over)) && over[0]) // QA: set mw2coop_split_fov to try a value
		want = (float)atof(over);
	int w, h;
	slot_size(&w, &h);
	double k = ((double)w / h) / (4.0 / 3.0);
	double rad = want * 3.14159265358979 / 180.0;
	float value = (float)(2.0 * atan(tan(rad / 2.0) / k) * 180.0 / 3.14159265358979);
	if (min && min->type == DVAR_TYPE_FLOAT)
		min->current.value = 1;
	if (fov->current.value != value)
		fov->current.value = value;
}

static int embed; // 1: the game windows become child windows (they flicker: two processes drawing into one window); 0 (default): they stay windows of their own,
                  // owned by the shared window, so that they move, minimise and show in the taskbar with it
static LONG saved_style, saved_exstyle;
static RECT saved_rect;

// the shared window's client size: 90% of the work area, even numbers; a slot's picture is half of it
static int fullscreen = 1; // the shared window covers the whole screen (windowed: 90% of the work area, with a title bar)

static void monitor_rect(RECT *r)
{
	POINT origin = {0, 0};
	MONITORINFO mi;
	memset(&mi, 0, sizeof(mi));
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi))
		*r = mi.rcMonitor;
	else
		SetRect(r, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

static void client_size(int *w, int *h)
{
	RECT area;
	if (fullscreen) {
		monitor_rect(&area);
		*w = (area.right - area.left) & ~1;
		*h = (area.bottom - area.top) & ~3;
		return;
	}
	SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
	*w = (area.right - area.left) * 9 / 10 & ~1;
	*h = (area.bottom - area.top) * 9 / 10 & ~3;
}

static void slot_size(int *w, int *h)
{
	client_size(w, h);
	if (stacked)
		*h /= 2;
	else
		*w /= 2;
}

static LRESULT CALLBACK container_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
	if (msg == WM_CLOSE) {
		log_printf("splitscreen: the window was closed");
		ExitProcess(0);
	}
	if (msg == WM_ERASEBKGND)
		return 1;
	return DefWindowProcA(h, msg, w, l);
}

static DWORD WINAPI container_thread(LPVOID unused)
{
	(void)unused;
	WNDCLASSA wc = {0};
	wc.lpfnWndProc = container_proc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = CONTAINER_CLASS;
	wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	RegisterClassA(&wc);
	RECT area;
	SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
	int cw, ch;
	client_size(&cw, &ch);
	HWND c;
	if (fullscreen) { // no frame, the whole monitor
		RECT mon;
		monitor_rect(&mon);
		c = CreateWindowExA(0, CONTAINER_CLASS, "Modern Warfare 2 - split screen", WS_POPUP | WS_CLIPCHILDREN, mon.left, mon.top, cw, ch, NULL, NULL, wc.hInstance, NULL);
	} else {
		RECT r = {0, 0, cw, ch};
		AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
		int w = r.right - r.left, h = r.bottom - r.top;
		c = CreateWindowExA(0, CONTAINER_CLASS, "Modern Warfare 2 - split screen", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, area.left + (area.right - area.left - w) / 2,
				    area.top + (area.bottom - area.top - h) / 2, w, h, NULL, NULL, wc.hInstance, NULL);
	}
	container = c;
	MSG m;
	while (GetMessageA(&m, NULL, 0, 0) > 0) {
		TranslateMessage(&m);
		DispatchMessageA(&m);
	}
	return 0;
}

static int level_running(void)
{
	dvar_t *d = game_find_dvar("mapname");
	return d && d->type == DVAR_TYPE_STRING && d->current.string && d->current.string[0];
}

static void layout_child(HWND w)
{
	RECT c;
	if (!GetClientRect(container, &c) || c.right < 100 || c.bottom < 100) // gone or minimised
		return;
	int cw = c.right, ch = c.bottom;
	int x = stacked ? 0 : slot * cw / 2, y = stacked ? slot * ch / 2 : 0;
	int ww = stacked ? cw : cw / 2, hh = stacked ? ch / 2 : ch;
	RECT now;
	GetWindowRect(w, &now);
	POINT tl = {now.left, now.top};
	if (embed)
		ScreenToClient(container, &tl);
	else {
		POINT o = {x, y};
		ClientToScreen(container, &o);
		x = o.x, y = o.y;
	}
	if (tl.x != x || tl.y != y || now.right - now.left != ww || now.bottom - now.top != hh) {
		static int fixes;
		if (fixes++ < 40)
			log_printf("splitscreen: slot %d window is at %ld,%ld %ldx%ld, put at %d,%d %dx%d", slot, tl.x, tl.y, now.right - now.left, now.bottom - now.top, x, y, ww, hh);
		SetWindowPos(w, NULL, x, y, ww, hh, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

static void restore_resolution(void);

// called every 100 ms
static void container_poll(HWND w)
{
	if (slot < 0)
		return;
	if (!container || !IsWindow(container)) {
		container = FindWindowA(CONTAINER_CLASS, NULL);
		if (!container && joined) {
			log_printf("splitscreen: the shared window is gone");
			ExitProcess(0);
		}
	}
	// the windows go into the shared window only when the level is loaded: two game processes whose windows are tied together stall each other
	// while one of them is in a long load that does not pump its window messages
	static DWORD level_since;
	if (!level_running())
		level_since = 0;
	else if (!level_since)
		level_since = GetTickCount();
	int want = level_since && GetTickCount() - level_since > join_delay_ms && container && IsWindow(container);
	if (want && !joined) {
		GetWindowRect(w, &saved_rect);
		saved_style = GetWindowLongA(w, GWL_STYLE);
		saved_exstyle = GetWindowLongA(w, GWL_EXSTYLE);
		SetWindowLongA(w, GWL_STYLE, (saved_style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_SYSMENU)) | WS_CHILD | WS_VISIBLE);
		if (embed)
			SetParent(w, container);
		else
		{
			SetWindowLongA(w, GWL_STYLE, WS_POPUP | WS_VISIBLE);
			SetWindowLongA(w, GWL_EXSTYLE, saved_exstyle | WS_EX_TOOLWINDOW);
			SetWindowLongPtrA(w, GWLP_HWNDPARENT, (LONG_PTR)container);
		}
		ShowWindow(container, SW_SHOW); // the shared window first, each game window goes to its half before it is shown: no overlap
		joined = 1;
		layout_child(w);
		ShowWindow(w, SW_SHOWNA);
		log_printf("splitscreen: slot %d joins the shared window", slot);
	} else if (!want && joined) {
		if (embed)
			SetParent(w, NULL);
		else {
			SetWindowLongPtrA(w, GWLP_HWNDPARENT, 0);
			SetWindowLongA(w, GWL_EXSTYLE, saved_exstyle);
		}
		SetWindowLongA(w, GWL_STYLE, saved_style);
		SetWindowPos(w, NULL, saved_rect.left, saved_rect.top, saved_rect.right - saved_rect.left, saved_rect.bottom - saved_rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
		if (slot == 0) {
			ShowWindow(container, SW_HIDE);
			restore_resolution();
		}
		joined = 0;
		log_printf("splitscreen: slot %d leaves the shared window", slot);
	}
	if (!joined && slot > 0) {
		// the second copy has no window of its own in the menus: no taskbar button, nothing on the screen
		LONG ex = GetWindowLongA(w, GWL_EXSTYLE);
		if (!(ex & WS_EX_TOOLWINDOW) || (ex & WS_EX_APPWINDOW))
			SetWindowLongA(w, GWL_EXSTYLE, (ex | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
		if (IsWindowVisible(w))
			ShowWindow(w, SW_HIDE);
	}
	if (joined) {
		layout_child(w);
		static DWORD fov_at;
		DWORD now = GetTickCount();
		if (now - fov_at > 100) { // the game's scripts may put the field of view back
			fov_at = now;
			apply_fov();
		}
	}
}

static HWND find_game_window(int tries)
{
	HWND w = NULL;
	for (int i = 0; i < tries && !w; i++) {
		EnumWindows(find_window, (LPARAM)&w);
		if (!w)
			Sleep(100);
	}
	return w;
}

// the game draws at the size of its half of the shared window: no stretching
// r_customMode is saved with the player's settings: the first copy puts its own value back when the session ends, and a file keeps it for a
// crash (the next start puts it back, restore_stale)
static char orig_mode[64];
static int orig_saved;

static void state_path(char *out)
{
	lstrcpynA(out, ini_path, MAX_PATH);
	char *slash = strrchr(out, '\\');
	if (slash)
		lstrcpynA(slash + 1, "split_state.ini", (int)(MAX_PATH - (slash + 1 - out)));
}

// returns 1 when the video mode was changed (a restart is queued)
static int set_resolution(void)
{
	int w, h;
	slot_size(&w, &h);
	char want[32], cmd[128];
	snprintf(want, sizeof(want), "%dx%d", w, h);
	char have[64] = "";
	game_dvar_raw("r_customMode", have, sizeof(have));
	if (strcmp(have, want) == 0)
		return 0;
	if (slot == 0 && !orig_saved) {
		lstrcpynA(orig_mode, have, sizeof(orig_mode));
		orig_saved = 1;
		char path[MAX_PATH];
		state_path(path);
		WritePrivateProfileStringA("mode", "original", orig_mode, path);
	}
	snprintf(cmd, sizeof(cmd), "set r_customMode \"%s\"%cvid_restart%c", want, 10, 10);
	game_cbuf_addtext(cmd);
	log_printf("splitscreen: video mode %s (was '%s')", want, have);
	return 1;
}

static void restore_resolution(void)
{
	if (slot != 0 || !orig_saved)
		return;
	char cmd[128], path[MAX_PATH];
	snprintf(cmd, sizeof(cmd), "set r_customMode \"%s\"%cvid_restart%c", orig_mode, 10, 10);
	game_cbuf_addtext(cmd);
	log_printf("splitscreen: video mode back to '%s'", orig_mode);
	orig_saved = 0;
	state_path(path);
	DeleteFileA(path);
}

// the first copy's Start button: the picture gets the size of its half first (a video restart), the launch follows
static volatile LONG launch_pending;

static DWORD WINAPI launch_thread(LPVOID unused)
{
	(void)unused;
	HWND old = game_window;
	for (int i = 0; i < 150 && old && IsWindow(old); i++)
		Sleep(100);
	Sleep(2500); // the new window and its device
	game_cbuf_addtext("coopLaunch\n");
	log_printf("splitscreen: the video restart is done, launching");
	InterlockedExchange(&launch_pending, 0);
	return 0;
}

int splitscreen_before_launch(void)
{
	if (slot != 0 || launch_pending)
		return launch_pending ? 1 : 0;
	if (!set_resolution())
		return 0;
	InterlockedExchange(&launch_pending, 1);
	CreateThread(NULL, 0, launch_thread, NULL, 0, NULL);
	return 1;
}

static DWORD WINAPI split_thread(LPVOID unused)
{
	(void)unused;
	HWND w = find_game_window(600); // up to a minute: the 32-bit game opens its window late
	if (!w) {
		log_printf("splitscreen: no game window found");
		return 0;
	}
	game_window = w;
	Sleep(500);
	if (slot > 0) { // the second copy is hidden: its picture is made once the game's console is up
		for (int i = 0; i < 600 && !game_find_dvar("r_customMode"); i++)
			Sleep(100);
		Sleep(3000);
		if (set_resolution()) { // the video restart makes a new window: wait for it before the join below
			for (int i = 0; i < 200 && IsWindow(w); i++)
				Sleep(100);
			HWND n = find_game_window(100);
			if (n) {
				w = game_window = n;
				log_printf("splitscreen: new game window %p", (void *)n);
			}
			Sleep(3000);
		}
		// the join comes only now (not from the command file): a video restart in the middle of a join or a level load breaks it
		while (GetTickCount() - started_at < 20000)
			Sleep(200);
		game_cbuf_addtext("mw2coop_join 127.0.0.1\n");
		log_printf("splitscreen: joining the first process");
		Sleep(6000); // nobody sits at this copy: it marks itself ready in the lobby (that also sends its player id for the saves)
		game_cbuf_addtext("mw2coop_ready\n");
		log_printf("splitscreen: ready in the lobby");
	}
	if (slot < 0)
		place_window(w);
	if (pad >= 0)
		log_printf("splitscreen: pad %d drives this copy%s", pad, fakepad ? " (fakepad.txt)" : "");
	DWORD last_activate = 0, last_poll = 0, pad_checked = 0;
	HANDLE parent = NULL; // the second process quits with the first
	char pid[16];
	if (slot > 0 && GetEnvironmentVariableA("MW2COOP_PARENT_PID", pid, sizeof(pid)))
		parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)atoi(pid));
	for (;;) {
		Sleep(10);
		DWORD now = GetTickCount();
		if (now - last_poll > (joined ? 16u : 100u)) {
			last_poll = now;
			if (!IsWindow(w)) { // a video restart makes a new window
				HWND n = find_game_window(100);
				if (n && n != w) {
					log_printf("splitscreen: new game window %p", (void *)n);
					w = game_window = n;
					joined = 0;
				}
			}
			container_poll(w);
			if (parent && WaitForSingleObject(parent, 0) == WAIT_OBJECT_0) {
				log_printf("splitscreen: the first process is gone");
				ExitProcess(0);
			}
		}
		if (slot > 0 && pad < 0 && now - pad_checked > 500) { // the first copy tells which pad is the second player's
			pad_checked = now;
			char file[MAX_PATH];
			state_path(file);
			lstrcpynA(strrchr(file, '\\') + 1, "pad.txt", 16);
			FILE *f = fopen(file, "r");
			if (f) {
				int n = -1;
				if (fscanf(f, "%d", &n) == 1 && n >= 0 && n <= 3) {
					pad = n;
					log_printf("splitscreen: pad %d drives this copy", pad);
				}
				fclose(f);
			}
		}
		if (pad < 0)
			continue;
		if (now - last_activate > 5000) { // the game ignores the mouse of a window that is not active
			last_activate = now;
			PostMessageA(w, WM_ACTIVATE, WA_ACTIVE, 0);
			PostMessageA(w, WM_ACTIVATEAPP, TRUE, 0);
		}
		PadState s;
		if (read_pad(&s))
			apply(&s);
	}
	return 0;
}

static int started;

// the first XInput pad with something pressed (for the lobby: whose pad is the second player's); -1 when none
int splitscreen_pad_buttons(int pad)
{
	if (!xinput_get || pad < 0 || pad > 3)
		return 0;
	struct {
		DWORD packet;
		PadState gp;
	} raw;
	if (xinput_get((DWORD)pad, &raw) != ERROR_SUCCESS)
		return 0;
	return raw.gp.buttons;
}

int splitscreen_poll_any_pad(void)
{
	if (!xinput_get) {
		const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
		for (int i = 0; i < 3 && !xinput_get; i++) {
			HMODULE h = LoadLibraryA(dlls[i]);
			if (h)
				xinput_get = (xinput_get_t)GetProcAddress(h, "XInputGetState");
		}
		if (!xinput_get)
			return -1;
	}
	for (int i = 0; i < 4; i++) {
		struct {
			DWORD packet;
			PadState gp;
		} raw;
		if (xinput_get((DWORD)i, &raw) != ERROR_SUCCESS)
			continue;
		const PadState *g = &raw.gp;
		if (g->buttons || g->lt > 100 || g->rt > 100 || g->lx > 20000 || g->lx < -20000 || g->ly > 20000 || g->ly < -20000)
			return i;
	}
	return -1;
}

static void start_threads(void)
{
	if (started)
		return;
	started = 1;
	started_at = GetTickCount();
	if (slot == 0)
		CreateThread(NULL, 0, container_thread, NULL, 0, NULL);
	CreateThread(NULL, 0, split_thread, NULL, 0, NULL);
}

void splitscreen_host_activate(void)
{
	if (slot >= 0)
		return;
	slot = 0;
	log_printf("splitscreen: this copy is the first of a split screen session");
	start_threads();
}

// a crash left r_customMode at the half-size picture: put the player's own value back
static DWORD WINAPI stale_thread(LPVOID unused)
{
	(void)unused;
	char path[MAX_PATH], orig[64];
	state_path(path);
	GetPrivateProfileStringA("mode", "original", "", orig, sizeof(orig), path);
	HWND w = find_game_window(600);
	if (!w)
		return 0;
	Sleep(8000); // the menus are up
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "set r_customMode \"%s\"%cvid_restart%c", orig, 10, 10);
	game_cbuf_addtext(cmd);
	DeleteFileA(path);
	log_printf("splitscreen: video mode put back to '%s' (left by an earlier session)", orig);
	return 0;
}

void splitscreen_install(void)
{
	DWORD n = GetModuleFileNameA(NULL, ini_path, MAX_PATH);
	while (n > 0 && ini_path[n - 1] != '\\')
		n--;
	ini_path[n] = 0;
	lstrcatA(ini_path, "mw2coop\\settings.ini");
	pad = GetPrivateProfileIntA("splitscreen", "pad", -1, ini_path);
	turn_speed = GetPrivateProfileIntA("splitscreen", "speed", 14, ini_path);
	fakepad = GetPrivateProfileIntA("splitscreen", "fakepad", 0, ini_path) != 0;
	GetPrivateProfileStringA("splitscreen", "window", "", window_spec, sizeof(window_spec), ini_path);
	slot = GetPrivateProfileIntA("splitscreen", "slot", -1, ini_path);
	view_degrees = (float)GetPrivateProfileIntA("splitscreen", "view", 90, ini_path);
	join_delay_ms = (DWORD)GetPrivateProfileIntA("splitscreen", "delay", 12, ini_path) * 1000;
	fullscreen = GetPrivateProfileIntA("splitscreen", "fullscreen", 1, ini_path) != 0;
	embed = GetPrivateProfileIntA("splitscreen", "embed", 0, ini_path) != 0;
	stacked = GetPrivateProfileIntA("splitscreen", "stacked", 1, ini_path) != 0;
	if (slot < 0) {
		char path[MAX_PATH];
		state_path(path);
		if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
			CreateThread(NULL, 0, stale_thread, NULL, 0, NULL);
	}
	if (pad < 0 && !window_spec[0] && slot < 0)
		return;
	if ((pad >= 0 || slot > 0) && !fakepad) {
		const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
		for (int i = 0; i < 3 && !xinput_get; i++) {
			HMODULE h = LoadLibraryA(dlls[i]);
			if (h)
				xinput_get = (xinput_get_t)GetProcAddress(h, "XInputGetState");
		}
		if (!xinput_get) {
			log_printf("splitscreen: no XInput, the pad cannot be read");
			pad = -1;
		}
	}
	if (pad >= 0 || slot > 0) { // the second copy gets its pad later (mw2coop\pad.txt)
		iat_patch("USER32.dll", "GetCursorPos", my_get_cursor_pos);
		iat_patch("USER32.dll", "SetCursorPos", my_set_cursor_pos);
		iat_patch("USER32.dll", "GetForegroundWindow", my_get_foreground_window);
		iat_patch("USER32.dll", "GetActiveWindow", my_get_active_window);
	}
	log_printf("splitscreen: pad %d, window '%s'", pad, window_spec);
	start_threads();
}
