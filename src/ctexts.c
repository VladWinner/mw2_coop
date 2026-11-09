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

int ctexts_set(const char *name, const char *english_utf8, const char *russian_utf8)
{
	Text *t = find(name);
	if (!t)
		return 0;
	if (english_utf8)
		t->en = english_utf8;
	if (russian_utf8)
		t->ru = russian_utf8;
	return 1;
}
