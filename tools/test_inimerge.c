// Checks src/inimerge.c (the settings.ini merge): the player's values survive, new keys of the template appear, his unknown keys and sections stay, the result is
// stable. Run by tools/test_inimerge.sh. It also merges the real template (content/release/settings.ini, given as the first argument) with itself.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inimerge.h"

static int failed;

static void expect(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failed = 1;
	}
}

static const char *TMPL =
	"; settings\n"
	"\n"
	"[network]\n"
	"; the port\n"
	"port=28960\n"
	"steam_relay=0\n"
	"\n"
	"[display]\n"
	"; fonts\n"
	"fonts=1\n"
	";fsr3_sharpness=60\n"
	"\n"
	"[native]\n"
	";movedbuilds=0\n";

static char *merge(const char *user, IniMergeStats *st)
{
	char *r = ini_merge(TMPL, user, st);
	if (!r) {
		printf("FAIL: ini_merge returned NULL\n");
		exit(1);
	}
	return r;
}

int main(int argc, char **argv)
{
	IniMergeStats st;
	char *r = merge(NULL, &st);
	expect(!strcmp(r, TMPL), "no player file gives the template");
	expect(st.template_keys_added == 3 && st.player_values_kept == 0 && st.unknown_kept == 0, "stats of a new file");
	free(r);

	// a player's changes: a value, an activated commented default, keys with other case and spacing, CRLF line ends, his own comment
	const char *user =
		"[Network]\r\n"
		"; my note\r\n"
		"PORT = 28961\r\n"
		"\r\n"
		"[display]\r\n"
		"fonts=0\r\n"
		"fsr3_sharpness=40\r\n";
	r = merge(user, &st);
	expect(strstr(r, "PORT = 28961\n") != NULL, "the player's port line is kept as he wrote it");
	expect(strstr(r, "port=28960") == NULL, "the default port is not written next to his");
	expect(strstr(r, "\nfonts=0\n") != NULL, "his fonts value");
	expect(strstr(r, "\nfsr3_sharpness=40\n") != NULL && strstr(r, ";fsr3_sharpness=60") == NULL, "an activated commented default replaces the comment");
	expect(strstr(r, "steam_relay=0\n") != NULL, "a key he lacks comes with the template's value");
	expect(strstr(r, "; the port\n") != NULL, "the template's comments");
	expect(strstr(r, "my note") == NULL, "his own comment is not kept");
	expect(strstr(r, "\r") == NULL, "line ends are \\n");
	expect(strstr(r, ";movedbuilds=0\n") != NULL, "the [native] template comment is there");
	expect(st.player_values_kept == 3 && st.template_keys_added == 1 && st.unknown_kept == 0, "stats of a changed file");
	// stable: merging the result again changes nothing
	char *r2 = ini_merge(TMPL, r, NULL);
	expect(r2 && !strcmp(r, r2), "merging a merged file again gives the same text");
	free(r2);
	free(r);

	// unknown keys and sections stay: the console's own keys, a test's [native] keys, a section of a newer version
	const char *extra =
		"[network]\n"
		"port=28960\n"
		"my_future_key=7\n"
		"[native]\n"
		"players=2\n"
		"coopdvar=1\n"
		"[console]\n"
		"layout=legacy\n"
		"[empty_section]\n";
	r = merge(extra, &st);
	const char *a = strstr(r, "my_future_key=7\n"), *b = strstr(r, "\n[display]");
	expect(a && b && a < b, "an unknown key stays in its section (before the next one)");
	a = strstr(r, "\n[native]\n");
	expect(a && strstr(a, "players=2\n") && strstr(a, "coopdvar=1\n") && strstr(a, ";movedbuilds=0\n"), "the [native] keys of a test stay in [native]");
	a = strstr(r, "\n[console]\nlayout=legacy\n");
	expect(a != NULL, "a section the template does not have is kept at the end");
	expect(strstr(r, "[empty_section]") == NULL, "a section with no keys is not kept");
	expect(st.unknown_kept == 4, "unknown keys counted");
	free(r);

	// duplicate keys do not multiply: the first one decides, the rest is kept once as unknown
	r = merge("[network]\nport=1\nport=2\n", &st);
	expect(strstr(r, "port=1\n") && strstr(r, "port=2\n"), "a duplicate is kept");
	r2 = ini_merge(TMPL, r, NULL);
	expect(r2 && !strcmp(r, r2), "duplicates are stable");
	free(r2);
	free(r);

	// the real template merged with itself is itself (normalised line ends)
	if (argc > 1) {
		FILE *f = fopen(argv[1], "rb");
		expect(f != NULL, "the template file opens");
		if (f) {
			static char buf[1 << 20], norm[1 << 20];
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			fclose(f);
			buf[n] = 0;
			size_t w = 0;
			for (size_t i = 0; i < n; i++)
				if (buf[i] != '\r')
					norm[w++] = buf[i];
			norm[w] = 0;
			char *m = ini_merge(norm, buf, &st);
			expect(m && !strcmp(m, norm), "the real template merged with its own file (CRLF) is itself");
			expect(st.template_keys_added == 0 && st.unknown_kept == 0, "nothing is added or kept for the real template");
			free(m);
			m = ini_merge(norm, NULL, &st);
			expect(m && !strcmp(m, norm), "the real template without a player file is itself");
			printf("real template: %d active keys\n", st.template_keys_added);
			free(m);
		}
	}
	if (!failed)
		printf("inimerge: all checks passed\n");
	return failed;
}
