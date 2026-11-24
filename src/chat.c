// The text chat of a network co-op session (owner 2026-11-24: "a text chat, opened with T").
//
// T ([chat] key, bound once to mw2coop_chat where the player's config binds that key to nothing else) opens a line at the left of the view while a network
// co-op level runs. Enter sends it to the partner, Esc closes it, Backspace takes back a letter (with Ctrl a word), Ctrl+V pastes. Both PCs show "<name>: <text>"
// over the line, at the left of the view about its middle (owner: not at the top left, where the game's own messages and hints go): a line stays 10 s and fades,
// and while the line is open the last few of the past 3 minutes stay.
// While the line is open it takes the keyboard: console.c's window procedure gives it the game window's WM_KEYDOWN and WM_CHAR first. The game still gets the key
// releases (WM_KEYUP), so a key held when the line opened (W to walk) is let go in the game as well; the mouse stays the game's.
// The chat is drawn with the game's own text draw (CL_DrawTextPhysical, the one the co-op markers' distance uses) after the cgame's frame (native.c, cg_draw_hook),
// in the game's normal font. The Russian game's fonts are Windows-1251, the others' 1252: a letter the game's code page has no place for shows as '?'.
// Typed letters: the game window is an ANSI window, so WM_CHAR holds the letter in the code page of the keyboard layout in use (user32 converts with the thread's
// code page, which follows the layout); the line turns it into UTF-16 with that code page.
// Transport: the mod's out-of-band packet "mw2coop_t" on the game's socket (LAN, VPN, UPnP and the Steam relay alike): this game's random id, a sequence number,
// the sender's name and the text in UTF-8, sent three times; the receiver drops the copies it has seen. The log holds no chat text (bug reports carry the log):
// MW2COOP_CHAT_LOG=1 (QA) logs it.
// Nothing in the split screen (both players at one PC) or outside a level.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "chat.h"
#include "ctexts.h"
#include "game.h"
#include "localize.h"
#include "log.h"
#include "native.h"
#include "net.h"
#include "nick.h"
#include "rejoin.h"
#include "settings.h"

#ifdef _WIN64
#define REF_TEXTDRAW 0xd2ce0	 // CL_DrawTextPhysical (text, max chars, font, x, y, x scale, y scale, colour, style) (markers.c)
#define REF_SCRPLACE 0xdaf10	 // ScreenPlacement *(int local client): scaleVirtualToReal [0..1], realViewportPosition [6..7], realViewportSize [8..9]
#define REF_DB_FIND 0x0ef5d0	 // void *DB_FindXAssetHeader(int type, const char *name) (padglyphs.c)
#define REF_CLS_STATE 0x533cb8	 // int: the client's connection state; 6 and up: a level runs
#define REF_KEY_CATCHERS 0x533cb0 // unsigned: local client 0's key catchers (0x10: a menu has the keys) (native.c)
#define FONT_HEIGHT 8		 // Font_s: name, pixelHeight, glyphCount, material, glowMaterial, glyphs
#define FONT_COUNT 12
#define FONT_GLYPHS 32
#else
#define REF_TEXTDRAW 0xb8a70	 // VA 0x4b8a70, cdecl
#define REF_SCRPLACE 0x48a30	 // VA 0x448a30, cdecl
#define REF_DB_FIND 0x0f6f10	 // VA 0x4f6f10
#define REF_CLS_STATE 0x525bc0
#define REF_KEY_CATCHERS 0x525bb8
#define FONT_HEIGHT 4
#define FONT_COUNT 8
#define FONT_GLYPHS 20
#endif
#define ASSET_FONT 24
#define GLYPH_SIZE 24 // Glyph: letter (u16, in the font's code page), x0 y0 (s8), dx (u8: the advance), ...
#define TEXT_STYLE_SHADOWED 3

static const char TAG[] = "\xff\xff\xff\xffmw2coop_t";
#define TAG_LEN ((int)sizeof(TAG) - 1)
#define LINE_MAX 120 // UTF-16 units a line holds
#define NAME_MAX 32
#define HIST 6		     // chat lines kept for the view
#define SHOW_MS 10000	     // a line stays this long after it came, the last FADE_MS fading
#define FADE_MS 1500
#define OPEN_SHOW_MS 180000 // while the line is open: the lines of the last 3 minutes

typedef void (*text_draw_t)(const char *text, int max_chars, void *font, float x, float y, float x_scale, float y_scale, const float *color, int style);

static uint8_t *base;
static int enabled = 1, ready, log_text;
static char key_name[16] = "T";

// the open line (main thread: the window procedure, the commands and the draw)
static volatile LONG open_now;
static WCHAR line[LINE_MAX + 1];
static int line_len;
static DWORD opened_at;
static int open_scan = -1; // the scan code of the key that opened the line: its WM_CHAR, when it comes after the open, is not typed
static int last_down_scan;
static DWORD last_down_at;
static void *font;

// what came from the partner (the receiver may be another thread): shown by mw2coop_chat_show on the main thread
#define QUEUE 8
static CRITICAL_SECTION rx_lock;
static struct {
	char name[NAME_MAX + 1];
	char text[LINE_MAX * 3 + 1];
} rx[QUEUE];
static int rx_head, rx_count;
static uint32_t seen_id[32];
static uint16_t seen_seq[32];
static int seen_next;
static DWORD flood_at[6];
static int flood_next;

static uint32_t my_id;
static uint16_t seq_out;

static int level_runs(void)
{
	return base && *(volatile int *)(base + XR(REF_CLS_STATE)) >= 6;
}

static int menu_open(void)
{
	return (*(volatile unsigned *)(base + XR(REF_KEY_CATCHERS)) & 0x10) != 0;
}

static int session_on(void)
{
	char coop[8] = "";
	game_dvar_raw("coop", coop, sizeof(coop));
	return enabled && ready && coop[0] == '1' && net_partner_known() && !native_split_active();
}

static UINT game_codepage(void)
{
	return localize_is_russian() ? 1251 : 1252;
}

// UTF-8 -> the game's code page, control characters and a '^' that does not start a colour (^0..^9) left out
static void to_game_text(const char *utf8, char *out, int size)
{
	WCHAR wide[LINE_MAX * 2 + 1];
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
	if (n <= 0)
		wide[0] = 0;
	wide[sizeof(wide) / sizeof(wide[0]) - 1] = 0;
	char raw[LINE_MAX * 2 + 1];
	n = WideCharToMultiByte(game_codepage(), 0, wide, -1, raw, sizeof(raw), "?", NULL);
	if (n <= 0)
		raw[0] = 0;
	raw[sizeof(raw) - 1] = 0;
	int k = 0;
	for (int i = 0; raw[i] && k + 1 < size; i++) {
		const unsigned char c = (unsigned char)raw[i];
		if (c < 0x20 || c == 0x7f)
			continue;
		if (c == '^' && !(raw[i + 1] >= '0' && raw[i + 1] <= '9'))
			continue;
		out[k++] = (char)c;
	}
	out[k] = 0;
}

static void clean_name(const char *in, char *out, int size)
{
	int k = 0;
	for (; *in && k + 1 < size; in++) {
		const unsigned char c = (unsigned char)*in;
		if (c >= 0x20 && c != 0x7f && c != '^')
			out[k++] = (char)c;
	}
	out[k] = 0;
}

// the chat's lines (main thread: show_line writes, chat_draw reads), the newest last
static struct {
	char text[LINE_MAX * 2 + NAME_MAX + 32];
	DWORD at;
} hist[HIST];
static int hist_next, hist_count;

// a chat line at the left of the view (main thread)
static void show_line(const char *name, const char *utf8, int own)
{
	char text[LINE_MAX * 2 + 1], who[NAME_MAX + 1], shown[LINE_MAX * 2 + NAME_MAX + 32];
	to_game_text(utf8, text, sizeof(text));
	clean_name(name, who, sizeof(who));
	if (!who[0]) {
		const char *partner = localize_text("MW2COOP_VOICE_PARTNER");
		lstrcpynA(who, own ? "You" : partner && partner[0] ? partner : "Partner", sizeof(who));
	}
	const char *fmt = localize_text("MW2COOP_CHAT_LINE_FMT");
	snprintf(shown, sizeof(shown), fmt && ctexts_fmt_ok(fmt, "ss") ? fmt : "^5%s:^7 %s", who, text);
	if (log_text)
		log_printf("chat: shown %s line: %s", own ? "own" : "the partner's", shown);
	else
		log_printf("chat: shown %s line (%d characters)", own ? "own" : "the partner's", (int)strlen(text));
	lstrcpynA(hist[hist_next].text, shown, sizeof(hist[hist_next].text));
	hist[hist_next].at = GetTickCount();
	hist_next = (hist_next + 1) % HIST;
	if (hist_count < HIST)
		hist_count++;
}

static void close_line(const char *why)
{
	if (InterlockedExchange(&open_now, 0))
		log_printf("chat: the line is closed (%s)", why);
	line_len = 0;
	line[0] = 0;
}

static void send_line(void)
{
	int a = 0, b = line_len;
	while (a < b && (line[a] == L' ' || line[a] == L'\t'))
		a++;
	while (b > a && (line[b - 1] == L' ' || line[b - 1] == L'\t'))
		b--;
	if (b <= a)
		return;
	char utf8[LINE_MAX * 4 + 1];
	int n = WideCharToMultiByte(CP_UTF8, 0, line + a, b - a, utf8, sizeof(utf8) - 1, NULL, NULL);
	if (n <= 0)
		return;
	utf8[n] = 0;
	char name[NAME_MAX + 1];
	rejoin_info_t r;
	const char *nick = rejoin_info(&r) ? nick_in_session(r.host) : nick_current();
	lstrcpynA(name, nick ? nick : "", sizeof(name));
	const int name_len = (int)strlen(name);
	// TAG, id (4), sequence (2), name length (1), name, text length (2), text
	uint8_t pkt[TAG_LEN + 9 + NAME_MAX + LINE_MAX * 4];
	int k = 0;
	memcpy(pkt, TAG, TAG_LEN);
	k = TAG_LEN;
	const uint16_t seq = ++seq_out;
	memcpy(pkt + k, &my_id, 4), k += 4;
	memcpy(pkt + k, &seq, 2), k += 2;
	pkt[k++] = (uint8_t)name_len;
	memcpy(pkt + k, name, name_len), k += name_len;
	const uint16_t tl = (uint16_t)n;
	memcpy(pkt + k, &tl, 2), k += 2;
	memcpy(pkt + k, utf8, n), k += n;
	int sent = 0;
	for (int i = 0; i < 3; i++)
		sent += net_send_mod_bin(pkt, k);
	log_printf("chat: line %u sent (%d characters, %d of 3 copies went out)", seq, b - a, sent);
	show_line(name, utf8, 1);
}

void chat_on_packet(const char *buf, int len)
{
	if (!enabled || len < TAG_LEN + 9 || memcmp(buf, TAG, TAG_LEN) != 0)
		return;
	const uint8_t *p = (const uint8_t *)buf + TAG_LEN, *end = (const uint8_t *)buf + len;
	uint32_t id;
	uint16_t seq, tl;
	memcpy(&id, p, 4), p += 4;
	memcpy(&seq, p, 2), p += 2;
	const int nl = *p++;
	if (nl > NAME_MAX || p + nl + 2 > end)
		return;
	const uint8_t *name = p;
	p += nl;
	memcpy(&tl, p, 2), p += 2;
	if (tl == 0 || tl > LINE_MAX * 4 || p + tl > end)
		return;
	EnterCriticalSection(&rx_lock);
	for (int i = 0; i < 32; i++)
		if (seen_id[i] == id && seen_seq[i] == seq && (id || seq)) {
			LeaveCriticalSection(&rx_lock);
			return; // a copy already taken
		}
	seen_id[seen_next] = id, seen_seq[seen_next] = seq;
	seen_next = (seen_next + 1) % 32;
	const DWORD now = GetTickCount();
	const int flood = flood_at[flood_next] && now - flood_at[flood_next] < 3000; // (a sixth line within 3 s is dropped)
	if (!flood) {
		flood_at[flood_next] = now;
		flood_next = (flood_next + 1) % 6;
		const int slot = (rx_head + rx_count) % QUEUE;
		memcpy(rx[slot].name, name, nl);
		rx[slot].name[nl] = 0;
		const int t = tl < (int)sizeof(rx[slot].text) - 1 ? tl : (int)sizeof(rx[slot].text) - 1;
		memcpy(rx[slot].text, p, t);
		rx[slot].text[t] = 0;
		if (rx_count < QUEUE)
			rx_count++;
		else
			rx_head = (rx_head + 1) % QUEUE;
	}
	LeaveCriticalSection(&rx_lock);
	if (flood)
		log_printf("chat: a line from the partner dropped (more than 5 in 3 s)");
	else
		game_cbuf_addtext("mw2coop_chat_show\n");
}

static void show_f(void)
{
	for (;;) {
		char name[NAME_MAX + 1], text[LINE_MAX * 3 + 1];
		EnterCriticalSection(&rx_lock);
		if (!rx_count) {
			LeaveCriticalSection(&rx_lock);
			return;
		}
		lstrcpynA(name, rx[rx_head].name, sizeof(name));
		lstrcpynA(text, rx[rx_head].text, sizeof(text));
		rx_head = (rx_head + 1) % QUEUE;
		rx_count--;
		LeaveCriticalSection(&rx_lock);
		show_line(name, text, 0);
	}
}

// T: the line opens (the key's command, main thread)
static void chat_f(void)
{
	if (!enabled)
		return;
	if (!session_on() || !level_runs()) {
		static int said;
		if (!said++)
			log_printf("chat: the chat key works in a network co-op level only");
		return;
	}
	line_len = 0;
	line[0] = 0;
	opened_at = GetTickCount();
	open_scan = opened_at - last_down_at < 500 ? last_down_scan : -1;
	if (!InterlockedExchange(&open_now, 1))
		log_printf("chat: the line is open");
}

static void add_char(WCHAR c)
{
	if (c < 0x20 || c == 0x7f || line_len >= LINE_MAX)
		return;
	line[line_len++] = c;
	line[line_len] = 0;
}

static UINT layout_codepage(void)
{
	char buf[8];
	const LCID lcid = MAKELCID(LOWORD((DWORD_PTR)GetKeyboardLayout(0)), SORT_DEFAULT);
	if (GetLocaleInfoA(lcid, LOCALE_IDEFAULTANSICODEPAGE, buf, sizeof(buf)) > 0 && atoi(buf) > 0)
		return (UINT)atoi(buf);
	return CP_ACP;
}

static void paste(HWND h)
{
	if (!OpenClipboard(h))
		return;
	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	const WCHAR *t = data ? (const WCHAR *)GlobalLock(data) : NULL;
	if (t) {
		for (; *t && *t != L'\r' && *t != L'\n' && line_len < LINE_MAX; t++)
			add_char(*t == L'\t' ? L' ' : *t);
		GlobalUnlock(data);
	}
	CloseClipboard();
}

int chat_window_message(HWND h, UINT msg, WPARAM w, LPARAM l, LRESULT *result)
{
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
		last_down_scan = (int)((l >> 16) & 0xff);
		last_down_at = GetTickCount();
	}
	if (!open_now)
		return 0;
	if (!level_runs() || menu_open()) {
		close_line(level_runs() ? "a menu opened" : "the level ended");
		return 0;
	}
	switch (msg) {
	case WM_KEYDOWN: {
		const int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (w == VK_RETURN) {
			send_line();
			close_line("sent");
		} else if (w == VK_ESCAPE)
			close_line("Esc");
		else if (w == VK_BACK) {
			if (ctrl) {
				while (line_len > 0 && line[line_len - 1] == L' ')
					line_len--;
				while (line_len > 0 && line[line_len - 1] != L' ')
					line_len--;
			} else if (line_len > 0)
				line_len--;
			line[line_len] = 0;
		} else if (ctrl && w == 'V')
			paste(h);
		*result = 0;
		return 1;
	}
	case WM_CHAR: {
		const int scan = (int)((l >> 16) & 0xff);
		if (open_scan >= 0 && scan == open_scan && GetTickCount() - opened_at < 500) {
			open_scan = -1; // the letter of the key that opened the line
			*result = 0;
			return 1;
		}
		open_scan = -1;
		if (!(GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) { // (Ctrl+letter gives control codes; AltGr is Ctrl+Alt and types letters)
			const char c = (char)(unsigned char)w;
			WCHAR wide[2];
			if (MultiByteToWideChar(layout_codepage(), 0, &c, 1, wide, 2) == 1)
				add_char(wide[0]);
		}
		*result = 0;
		return 1;
	}
	case WM_DEADCHAR:
		*result = 0;
		return 1;
	}
	return 0;
}

// a font asset by name; NULL when the database gives another (its default font for a name it does not have)
static void *find_font(const char *name)
{
	void *f = ((void *(*)(int, const char *))(base + XR(REF_DB_FIND)))(ASSET_FONT, name);
	if (!f || !game_mem_readable(f, FONT_GLYPHS + sizeof(void *)))
		return NULL;
	const char *own = *(const char *const *)f;
	return own && game_string_readable(own, 64) && _stricmp(own, name) == 0 ? f : NULL;
}

// the font (the game's bold one, which its multiplayer chat uses; else the normal one) and the advance of every letter in it (its glyphs' dx, in the font's
// units), looked up once
static uint8_t adv_tab[256];
static int font_ok(void)
{
	static int failed;
	if (font)
		return 1;
	if (failed)
		return 0;
	const char *which = "fonts/boldFont";
	void *f = find_font(which);
	if (!f)
		f = find_font(which = "fonts/normalFont");
	if (!f) {
		failed = 1;
		log_printf("chat: the game's bold and normal fonts were not found: the chat is not drawn");
		return 0;
	}
	const int count = *(const int *)((const uint8_t *)f + FONT_COUNT);
	const uint8_t *glyphs = *(const uint8_t *const *)((const uint8_t *)f + FONT_GLYPHS);
	int letters = 0;
	if (glyphs && count > 0 && count < 4096 && game_mem_readable(glyphs, (size_t)count * GLYPH_SIZE))
		for (int g = 0; g < count; g++) {
			const uint16_t letter = *(const uint16_t *)(glyphs + (size_t)g * GLYPH_SIZE);
			if (letter < 256)
				adv_tab[letter] = glyphs[(size_t)g * GLYPH_SIZE + 4], letters++;
		}
	font = f;
	log_printf("chat: font %s, %d letters measured", which, letters);
	return 1;
}

static int color_code(const char *s)
{
	return s[0] == '^' && s[1] >= '0' && s[1] <= '9';
}

static float text_width(const char *s, int n, float scale)
{
	float w = 0.0f;
	for (int i = 0; i < n && s[i]; i++) {
		if (color_code(s + i)) {
			i++;
			continue;
		}
		w += adv_tab[(unsigned char)s[i]] * scale;
	}
	return w;
}

// the rows of a text at most `room` pixels wide (broken at a space, or in a word longer than a row): starts and lengths, at most `max` rows
static int wrap_rows(const char *s, float scale, float room, int *starts, int *lens, int max)
{
	int rows = 0, start = 0, space = -1;
	float w = 0.0f;
	for (int i = 0; s[i]; i++) {
		if (color_code(s + i)) {
			i++;
			continue;
		}
		if (s[i] == ' ')
			space = i;
		w += adv_tab[(unsigned char)s[i]] * scale;
		if (w > room && i > start) {
			const int end = space > start ? space : i;
			starts[rows] = start, lens[rows] = end - start;
			if (++rows == max)
				return rows;
			start = space > start ? space + 1 : i;
			space = -1;
			w = text_width(s + start, i + 1 - start, scale);
		}
	}
	if (s[start] && rows < max)
		starts[rows] = start, lens[rows] = (int)strlen(s + start), rows++;
	return rows;
}

// a text with the game's shadow (owner, 2026-11-24: the bold font, a size between the first two tried, no outline)
static void draw_text(const char *text, float x, float y, float scale, float alpha)
{
	const float color[4] = {1.0f, 1.0f, 1.0f, alpha};
	((text_draw_t)(void *)(base + XR(REF_TEXTDRAW)))(text, 0x7fffffff, font, x, y, scale, scale, color, TEXT_STYLE_SHADOWED);
}

// The chat at the left of the view, about its middle (owner, 2026-11-24: "lower, on the left, in the middle"): the lines, the newest at the bottom, and under
// them the open line. A line stays SHOW_MS after it came; while the line is open, the lines of the last OPEN_SHOW_MS. After every cgame frame (main thread).
void chat_draw(void)
{
	if (!ready || (!open_now && !hist_count))
		return;
	const int open = open_now != 0;
	if (menu_open()) {
		if (open)
			close_line("a menu opened");
		return;
	}
	const DWORD now = GetTickCount();
	if (!open && now - hist[(hist_next + HIST - 1) % HIST].at >= SHOW_MS)
		return;
	if (!font_ok())
		return;
	const float *pl = ((const float *(*)(int))(base + XR(REF_SCRPLACE)))(0);
	if (!pl || !game_mem_readable(pl, 10 * sizeof(float)))
		return;
	const float vx = pl[6], vy = pl[7], vw = pl[8], vh = pl[9];
	const int units = *(const int *)((const uint8_t *)font + FONT_HEIGHT);
	if (vw < 64.0f || vh < 64.0f || units <= 0)
		return;
	const float px = vh * 0.028f, scale = px / (float)units, lh = px * 1.25f;
	const float x = vx + vw * 0.03f, y_line = vy + vh * 0.62f;
	// the lines, from the newest up (at most 8 rows)
	float y = y_line - lh;
	int rows_left = 8;
	for (int k = 0; k < hist_count && rows_left > 0; k++) {
		const int i = (hist_next + HIST - 1 - k) % HIST;
		const DWORD age = now - hist[i].at;
		if (age >= (DWORD)(open ? OPEN_SHOW_MS : SHOW_MS))
			break;
		const float alpha = !open && age > SHOW_MS - FADE_MS ? (float)(SHOW_MS - age) / FADE_MS : 1.0f;
		int starts[4], lens[4];
		const int n = wrap_rows(hist[i].text, scale, vw * 0.45f, starts, lens, 4);
		for (int r = n - 1; r >= 0 && rows_left > 0; r--, rows_left--) {
			char colour = '7', row[sizeof(hist[0].text) + 4];
			for (int c = 0; c < starts[r] && hist[i].text[c]; c++) // (the colour in force where the row starts)
				if (color_code(hist[i].text + c))
					colour = hist[i].text[c + 1];
			snprintf(row, sizeof(row), "^%c%.*s", colour, lens[r], hist[i].text + starts[r]);
			draw_text(row, x, y, scale, alpha);
			y -= lh;
		}
	}
	if (!open)
		return;
	// the open line: the prompt, the typed text's tail that fits in 60% of the view's width, the caret (blinking)
	const char *prompt = localize_text("MW2COOP_CHAT_PROMPT");
	char head[64];
	snprintf(head, sizeof(head), "^5%s^7 ", prompt && prompt[0] ? prompt : "Chat:");
	char typed[LINE_MAX * 2 + 1];
	WCHAR shown[LINE_MAX + 1];
	lstrcpynW(shown, line, LINE_MAX + 1);
	if (WideCharToMultiByte(game_codepage(), 0, shown, -1, typed, sizeof(typed), "?", NULL) <= 0)
		typed[0] = 0;
	typed[sizeof(typed) - 1] = 0;
	const char *from = typed;
	float width = 0.0f;
	for (const char *c = typed + strlen(typed); c > typed; c--) {
		const float adv = adv_tab[(unsigned char)c[-1]] * scale;
		if (width + adv > vw * 0.6f)
			break;
		width += adv;
		from = c - 1;
	}
	char text[sizeof(head) + sizeof(typed) + 2];
	const int caret = ((now - opened_at) / 500) % 2 == 0;
	snprintf(text, sizeof(text), "%s%s%s", head, from, caret ? "_" : "");
	draw_text(text, x, y_line, scale, 1.0f);
	static int logged;
	if (!logged++)
		log_printf("chat: the line is drawn at %.0f %.0f, %.1f px high (view %.0fx%.0f)", x, y_line, px, vw, vh);
}

// the chat key: [chat] key bound once to mw2coop_chat where the player's config binds that key to nothing else
void chat_menu_tick(void)
{
	static int done;
	if (done || !enabled || !key_name[0])
		return;
	done = 1;
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "players\\config.cfg");
	FILE *f = fopen(path, "rb");
	char text[256], want[40];
	int bound = 0, taken = 0;
	snprintf(want, sizeof(want), "bind %s ", key_name);
	if (f) {
		while (fgets(text, sizeof(text), f)) {
			if (strstr(text, "mw2coop_chat"))
				bound = 1;
			else if (_strnicmp(text, want, strlen(want)) == 0)
				taken = 1;
		}
		fclose(f);
	}
	if (bound)
		return;
	if (taken) {
		log_printf("chat: %s is bound to something else: no key for the chat (bind one to mw2coop_chat, or set [chat] key)", key_name);
		return;
	}
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "bind %s mw2coop_chat\n", key_name);
	game_cbuf_addtext(cmd);
	log_printf("chat: %s opens the chat line (bind %s mw2coop_chat)", key_name, key_name);
}

// QA: mw2coop_chat_qa_type <text> (UTF-8): the text into the open line, as if typed (the letters a layout of this PC may not have)
static void qa_type_f(void)
{
	if (!open_now)
		return;
	char utf8[LINE_MAX * 4 + 1] = "";
	for (int i = 1; i < game_cmd_argc(); i++) {
		if (i > 1)
			strncat(utf8, " ", sizeof(utf8) - strlen(utf8) - 1);
		strncat(utf8, game_cmd_argv(i), sizeof(utf8) - strlen(utf8) - 1);
	}
	WCHAR wide[LINE_MAX + 1];
	const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, LINE_MAX + 1);
	for (int i = 0; i + 1 < n; i++)
		add_char(wide[i]);
	log_printf("chat: QA: %d characters put into the line", n > 0 ? n - 1 : 0);
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_chat", NULL, NULL, chat_f CMD_NODE_FLAG},
	{NULL, "mw2coop_chat_show", NULL, NULL, show_f CMD_NODE_FLAG},
	{NULL, "mw2coop_chat_qa_type", NULL, NULL, qa_type_f CMD_NODE_FLAG},
};

void chat_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	InitializeCriticalSection(&rx_lock);
	const char *ini = settings_ini_path();
	enabled = GetPrivateProfileIntA("chat", "enabled", 1, ini) != 0;
	GetPrivateProfileStringA("chat", "key", "T", key_name, sizeof(key_name), ini);
	for (char *c = key_name; *c; c++)
		if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) {
			log_printf("chat: [chat] key '%s' is not a key name: no key bound", key_name);
			key_name[0] = 0;
			break;
		}
	const char *v = getenv("MW2COOP_CHAT_LOG");
	log_text = v && *v == '1';
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	my_id = (uint32_t)(t.LowPart ^ (GetCurrentProcessId() << 16) ^ GetTickCount()) | 1u;
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
	if (!enabled) {
		log_printf("chat: off ([chat] enabled=0)");
		return;
	}
	ready = addr_resolved(REF_TEXTDRAW) && addr_resolved(REF_SCRPLACE) && addr_resolved(REF_DB_FIND) &&
		addr_resolved(REF_CLS_STATE) && addr_resolved(REF_KEY_CATCHERS);
	log_printf("chat: %s", ready ? "the text chat is on in network co-op" : "the game's text draw was not found on this build: no chat");
}
