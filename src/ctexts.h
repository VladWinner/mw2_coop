#pragma once
// The texts of the mod's developer console (and other code that wants UTF-8 texts in two languages): a name, an English and a
// Russian text, UTF-8. The built-in values are made from content/release/localizedstrings/console.str (tools/gen_console_texts.py);
// a *.str file in mw2coop\localizedstrings replaces them (the loader in localize.c calls ctexts_set).

// The text of <name> in English or Russian; the name itself if there is no such text.
const char *ctexts_get(const char *name, int russian);
// Replace the texts of <name> (either may be NULL: that language keeps its text). Returns 0 if no such text exists.
int ctexts_set(const char *name, const char *english_utf8, const char *russian_utf8);
