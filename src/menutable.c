// The menus the mod adds, read from a .menu file (content/release/menus.menu) instead of arrays in menus.c. See menutable.h.
// The built-in copy is parsed first, then mw2coop\menus.menu next to the game, if there is one: its items replace the text and action of the built-in
// item of the same menu and name (an item with a new name is ignored: the code asks for the items it knows), and its title replaces a popup's.
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menutable.h"
#include "log.h"

#include "menus_gen.h"

#define MAX_ITEMS 48
#define MAX_MENUS 12

static MenuItem items[MAX_ITEMS];
static int item_count;
static struct {
	char menu[40], title[80];
} titles[MAX_MENUS];
static int title_count;
static volatile LONG loaded; // 0 not yet, 1 loading, 2 done

typedef struct {
	const char *p;
	char word[200];
	int quoted;
} Lexer;

// the next token: a quoted string, a brace, a ';' or a bare word. Returns 0 at the end. // starts a comment.
static int next_token(Lexer *lx)
{
	const char *p = lx->p;
	for (;;) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n')
				p++;
			continue;
		}
		break;
	}
	lx->quoted = 0;
	if (!*p) {
		lx->p = p;
		return 0;
	}
	size_t n = 0;
	if (*p == '"') {
		lx->quoted = 1;
		p++;
		while (*p && *p != '"') {
			if (n + 1 < sizeof(lx->word))
				lx->word[n++] = *p;
			p++;
		}
		if (*p == '"')
			p++;
	} else if (*p == '{' || *p == '}' || *p == ';') {
		lx->word[n++] = *p++;
	} else {
		while (*p && !isspace((unsigned char)*p) && *p != '{' && *p != '}' && *p != ';' && *p != '"') {
			if (n + 1 < sizeof(lx->word))
				lx->word[n++] = *p;
			p++;
		}
	}
	lx->word[n] = 0;
	lx->p = p;
	return 1;
}

static int is_punct(const Lexer *lx, char c)
{
	return !lx->quoted && lx->word[0] == c && !lx->word[1];
}

static int is_num(const char *s)
{
	if (*s == '-')
		s++;
	if (!*s)
		return 0;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s) && *s != '.')
			return 0;
	return 1;
}

// the body of action { ... } (the '{' was read): "cmd" "arg" ; ... in the game's parsed form
static void read_action(Lexer *lx, char *out, size_t size)
{
	size_t n = 0;
	out[0] = 0;
	int first = 1, depth = 1;
	while (depth && next_token(lx)) {
		if (is_punct(lx, '{'))
			depth++;
		else if (is_punct(lx, '}')) {
			if (--depth == 0)
				break;
		} else if (is_punct(lx, ';')) {
			if (!first && n + 3 < size) {
				memcpy(out + n, "; ", 3);
				n += 2;
			}
			first = 1;
		} else {
			// a token of a command; numbers stay bare, everything else is quoted, as the game's own scripts have it
			int w = (is_num(lx->word) && !lx->quoted) ? snprintf(out + n, size - n, "%s ", lx->word) : snprintf(out + n, size - n, "\"%s\" ", lx->word);
			if (w < 0 || (size_t)w >= size - n)
				break;
			n += (size_t)w;
			first = 0;
		}
	}
	if (!first && n + 3 < size) { // a last command without ';'
		memcpy(out + n, "; ", 3);
		n += 2;
	}
	out[n] = 0;
}

static void skip_block(Lexer *lx) // after a '{'
{
	int depth = 1;
	while (depth && next_token(lx)) {
		if (is_punct(lx, '{'))
			depth++;
		else if (is_punct(lx, '}'))
			depth--;
	}
}

static MenuItem *find_item(const char *menu, const char *name)
{
	for (int i = 0; i < item_count; i++)
		if (!_stricmp(items[i].menu, menu) && !_stricmp(items[i].name, name))
			return &items[i];
	return NULL;
}

// replace_only: a file over the built-in copy; items and menus the built-in copy lacks are ignored
static int parse(const char *text, int replace_only)
{
	Lexer lx = {text, "", 0};
	int read = 0;
	while (next_token(&lx)) {
		if (lx.quoted || strcmp(lx.word, "menuDef"))
			continue;
		if (!next_token(&lx) || !is_punct(&lx, '{'))
			continue;
		char menu[40] = "";
		int depth = 1;
		while (depth && next_token(&lx)) {
			if (is_punct(&lx, '}')) {
				depth--;
			} else if (is_punct(&lx, '{')) {
				skip_block(&lx);
			} else if (!lx.quoted && !strcmp(lx.word, "name")) {
				if (next_token(&lx))
					lstrcpynA(menu, lx.word, sizeof(menu));
			} else if (!lx.quoted && !strcmp(lx.word, "title")) {
				if (next_token(&lx) && menu[0]) {
					int t;
					for (t = 0; t < title_count; t++)
						if (!_stricmp(titles[t].menu, menu))
							break;
					if (t < title_count || (!replace_only && title_count < MAX_MENUS)) {
						if (t == title_count) {
							title_count++;
							lstrcpynA(titles[t].menu, menu, sizeof(titles[t].menu));
						}
						lstrcpynA(titles[t].title, lx.word, sizeof(titles[t].title));
						read++;
					}
				}
			} else if (!lx.quoted && !strcmp(lx.word, "itemDef")) {
				if (!next_token(&lx) || !is_punct(&lx, '{'))
					continue;
				char name[40] = "", label[80] = "", action[240] = "";
				int have_text = 0, have_action = 0, d = 1;
				while (d && next_token(&lx)) {
					if (is_punct(&lx, '}')) {
						d--;
					} else if (is_punct(&lx, '{')) {
						skip_block(&lx);
					} else if (!lx.quoted && !strcmp(lx.word, "name")) {
						if (next_token(&lx))
							lstrcpynA(name, lx.word, sizeof(name));
					} else if (!lx.quoted && !strcmp(lx.word, "text")) {
						if (next_token(&lx)) {
							lstrcpynA(label, lx.word, sizeof(label));
							have_text = 1;
						}
					} else if (!lx.quoted && !strcmp(lx.word, "action")) {
						if (next_token(&lx) && is_punct(&lx, '{')) {
							read_action(&lx, action, sizeof(action));
							have_action = 1;
						}
					}
				}
				if (!menu[0] || !name[0])
					continue;
				MenuItem *it = find_item(menu, name);
				if (!it) {
					if (replace_only || item_count >= MAX_ITEMS)
						continue;
					it = &items[item_count++];
					lstrcpynA(it->menu, menu, sizeof(it->menu));
					lstrcpynA(it->name, name, sizeof(it->name));
				}
				if (have_text || !replace_only)
					lstrcpynA(it->text, label, sizeof(it->text));
				if (have_action || !replace_only)
					lstrcpynA(it->action, action, sizeof(it->action));
				read++;
			}
		}
	}
	return read;
}

static void read_file(void)
{
	char path[MAX_PATH];
	DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
	char *slash = length && length < sizeof(path) ? strrchr(path, '\\') : NULL;
	if (!slash)
		return;
	slash[1] = 0;
	strncat(path, "mw2coop\\menus.menu", sizeof(path) - strlen(path) - 1);
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	static char buf[1 << 16];
	DWORD got = 0;
	if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL))
		got = 0;
	CloseHandle(h);
	buf[got] = 0;
	log_printf("menus: menus.menu: %d items and titles read", parse(buf, 1));
}

static void load(void)
{
	if (loaded == 2)
		return;
	if (InterlockedCompareExchange(&loaded, 1, 0) == 0) {
		parse(built_in_menu, 0);
		read_file();
		InterlockedExchange(&loaded, 2);
	} else
		while (loaded != 2)
			Sleep(1);
}

const MenuItem *menutable_item(const char *menu, int n)
{
	load();
	for (int i = 0; i < item_count; i++)
		if (!_stricmp(items[i].menu, menu) && n-- == 0)
			return &items[i];
	return NULL;
}

const char *menutable_title(const char *menu)
{
	load();
	for (int i = 0; i < title_count; i++)
		if (!_stricmp(titles[i].menu, menu))
			return titles[i].title;
	return NULL;
}
