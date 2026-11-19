// Console texts by name (see ctexts.h). The built-in table is src/console_texts_gen.h, generated from console.str.
#include <windows.h>
#include <string.h>

#include "ctexts.h"

typedef struct {
	const char *name, *en, *ru;
} Text;

static Text texts[] = {
#include "console_texts_gen.h"
};
#define TEXT_COUNT (int)(sizeof(texts) / sizeof(texts[0]))

static Text *find(const char *name)
{
	for (int i = 0; i < TEXT_COUNT; i++)
		if (strcmp(texts[i].name, name) == 0)
			return &texts[i];
	return NULL;
}

const char *ctexts_get(const char *name, int russian)
{
	Text *t = name ? find(name) : NULL;
	if (!t)
		return name ? name : "";
	return russian ? t->ru : t->en;
}

// the conversions of a printf format, one letter each ('?' for one this code does not accept), at most size - 1
static void conversions(const char *f, char *out, int size)
{
	int n = 0;
	for (; f && *f && n < size - 1; f++) {
		if (*f != '%')
			continue;
		if (f[1] == '%') {
			f++;
			continue;
		}
		f++;
		while (*f && strchr("-+ #0123456789.", *f))
			f++;
		int sized = 0; // (a length modifier changes what is read: %ls takes a wide string, %lld eight bytes; not accepted)
		while (*f == 'l' || *f == 'h' || *f == 'L' || *f == 'z' || *f == 'j' || *f == 't' || *f == 'I')
			f++, sized = 1;
		out[n++] = (*f && !sized && strchr("sdiuxXcfg", *f)) ? (*f == 'i' ? 'd' : *f) : '?';
		if (!*f)
			break;
	}
	out[n] = 0;
}

int ctexts_fmt_ok(const char *fmt, const char *want)
{
	char have[24];
	conversions(fmt, have, sizeof(have));
	return !strchr(have, '?') && strncmp(have, want, strlen(have)) == 0 && strlen(have) <= strlen(want);
}

int ctexts_set(const char *name, const char *english_utf8, const char *russian_utf8)
{
	Text *t = find(name);
	if (!t)
		return 0;
	// A text the code formats with printf keeps the conversions of the built-in one: a translation with another %s / %d crashed the drawing (audit
	// 2026-11-18, console C12); such a replacement is left out
	char want[24];
	conversions(t->en, want, sizeof(want));
	if (english_utf8 && want[0] && !strchr(want, '?') && !ctexts_fmt_ok(english_utf8, want))
		english_utf8 = NULL;
	if (russian_utf8 && want[0] && !strchr(want, '?') && !ctexts_fmt_ok(russian_utf8, want))
		russian_utf8 = NULL;
	if (english_utf8)
		t->en = english_utf8;
	if (russian_utf8)
		t->ru = russian_utf8;
	return 1;
}
