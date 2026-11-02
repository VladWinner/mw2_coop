// The developer console: a panel over the top of the game picture, drawn by a Present hook of the game's Direct3D 9 device (GDI paints the text into a bitmap, the
// bitmap goes into a texture, one quad draws it), with its own keyboard handling (the game window's procedure is replaced while the game runs). The same code serves the
// 64-bit, the 32-bit and the iw4x-sp builds: it needs only the device and the game's command buffer.
#include <windows.h>
#include <d3d9.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "game.h"
#include "log.h"
#include "native.h"
#include "shot.h"
#ifndef _WIN64
#include "d3dcap.h"
#endif

#define MAX_LINES 500
#define LINE_LEN 200
#define INPUT_LEN 190
#define HISTORY 24
#define SCAN_TILDE 0x29 // the key left of 1, whatever the layout puts on it

static CRITICAL_SECTION lock;
static char lines[MAX_LINES][LINE_LEN];
static int line_count, line_next; // a ring: line_next is where the next line goes
static char input[INPUT_LEN + 1];
static int input_len, cursor;
static char history[HISTORY][INPUT_LEN + 1];
static int history_count, history_pos;
static int scroll;
static volatile LONG is_open;
static volatile LONG dirty = 1;
static volatile LONG ignore_tilde;
static char dump_name[64]; // QA: a frame with the panel on it is saved under this name

// ---------------------------------------------------------------- the text

void console_print(const char *text)
{
	if (!text)
		return;
	EnterCriticalSection(&lock);
	const char *p = text;
	while (*p) {
		const char *e = p;
		while (*e && *e != '\n')
			e++;
		char one[LINE_LEN];
		int n = 0;
		for (const char *c = p; c < e && n < LINE_LEN - 1; c++)
			if (*c != '\r')
				one[n++] = *c;
		one[n] = 0;
		if (n || *e == '\n') {
			lstrcpynA(lines[line_next], one, LINE_LEN);
			line_next = (line_next + 1) % MAX_LINES;
			if (line_count < MAX_LINES)
				line_count++;
		}
		p = *e ? e + 1 : e;
	}
	scroll = 0;
	InterlockedExchange(&dirty, 1);
	LeaveCriticalSection(&lock);
}

static void printf_line(const char *fmt, ...)
{
	char buf[LINE_LEN * 2];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	console_print(buf);
}

// ---------------------------------------------------------------- the lists (what the original console printed with cmdlist, dvarlist, dvardump and find)

// a filter matches by wildcard (* and ?) when it has one, else as a part of the name; case does not matter
static int glob_match(const char *p, const char *s)
{
	for (; *p; p++, s++) {
		if (*p == '*') {
			while (p[1] == '*')
				p++;
			if (!p[1])
				return 1;
			for (; *s; s++)
				if (glob_match(p + 1, s))
					return 1;
			return 0;
		}
		if (!*s || (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s)))
			return 0;
	}
	return !*s;
}

static int name_matches(const char *filter, const char *name)
{
	if (!filter || !*filter)
		return 1;
	if (strchr(filter, '*') || strchr(filter, '?'))
		return glob_match(filter, name);
	char a[96], b[96];
	int i;
	for (i = 0; filter[i] && i < 95; i++)
		a[i] = (char)tolower((unsigned char)filter[i]);
	a[i] = 0;
	for (i = 0; name[i] && i < 95; i++)
		b[i] = (char)tolower((unsigned char)name[i]);
	b[i] = 0;
	return strstr(b, a) != NULL;
}

typedef struct {
	const char *filter;
	int n, cap;
	const char **names;
	const dvar_t **dvars;
	int with_default;
} list_ctx;

static int collect_command(const char *name, void *ctx)
{
	list_ctx *l = (list_ctx *)ctx;
	if (name_matches(l->filter, name) && l->n < l->cap)
		l->names[l->n++] = name;
	return 0;
}

static int collect_dvar(dvar_t *d, void *ctx)
{
	list_ctx *l = (list_ctx *)ctx;
	if (name_matches(l->filter, d->name) && l->n < l->cap) {
		l->names[l->n] = d->name;
		l->dvars[l->n++] = d;
	}
	return 0;
}

static int by_name(const void *a, const void *b)
{
	return _stricmp(*(const char *const *)a, *(const char *const *)b);
}

static void list_commands(const char *filter)
{
	static const char *names[4096];
	list_ctx l = {filter, 0, 4096, names, NULL, 0};
	game_for_each_command(collect_command, &l);
	qsort(names, (size_t)l.n, sizeof(names[0]), by_name);
	for (int i = 0; i < l.n; i++)
		console_print(names[i]);
	printf_line("%d commands", l.n);
}

static void print_dvar_line(const dvar_t *d, int with_default)
{
	char flags[9], value[120], def[120];
	static const struct {
		unsigned bit;
		char c;
	} fl[] = {{0x1, 'A'}, {0x2, 'U'}, {0x4, 'S'}, {0x8, 'Y'}, {0x20, 'L'}, {0x40, 'R'}, {0x80, 'I'}, {0x100, 'C'}};
	int n = 0;
	for (size_t k = 0; k < sizeof(fl) / sizeof(fl[0]); k++)
		flags[n++] = d->flags & fl[k].bit ? fl[k].c : '.';
	flags[n] = 0;
	game_dvar_value_text(d, &d->current, value, sizeof(value));
	if (with_default) {
		game_dvar_value_text(d, &d->reset, def, sizeof(def));
		printf_line("%s %s \"%s\"  (default \"%s\")", flags, d->name, value, def);
	} else
		printf_line("%s %s \"%s\"", flags, d->name, value);
}

static void list_dvars(const char *filter, int with_default)
{
	static const char *names[16384];
	static const dvar_t *dvars[16384];
	list_ctx l = {filter, 0, 16384, names, dvars, with_default};
	game_for_each_dvar(collect_dvar, &l);
	// sort the dvars by name (the parallel array follows the names)
	for (int i = 1; i < l.n; i++) {
		const char *nm = names[i];
		const dvar_t *dv = dvars[i];
		int j = i - 1;
		while (j >= 0 && _stricmp(names[j], nm) > 0) {
			names[j + 1] = names[j];
			dvars[j + 1] = dvars[j];
			j--;
		}
		names[j + 1] = nm;
		dvars[j + 1] = dv;
	}
	for (int i = 0; i < l.n; i++)
		print_dvar_line(dvars[i], with_default);
	printf_line("%d dvars  (flags: A archived, U userinfo, S serverinfo, Y systeminfo, L latched, R read-only, I init-only, C cheat)", l.n);
}

static void find_all(const char *text)
{
	if (!*text) {
		console_print("find <text>: the commands and dvars with that in their name");
		return;
	}
	list_commands(text);
	list_dvars(text, 0);
}

static void dump_console(const char *name)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	char file[96];
	snprintf(file, sizeof(file), "mw2coop\\%s.txt", name && *name ? name : "console_dump");
	lstrcatA(path, file);
	FILE *f = fopen(path, "w");
	if (!f) {
		printf_line("^1condump: cannot write %s", file);
		return;
	}
	EnterCriticalSection(&lock);
	for (int i = 0; i < line_count; i++) {
		int idx = (line_next - line_count + i + MAX_LINES * 2) % MAX_LINES;
		fprintf(f, "%s\n", lines[idx]);
	}
	LeaveCriticalSection(&lock);
	fclose(f);
	printf_line("console written to %s", file);
}

// Tab: completes the word at the start of the line to a command or dvar name; with several candidates, prints them and completes what they share
typedef struct {
	const char *prefix;
	size_t len;
	int n;
	char common[96];
	char shown[24][64];
} comp_ctx;

static void comp_add(comp_ctx *c, const char *name)
{
	if (_strnicmp(name, c->prefix, c->len))
		return;
	if (c->n == 0)
		lstrcpynA(c->common, name, sizeof(c->common));
	else {
		size_t k = 0;
		while (c->common[k] && name[k] && tolower((unsigned char)c->common[k]) == tolower((unsigned char)name[k]))
			k++;
		c->common[k] = 0;
	}
	if (c->n < 24)
		lstrcpynA(c->shown[c->n], name, 64);
	c->n++;
}

static int comp_command(const char *name, void *ctx)
{
	comp_add((comp_ctx *)ctx, name);
	return 0;
}

static int comp_dvar(dvar_t *d, void *ctx)
{
	comp_add((comp_ctx *)ctx, d->name);
	return 0;
}

static void tab_complete(void)
{
	char word[64];
	int n = 0;
	while (n < input_len && input[n] != ' ' && n < 63)
		word[n] = input[n], n++;
	word[n] = 0;
	if (!n || cursor > n)
		return;
	comp_ctx c;
	memset(&c, 0, sizeof(c));
	c.prefix = word;
	c.len = (size_t)n;
	static const char *const own[] = {"cmdlist", "commands", "dvarlist", "dvardump", "find", "echo", "condump", "clear", "help"};
	for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++)
		comp_add(&c, own[i]);
	game_for_each_command(comp_command, &c);
	game_for_each_dvar(comp_dvar, &c);
	if (!c.n)
		return;
	if (c.n > 1) {
		char row[LINE_LEN] = "";
		for (int i = 0; i < c.n && i < 24; i++) {
			if (strlen(row) + strlen(c.shown[i]) + 3 >= sizeof(row)) {
				console_print(row);
				row[0] = 0;
			}
			lstrcatA(row, c.shown[i]);
			lstrcatA(row, "  ");
		}
		console_print(row);
		if (c.n > 24)
			printf_line("... %d candidates", c.n);
	}
	int add = (int)strlen(c.common);
	if (add < n || input_len - n + add > INPUT_LEN)
		return;
	memmove(input + add, input + n, (size_t)(input_len - n + 1));
	memcpy(input, c.common, (size_t)add);
	input_len += add - n;
	cursor = add;
	if (c.n == 1 && input[add] != ' ' && input_len < INPUT_LEN) {
		memmove(input + add + 1, input + add, (size_t)(input_len - add + 1));
		input[add] = ' ';
		input_len++;
		cursor = add + 1;
	}
}

// ---------------------------------------------------------------- the commands

static void run_line(char *cmd)
{
	while (*cmd == ' ')
		cmd++;
	char *end = cmd + strlen(cmd);
	while (end > cmd && end[-1] == ' ')
		*--end = 0;
	if (!*cmd)
		return;
	printf_line("^2> %s", cmd);
	if (history_count == 0 || strcmp(history[(history_count - 1) % HISTORY], cmd))
		lstrcpynA(history[history_count++ % HISTORY], cmd, INPUT_LEN + 1);
	history_pos = history_count;
	if (!strcmp(cmd, "clear") || !strcmp(cmd, "cls")) {
		EnterCriticalSection(&lock);
		line_count = line_next = 0;
		scroll = 0;
		LeaveCriticalSection(&lock);
		return;
	}
	if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
		console_print("`/~ opens and closes the console, Enter runs a line, Esc closes it");
		console_print("up/down: earlier lines, PgUp/PgDn: scroll, clear: empties the panel");
		console_print("<dvar>: shows its value, <dvar> <value>: sets it, anything else goes to the game's commands");
		console_print("cmdlist [filter], dvarlist [filter], dvardump [filter], find <text>: the lists (a filter: part of a name, or with * and ?)");
		console_print("echo <text>, condump [file]: writes this panel to mw2coop\\<file>.txt, Tab: completes a command or dvar name");
		return;
	}
	char name[64];
	int n = 0;
	while (cmd[n] && cmd[n] != ' ' && n < (int)sizeof(name) - 1)
		name[n] = cmd[n], n++;
	name[n] = 0;
	const char *rest = cmd + n;
	while (*rest == ' ')
		rest++;
	// the console's own commands (the game's versions print through a function this build no longer has)
	if (!_stricmp(name, "cmdlist") || !_stricmp(name, "commands")) {
		list_commands(rest);
		return;
	}
	if (!_stricmp(name, "dvarlist")) {
		list_dvars(rest, 0);
		return;
	}
	if (!_stricmp(name, "dvardump")) {
		list_dvars(rest, 1);
		return;
	}
	if (!_stricmp(name, "find")) {
		find_all(rest);
		return;
	}
	if (!_stricmp(name, "echo")) {
		console_print(rest);
		return;
	}
	if (!_stricmp(name, "condump")) {
		dump_console(rest);
		return;
	}
	if (game_find_dvar(name) && !game_command_exists(name)) {
		char value[160];
		if (!*rest) {
			dvar_t *d = game_find_dvar(name);
			char def[160];
			game_dvar_value_text(d, &d->current, value, sizeof(value));
			game_dvar_value_text(d, &d->reset, def, sizeof(def));
			printf_line("\"%s\" is: \"%s^7\" default: \"%s^7\"", d->name, value, def);
			return;
		}
		char set[INPUT_LEN + 16];
		snprintf(set, sizeof(set), "set %s %s\n", name, rest);
		game_cbuf_addtext(set);
		game_dvar_string(name, value, sizeof(value)); // (the command runs a frame later: this may still be the old value)
		printf_line("\"%s\" set to \"%s\"", name, rest);
		return;
	}
	if (!game_command_exists(name) && strcmp(name, "set") && strcmp(name, "seta") && strcmp(name, "exec") && strcmp(name, "wait"))
		printf_line("^1unknown command or dvar \"%s\" (sent to the game anyway)", name);
	char cb[INPUT_LEN + 2];
	snprintf(cb, sizeof(cb), "%s\n", cmd);
	game_cbuf_addtext(cb);
}

// ---------------------------------------------------------------- the panel

static HWND game_window;
static WNDPROC game_proc;

// The game turns the view by how far the cursor is from the point it keeps putting it back to (GetCursorPos / SetCursorPos, no raw input). While the console is open
// its GetCursorPos answers with that very point, so nothing turns, and the real cursor is left alone.
static volatile LONG keep_x, keep_y, keep_set;

static BOOL WINAPI my_set_cursor_pos(int x, int y)
{
	keep_x = x, keep_y = y, keep_set = 1;
	return SetCursorPos(x, y);
}

static BOOL WINAPI my_get_cursor_pos(POINT *p)
{
	if (is_open && keep_set && p) {
		p->x = keep_x, p->y = keep_y;
		return TRUE;
	}
	return GetCursorPos(p);
}

static void iat_patch(HMODULE module, const char *dll, const char *func, void *replacement)
{
	uint8_t *base = (uint8_t *)module;
	if (!base)
		return;
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
			log_printf("console: %s redirected in module %p", func, (void *)module);
			return;
		}
	}
}

static void clip_mouse(int on)
{
	(void)on; // (nothing to do: my_get_cursor_pos looks at is_open)
}

static void set_open(int on)
{
	if (on == (int)is_open)
		return;
	if (on) { // (the Present hooks go in the first time the console opens, long after the Steam overlay put its own on the same functions: set at the start, the two call each other
		  // round until the stack is gone, 0xc00000fd inside gameoverlayrenderer)
		static int hooked;
		if (!hooked) {
			hooked = 1;
			native_present_hooks();
		}
	}
	InterlockedExchange(&is_open, on);
	InterlockedExchange(&dirty, 1);
	clip_mouse(on);
	log_printf("console: %s", on ? "open" : "closed");
}

static void edit_key(WPARAM vk)
{
	EnterCriticalSection(&lock);
	switch (vk) {
	case VK_RETURN: {
		char cmd[INPUT_LEN + 1];
		lstrcpynA(cmd, input, sizeof(cmd));
		input[0] = 0, input_len = cursor = 0;
		LeaveCriticalSection(&lock);
		run_line(cmd);
		InterlockedExchange(&dirty, 1);
		return;
	}
	case VK_BACK:
		if (cursor > 0) {
			memmove(input + cursor - 1, input + cursor, (size_t)(input_len - cursor + 1));
			cursor--, input_len--;
		}
		break;
	case VK_DELETE:
		if (cursor < input_len) {
			memmove(input + cursor, input + cursor + 1, (size_t)(input_len - cursor));
			input_len--;
		}
		break;
	case VK_LEFT:
		if (cursor > 0)
			cursor--;
		break;
	case VK_RIGHT:
		if (cursor < input_len)
			cursor++;
		break;
	case VK_HOME:
		cursor = 0;
		break;
	case VK_END:
		cursor = input_len;
		break;
	case VK_UP:
		if (history_pos > 0 && history_pos > history_count - HISTORY) {
			history_pos--;
			lstrcpynA(input, history[history_pos % HISTORY], INPUT_LEN + 1);
			input_len = cursor = (int)strlen(input);
		}
		break;
	case VK_DOWN:
		if (history_pos < history_count) {
			history_pos++;
			if (history_pos < history_count)
				lstrcpynA(input, history[history_pos % HISTORY], INPUT_LEN + 1);
			else
				input[0] = 0;
			input_len = cursor = (int)strlen(input);
		}
		break;
	case VK_TAB:
		tab_complete();
		break;
	case VK_PRIOR:
		scroll += 8;
		if (scroll > line_count)
			scroll = line_count;
		break;
	case VK_NEXT:
		scroll -= 8;
		if (scroll < 0)
			scroll = 0;
		break;
	}
	InterlockedExchange(&dirty, 1);
	LeaveCriticalSection(&lock);
}

static void edit_char(WPARAM ch)
{
	if (ch < 32 || ch > 126)
		return;
	EnterCriticalSection(&lock);
	if (input_len < INPUT_LEN) {
		memmove(input + cursor + 1, input + cursor, (size_t)(input_len - cursor + 1));
		input[cursor++] = (char)ch;
		input_len++;
	}
	InterlockedExchange(&dirty, 1);
	LeaveCriticalSection(&lock);
}

static LRESULT CALLBACK console_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
	int scan = (int)((l >> 16) & 0xff);
	if (msg == WM_KEYDOWN) {
		static int logged;
		if (logged < 12 && scan != 0 && w != VK_RETURN) {
			logged++;
			log_printf("console: key down vk %02x scan %02x", (unsigned)w, (unsigned)scan);
		}
	}
	if (msg == WM_KEYDOWN && (scan == SCAN_TILDE || w == 0xc0) && !(l & 0x40000000)) {
		set_open(!is_open);
		InterlockedExchange(&ignore_tilde, 1);
		return 0;
	}
	if (is_open) {
		switch (msg) {
		case WM_KEYDOWN:
			if (w == VK_ESCAPE)
				set_open(0);
			else
				edit_key(w);
			return 0;
		case WM_SYSKEYDOWN:
		case WM_SYSCHAR:
			return 0;
		case WM_CHAR:
			if (ignore_tilde) {
				InterlockedExchange(&ignore_tilde, 0);
				if (w == '`' || w == '~')
					return 0;
			}
			edit_char(w);
			return 0;
		case WM_INPUT: // raw mouse movement: the game must not turn
			return DefWindowProcA(h, msg, w, l);
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEWHEEL:
			if (msg == WM_MOUSEWHEEL)
				edit_key((short)HIWORD(w) > 0 ? VK_PRIOR : VK_NEXT);
			return 0;
		case WM_KILLFOCUS:
			clip_mouse(0);
			break;
		case WM_SETFOCUS:
			clip_mouse(1);
			break;
		}
	} else if (msg == WM_CHAR && ignore_tilde) {
		InterlockedExchange(&ignore_tilde, 0);
		if (w == '`' || w == '~')
			return 0;
	}
	return CallWindowProcA(game_proc, h, msg, w, l);
}

// ---------------------------------------------------------------- drawing

static HDC mem_dc;
static HBITMAP dib;
static uint32_t *dib_bits;
static HFONT font;
static int dib_w, dib_h;
static IDirect3DTexture9 *tex;
static int tex_w, tex_h;

static int panel_height(int h)
{
	int ph = h * 45 / 100;
	return ph < 220 ? (h < 220 ? h : 220) : ph;
}

static void paint(int w, int h)
{
	if (dib_w != w || dib_h != h) {
		if (dib)
			DeleteObject(dib);
		BITMAPINFO bi;
		memset(&bi, 0, sizeof(bi));
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		dib = CreateDIBSection(mem_dc, &bi, DIB_RGB_COLORS, (void **)&dib_bits, NULL, 0);
		SelectObject(mem_dc, dib);
		dib_w = w, dib_h = h;
	}
	if (!font) {
		font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
		SelectObject(mem_dc, font);
		SetBkMode(mem_dc, TRANSPARENT);
	}
	RECT all = {0, 0, w, h};
	HBRUSH back = CreateSolidBrush(RGB(12, 14, 20));
	FillRect(mem_dc, &all, back);
	DeleteObject(back);
	HBRUSH edge = CreateSolidBrush(RGB(60, 70, 90));
	RECT line = {0, h - 2, w, h};
	FillRect(mem_dc, &line, edge);
	DeleteObject(edge);
	static const COLORREF palette[10] = {RGB(0, 0, 0), RGB(255, 80, 80), RGB(90, 230, 90), RGB(240, 230, 90), RGB(100, 140, 255), RGB(90, 220, 230), RGB(230, 110, 230), RGB(235, 235, 235), RGB(235, 235, 235), RGB(150, 150, 150)};
	const int lh = 19, margin = 8;
	int rows = (h - 12 - lh * 2) / lh;
	EnterCriticalSection(&lock);
	SetTextColor(mem_dc, RGB(130, 150, 190));
	TextOutA(mem_dc, margin, 3, "MW2 Enhanced console  (` closes, help lists the keys)", 53);
	int shown = 0;
	for (int k = 0; k < rows && k + scroll < line_count; k++) {
		int idx = (line_next - 1 - scroll - k + MAX_LINES * 2) % MAX_LINES;
		const char *s = lines[idx];
		char text[LINE_LEN];
		COLORREF col = palette[7];
		int n = 0;
		for (const char *c = s; *c && n < LINE_LEN - 1; c++) {
			if (*c == '^' && c[1] >= '0' && c[1] <= '9') {
				col = palette[c[1] - '0'];
				c++;
				continue;
			}
			text[n++] = *c;
		}
		text[n] = 0;
		int y = h - 6 - lh * 2 - lh * k;
		SetTextColor(mem_dc, col);
		TextOutA(mem_dc, margin, y, text, n);
		shown++;
	}
	(void)shown;
	int y = h - 6 - lh;
	SetTextColor(mem_dc, RGB(240, 240, 240));
	char prompt[INPUT_LEN + 4];
	snprintf(prompt, sizeof(prompt), "> %s", input);
	TextOutA(mem_dc, margin, y, prompt, (int)strlen(prompt));
	if ((GetTickCount() / 500) & 1) {
		SIZE sz;
		GetTextExtentPoint32A(mem_dc, prompt, 2 + cursor, &sz);
		RECT cur = {margin + sz.cx, y + 2, margin + sz.cx + 2, y + lh - 2};
		HBRUSH cb = CreateSolidBrush(RGB(240, 240, 240));
		FillRect(mem_dc, &cur, cb);
		DeleteObject(cb);
	}
	LeaveCriticalSection(&lock);
	GdiFlush();
	for (int i = 0; i < w * h; i++)
		dib_bits[i] |= 0xE8000000u; // the whole panel a little see-through
}

typedef struct {
	float x, y, z, rhw, u, v;
} quad_vertex;

static void draw_panel(IDirect3DDevice9 *dev)
{
	IDirect3DSurface9 *bb = NULL;
	D3DSURFACE_DESC desc;
	if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) {
		log_printf("console: no back buffer");
		return;
	}
	IDirect3DSurface9_GetDesc(bb, &desc);
	IDirect3DSurface9_Release(bb);
	int w = (int)desc.Width, h = panel_height((int)desc.Height);
	{
		static int logged;
		if (logged++ < 3)
			log_printf("console: drawing the panel, back buffer %ux%u format %d, panel %dx%d", desc.Width, desc.Height, (int)desc.Format, w, h);
	}
	if (w < 64 || h < 64)
		return;
	if (!mem_dc)
		mem_dc = CreateCompatibleDC(NULL);
	if (!tex || tex_w != w || tex_h != h) {
		if (tex)
			IDirect3DTexture9_Release(tex);
		tex = NULL;
		if (FAILED(IDirect3DDevice9_CreateTexture(dev, (UINT)w, (UINT)h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL))) {
			static int logged;
			if (!logged++)
				log_printf("console: no texture for the panel (%dx%d)", w, h);
			tex = NULL;
			return;
		}
		tex_w = w, tex_h = h;
		InterlockedExchange(&dirty, 1);
	}
	static DWORD blink;
	DWORD phase = GetTickCount() / 500;
	if (phase != blink) {
		blink = phase;
		InterlockedExchange(&dirty, 1);
	}
	if (InterlockedExchange(&dirty, 0)) {
		paint(w, h);
		D3DLOCKED_RECT lr;
		if (SUCCEEDED(IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0))) {
			for (int y = 0; y < h; y++)
				memcpy((uint8_t *)lr.pBits + (size_t)y * (size_t)lr.Pitch, dib_bits + (size_t)y * (size_t)w, (size_t)w * 4);
			IDirect3DTexture9_UnlockRect(tex, 0);
		}
	}
	IDirect3DStateBlock9 *sb = NULL;
	if (FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &sb)) || !sb) {
		log_printf("console: no state block");
		return;
	}
	IDirect3DStateBlock9_Capture(sb);
	IDirect3DSurface9 *rt = NULL;
	IDirect3DDevice9_GetRenderTarget(dev, 0, &rt);
	IDirect3DDevice9_BeginScene(dev);
	D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
	IDirect3DDevice9_SetViewport(dev, &vp);
	IDirect3DDevice9_SetPixelShader(dev, NULL);
	IDirect3DDevice9_SetVertexShader(dev, NULL);
	IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
	IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tex);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_FILLMODE, D3DFILL_SOLID);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xf);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_SRGBWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	float x1 = (float)w - 0.5f, y1 = (float)h - 0.5f;
	quad_vertex q[4] = {{-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f}, {x1, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f}, {-0.5f, y1, 0.0f, 1.0f, 0.0f, 1.0f}, {x1, y1, 0.0f, 1.0f, 1.0f, 1.0f}};
	HRESULT dr = IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(quad_vertex));
	{
		static int logged;
		if (logged++ < 3)
			log_printf("console: quad drawn, result %lx", (unsigned long)dr);
	}
	IDirect3DDevice9_EndScene(dev);
	IDirect3DStateBlock9_Apply(sb);
	IDirect3DStateBlock9_Release(sb);
	if (rt)
		IDirect3DSurface9_Release(rt);
}

// Called by native.c's Present hooks (the vtable patches that also keep the Steam overlay's hooks from calling each other round): once per frame, on the render thread.
// Which of the three hooks the game really goes through is the first one that calls.
void console_present(int via, void *devp)
{
	static int draw_via = -1;
	if (draw_via < 0)
		draw_via = via;
	if (via != draw_via || !is_open)
		return;
	IDirect3DDevice9 *dev = (IDirect3DDevice9 *)devp;
	{
		static int logged;
		if (logged++ < 3)
			log_printf("console: present hook %d called while open (thread %lu)", via, (unsigned long)GetCurrentThreadId());
	}
	draw_panel(dev);
	if (dump_name[0]) { // QA: the frame with the panel on it
		char name[64];
		lstrcpynA(name, dump_name, sizeof(name));
		dump_name[0] = 0;
		shot_save_now(dev, name);
	}
}

// ---------------------------------------------------------------- the setup

static BOOL CALLBACK find_window_cb(HWND h, LPARAM out)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(h, &pid);
	if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
		return TRUE;
	RECT r;
	GetClientRect(h, &r);
	HWND *best = (HWND *)out;
	RECT b;
	if (r.right < 320 || r.bottom < 200)
		return TRUE;
	if (*best) {
		GetClientRect(*best, &b);
		if ((long)r.right * r.bottom <= (long)b.right * b.bottom)
			return TRUE;
	}
	*best = h;
	return TRUE;
}

static void *game_device(void)
{
#ifdef _WIN64
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	return *(void **)(base + 0x410fd20); // the 64-bit game keeps its device here (native.c RVA_D3D_DEVICE)
#else
	return d3dcap_device();
#endif
}

static DWORD WINAPI setup_thread(LPVOID unused)
{
	(void)unused;
	IDirect3DDevice9 *dev = NULL;
	for (int i = 0; i < 6000 && !dev; i++) {
		Sleep(100);
		dev = (IDirect3DDevice9 *)game_device();
		if (dev) {
			HWND first = NULL;
			EnumWindows(find_window_cb, (LPARAM)&first);
			if (!first)
				dev = NULL;
		}
	}
	if (!dev) {
		log_printf("console: the game's device or window never appeared");
		return 0;
	}
	console_print("^5MW2 Enhanced console^7: type help");
	{ // the cursor the game reads its mouse movement from (the exe's imports; a client that maps the game itself, iw4x-sp, has them in the game's own image)
		HMODULE mods[2] = {GetModuleHandleA(NULL), GetModuleHandleA("iw4sp.exe")};
		for (int m = 0; m < 2; m++) {
			if (m == 1 && mods[1] == mods[0])
				break;
			iat_patch(mods[m], "USER32.dll", "GetCursorPos", (void *)my_get_cursor_pos);
			iat_patch(mods[m], "USER32.dll", "SetCursorPos", (void *)my_set_cursor_pos);
		}
	}
	// the game may make its window again (a video restart): the watcher puts the console's procedure on whichever window is the game's
	for (;;) {
		HWND now = NULL;
		EnumWindows(find_window_cb, (LPARAM)&now);
		if (now && (now != game_window || (WNDPROC)GetWindowLongPtrA(now, GWLP_WNDPROC) != (WNDPROC)console_proc)) {
			if ((WNDPROC)GetWindowLongPtrA(now, GWLP_WNDPROC) != (WNDPROC)console_proc) {
				game_proc = (WNDPROC)SetWindowLongPtrA(now, GWLP_WNDPROC, (LONG_PTR)console_proc);
				log_printf("console: ready (device %p, window %p was %p, ` opens it)", (void *)dev, (void *)now, (void *)game_proc);
			}
			if (now != game_window && is_open)
				set_open(0);
			game_window = now;
		}
		Sleep(250);
	}
	return 0;
}

static void condump_f(void)
{
	lstrcpynA(dump_name, game_cmd_argv(1), sizeof(dump_name));
}
static cmd_function_t condump_cmd = {NULL, "mw2coop_condump", NULL, NULL, condump_f CMD_NODE_FLAG};

void console_install(void)
{
	game_add_command(&condump_cmd);
	InitializeCriticalSection(&lock);
	CloseHandle(CreateThread(NULL, 0, setup_thread, NULL, 0, NULL));
}
