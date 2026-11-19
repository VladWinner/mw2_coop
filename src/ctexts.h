#pragma once
// The texts of the mod's developer console (and other code that wants UTF-8 texts in two languages): a name, an English and a
// Russian text, UTF-8. The built-in values are made from content/release/localizedstrings/console.str (tools/gen_console_texts.py);
// a *.str file in mw2coop\localizedstrings replaces them (the loader in localize.c calls ctexts_set).

// The text of <name> in English or Russian; the name itself if there is no such text.
const char *ctexts_get(const char *name, int russian);
// Replace the texts of <name> (either may be NULL: that language keeps its text). Returns 0 if no such text exists.
int ctexts_set(const char *name, const char *english_utf8, const char *russian_utf8);
// A text used as a printf format that a translation may replace: 1 when its conversions are those of `want` (one letter each, e.g. "ssd"), or the
// first of them (a translation may leave a value out). Anything else (another type, more of them, '*' widths, %n) would read the wrong arguments.
int ctexts_fmt_ok(const char *fmt, const char *want);
