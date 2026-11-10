// See inimerge.h.
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "inimerge.h"

typedef enum { L_OTHER, L_SECTION, L_KEY, L_CKEY } Kind;

typedef struct {
	const char *text;
	size_t len;
	Kind kind;
	char sect[64]; // the section the line is in (lower case); for L_SECTION its own name
	char key[96];  // lower case, for L_KEY and L_CKEY
} Line;

typedef struct {
	Line *v;
	int n, cap;
} Lines;

static int is_key_char(int c)
{
	return isalnum((unsigned char)c) || c == '_';
}

static void lower_copy(char *dst, size_t cap, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && i + 1 < cap; i++)
		dst[i] = (char)tolower((unsigned char)s[i]);
	dst[i] = 0;
}

static int lines_add(Lines *ls, Line l)
{
	if (ls->n == ls->cap) {
		int cap = ls->cap ? ls->cap * 2 : 128;
		Line *nv = (Line *)realloc(ls->v, (size_t)cap * sizeof(Line));
		if (!nv)
			return 0;
		ls->v = nv;
		ls->cap = cap;
	}
	ls->v[ls->n++] = l;
	return 1;
}

static int parse(Lines *ls, const char *text)
{
	char cur[64] = "";
	const char *p = text ? text : "";
	while (*p) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		size_t tl = len;
		if (tl && p[tl - 1] == '\r')
			tl--;
		Line l;
		memset(&l, 0, sizeof(l));
		l.text = p;
		l.len = tl;
		l.kind = L_OTHER;
		const char *q = p, *end = p + tl;
		while (q < end && (*q == ' ' || *q == '\t'))
			q++;
		if (q < end && *q == '[') {
			const char *r = memchr(q, ']', (size_t)(end - q));
			if (r) {
				const char *a = q + 1, *b = r;
				while (a < b && (*a == ' ' || *a == '\t'))
					a++;
				while (b > a && (b[-1] == ' ' || b[-1] == '\t'))
					b--;
				lower_copy(cur, sizeof(cur), a, (size_t)(b - a));
				l.kind = L_SECTION;
			}
		} else if (q < end && *q == ';') {
			const char *k = q + 1, *r = k;
			while (r < end && is_key_char(*r))
				r++;
			if (r > k && r < end && *r == '=') {
				l.kind = L_CKEY;
				lower_copy(l.key, sizeof(l.key), k, (size_t)(r - k));
			}
		} else if (q < end && is_key_char(*q)) {
			const char *r = q;
			while (r < end && is_key_char(*r))
				r++;
			const char *a = r;
			while (a < end && (*a == ' ' || *a == '\t'))
				a++;
			if (a < end && *a == '=') {
				l.kind = L_KEY;
				lower_copy(l.key, sizeof(l.key), q, (size_t)(r - q));
			}
		}
		lower_copy(l.sect, sizeof(l.sect), cur, sizeof(cur));
		if (!lines_add(ls, l))
			return 0;
		if (!e)
			break;
		p = e + 1;
	}
	return 1;
}

typedef struct {
	const Line **v;
	int n, cap;
} Out;

static int out_insert(Out *o, int at, const Line *l)
{
	if (o->n == o->cap) {
		int cap = o->cap ? o->cap * 2 : 256;
		const Line **nv = (const Line **)realloc((void *)o->v, (size_t)cap * sizeof(*nv));
		if (!nv)
			return 0;
		o->v = nv;
		o->cap = cap;
	}
	memmove(o->v + at + 1, o->v + at, (size_t)(o->n - at) * sizeof(*o->v));
	o->v[at] = l;
	o->n++;
	return 1;
}

static int blank(const Line *l)
{
	for (size_t i = 0; i < l->len; i++)
		if (l->text[i] != ' ' && l->text[i] != '\t')
			return 0;
	return 1;
}

static int find_user(const Lines *u, const char *sect, const char *key)
{
	for (int i = 0; i < u->n; i++)
		if (u->v[i].kind == L_KEY && !strcmp(u->v[i].sect, sect) && !strcmp(u->v[i].key, key))
			return i;
	return -1;
}

char *ini_merge(const char *tmpl, const char *user, IniMergeStats *stats)
{
	IniMergeStats st = {0, 0, 0};
	Lines t = {0}, u = {0};
	Out o = {0};
	char *used = NULL, *result = NULL;
	if (!parse(&t, tmpl) || !parse(&u, user))
		goto done;
	used = (char *)calloc((size_t)u.n + 1, 1);
	if (!used)
		goto done;

	char cur[64] = "";
	int insert_at = 0; // where the player's unknown keys of the section being written go: after its last line that is not blank
	for (int i = 0; i <= t.n; i++) {
		const Line *l = i < t.n ? &t.v[i] : NULL;
		if (!l || l->kind == L_SECTION) {
			// the section that ends here: the player's keys the template does not know
			if (cur[0])
				for (int k = 0; k < u.n; k++)
					if (u.v[k].kind == L_KEY && !used[k] && !strcmp(u.v[k].sect, cur)) {
						if (!out_insert(&o, insert_at++, &u.v[k]))
							goto done;
						used[k] = 1;
						st.unknown_kept++;
					}
			if (!l)
				break;
			strcpy(cur, l->sect);
		}
		const Line *emit = l;
		if (l->kind == L_KEY || l->kind == L_CKEY) {
			int k = find_user(&u, cur, l->key);
			if (k >= 0 && !used[k]) {
				emit = &u.v[k];
				used[k] = 1;
				st.player_values_kept++;
			} else if (l->kind == L_KEY) {
				st.template_keys_added++;
			}
		}
		if (!out_insert(&o, o.n, emit))
			goto done;
		if (!blank(emit))
			insert_at = o.n;
	}
	// sections the template does not have: kept at the end, with their keys
	for (int i = 0; i < u.n; i++) {
		if (u.v[i].kind != L_SECTION)
			continue;
		int known = 0, has_keys = 0, seen_before = 0;
		for (int k = 0; k < t.n; k++)
			if (t.v[k].kind == L_SECTION && !strcmp(t.v[k].sect, u.v[i].sect))
				known = 1;
		for (int k = 0; k < i; k++)
			if (u.v[k].kind == L_SECTION && !strcmp(u.v[k].sect, u.v[i].sect))
				seen_before = 1;
		for (int k = 0; k < u.n; k++)
			if (u.v[k].kind == L_KEY && !used[k] && !strcmp(u.v[k].sect, u.v[i].sect))
				has_keys = 1;
		if (known || seen_before || !has_keys)
			continue;
		static const Line empty = {"", 0, L_OTHER, "", ""};
		if (!out_insert(&o, o.n, &empty) || !out_insert(&o, o.n, &u.v[i]))
			goto done;
		for (int k = 0; k < u.n; k++)
			if (u.v[k].kind == L_KEY && !used[k] && !strcmp(u.v[k].sect, u.v[i].sect)) {
				if (!out_insert(&o, o.n, &u.v[k]))
					goto done;
				used[k] = 1;
				st.unknown_kept++;
			}
	}
	// the text: no blank lines at the end, one line end after the last line
	{
		int n = o.n;
		while (n > 0 && blank(o.v[n - 1]))
			n--;
		size_t total = 1;
		for (int i = 0; i < n; i++)
			total += o.v[i]->len + 1;
		result = (char *)malloc(total);
		if (!result)
			goto done;
		char *w = result;
		for (int i = 0; i < n; i++) {
			memcpy(w, o.v[i]->text, o.v[i]->len);
			w += o.v[i]->len;
			*w++ = '\n';
		}
		*w = 0;
	}
done:
	free(t.v);
	free(u.v);
	free((void *)o.v);
	free(used);
	if (stats)
		*stats = st;
	return result;
}
