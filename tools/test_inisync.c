// Checks src/inisync.c (making and updating settings.ini on disk): a missing file is made from the template, an existing one keeps the player's values and
// is saved as .bak when it changes, a second start changes nothing, a UTF-16 file is left alone. Run by tools/test_inimerge.sh in a scratch folder.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inisync.h"

void log_printf(const char *f, ...)
{
	va_list a;
	va_start(a, f);
	printf("  log: ");
	vprintf(f, a);
	printf("\n");
	va_end(a);
}

static int failed;
static void expect(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failed = 1;
	}
}

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	char *b = (char *)malloc(1 << 16);
	size_t k = fread(b, 1, (1 << 16) - 1, f);
	fclose(f);
	b[k] = 0;
	if (n)
		*n = k;
	return b;
}

static void put(const char *path, const void *data, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(data, 1, n, f);
	fclose(f);
}

static const char *TMPL = "; settings\n\n[network]\n; the port\nport=28960\n\n[display]\nfonts=1\n;fsr3_sharpness=60\n";

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "inisync-test";
	char ini[MAX_PATH], bak[MAX_PATH], sub[MAX_PATH];
	snprintf(sub, sizeof(sub), "%s\\mw2coop", dir);
	snprintf(ini, sizeof(ini), "%s\\settings.ini", sub);
	snprintf(bak, sizeof(bak), "%s\\settings.ini.bak", sub);
	CreateDirectoryA(dir, NULL);
	DeleteFileA(ini);
	DeleteFileA(bak);

	// 1. no file (not even the folder): made from the template, with CRLF line ends
	RemoveDirectoryA(sub);
	ini_sync_file(ini, TMPL);
	size_t n = 0;
	char *t = slurp(ini, &n);
	expect(t != NULL, "a missing file is made (and its folder)");
	expect(t && strstr(t, "port=28960\r\n") && strstr(t, "; the port\r\n"), "the new file has the template's lines with CRLF");
	expect(slurp(bak, NULL) == NULL, "no .bak for a new file");
	free(t);

	// 2. a changed file: his value stays, a new key comes, the old file is the .bak
	const char *mine = "[network]\r\nport=1234\r\n[display]\r\nfsr3_sharpness=30\r\n";
	put(ini, mine, strlen(mine));
	ini_sync_file(ini, TMPL);
	t = slurp(ini, NULL);
	expect(t && strstr(t, "port=1234\r\n") && !strstr(t, "port=28960"), "his port is kept");
	expect(t && strstr(t, "fsr3_sharpness=30\r\n") && strstr(t, "fonts=1\r\n"), "his activated default is kept and the missing key is added");
	char *b = slurp(bak, NULL);
	expect(b && !strcmp(b, mine), "the old file is settings.ini.bak");
	free(b);
	char *first = t;

	// 3. a second start changes nothing: the file is the same and the .bak is still the first old file (it is not made again)
	put(bak, "marker", 6);
	ini_sync_file(ini, TMPL);
	t = slurp(ini, NULL);
	expect(t && !strcmp(t, first), "a second start leaves the file as it is");
	b = slurp(bak, NULL);
	expect(b && !strcmp(b, "marker"), "a second start does not touch the .bak");
	free(b);
	free(t);
	free(first);

	// 4. a UTF-16 file is left alone
	const unsigned char u16[] = {0xff, 0xfe, '[', 0, 'n', 0, ']', 0};
	put(ini, u16, sizeof(u16));
	ini_sync_file(ini, TMPL);
	t = slurp(ini, &n);
	expect(t && n == sizeof(u16) && !memcmp(t, u16, n), "a UTF-16 file is left alone");
	free(t);

	// 5. a UTF-8 file with a byte order mark is read and rewritten without it
	const char *bom = "\xef\xbb\xbf[network]\nport=777\n";
	put(ini, bom, strlen(bom));
	ini_sync_file(ini, TMPL);
	t = slurp(ini, NULL);
	expect(t && (unsigned char)t[0] == ';' && strstr(t, "port=777"), "a byte order mark is dropped, his value kept");
	free(t);

	if (!failed)
		printf("inisync: all checks passed\n");
	return failed;
}
