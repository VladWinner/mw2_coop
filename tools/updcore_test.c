// Tests of the updater's core (src/updcore.c), outside the game. Built and run by tools/updcore_test.sh:
//   updcore_test selftest              versions, nightly dates, safe paths, CRC-32, SHA-256, a JSON answer, inflate of known streams
//   updcore_test json <file>           a release answer of GitHub's API: tag, date, assets
//   updcore_test zip <file>            every file of an archive unpacked: "<sha256> <size> <path>" a line (the script compares them with Python's zipfile)
//   updcore_test fuzz <file> <seed> <n> n damaged copies of an archive (bytes changed at random): none may crash, a damaged file must not come out as good
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/updcore.h"

static int failures;
#define CHECK(c)                                                                         \
	do {                                                                             \
		if (!(c)) {                                                              \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);              \
			failures++;                                                      \
		}                                                                        \
	} while (0)

static unsigned char *load(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc(n > 0 ? (size_t)n : 1);
	*len = fread(b, 1, (size_t)n, f);
	fclose(f);
	return b;
}

static void selftest(void)
{
	int v[3], w[3];
	CHECK(upd_parse_version("v1.5.2", v) && v[0] == 1 && v[1] == 5 && v[2] == 2);
	CHECK(upd_parse_version("1.10.0", w) && upd_compare_versions(w, v) > 0);
	CHECK(upd_parse_version("v2.0.0-beta", w) && w[0] == 2);
	CHECK(!upd_parse_version("dev", w));
	CHECK(!upd_parse_version("v1.5", w));
	CHECK(!upd_parse_version("v1.5.2x", w));
	CHECK(!upd_parse_version("nightly-20261121-abc", w));
	CHECK(upd_parse_version("v1.5.2", w) && upd_compare_versions(w, v) == 0);
	CHECK(upd_parse_version("v1.5.1", w) && upd_compare_versions(w, v) < 0);
	CHECK(upd_nightly_date("nightly-20261121-abcdef0") == 20261121);
	CHECK(upd_nightly_date("v1.5.2") == 0);
	CHECK(upd_iso_date("2026-11-21T01:25:00Z") == 20261121);
	CHECK(upd_iso_date("bad") == 0);

	char out[260];
	CHECK(upd_safe_path("winmm.dll", out, sizeof(out)) && !strcmp(out, "winmm.dll"));
	CHECK(upd_safe_path("mw2coop/scripts/maps/_mw2coop.gsc", out, sizeof(out)) && !strcmp(out, "mw2coop\\scripts\\maps\\_mw2coop.gsc"));
	CHECK(upd_safe_path("INSTALL-DLSS.txt", out, sizeof(out)));
	CHECK(upd_safe_path("mw2coop/dlss/mw2coop_dlss.dll", out, sizeof(out)));
	CHECK(!upd_safe_path("iw4sp.exe", out, sizeof(out)));
	CHECK(!upd_safe_path("../winmm.dll", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/../../x.dll", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/./x", out, sizeof(out)));
	CHECK(!upd_safe_path("/winmm.dll", out, sizeof(out)));
	CHECK(!upd_safe_path("C:/Windows/x.dll", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop//x", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/update/staging/winmm.dll", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/settings.ini", out, sizeof(out)));
	CHECK(!upd_safe_path("MW2COOP/Settings.ini", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/x.", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/x ", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/a\x01" "b", out, sizeof(out)));
	CHECK(!upd_safe_path("mw2coop/", out, sizeof(out)));
	CHECK(!upd_safe_path("players/save/x.svg", out, sizeof(out)));
	CHECK(!upd_safe_path("", out, sizeof(out)));

	CHECK(upd_crc32(0, "123456789", 9) == 0xcbf43926u);
	char hex[65];
	CHECK(upd_sha256("abc", 3, hex) && !strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

	// inflate: a stored block, a fixed block ("hello hello hello", made by zlib at level 9 without its header), a damaged stream
	static const unsigned char stored[] = {0x01, 0x05, 0x00, 0xfa, 0xff, 'h', 'e', 'l', 'l', 'o'};
	unsigned char buf[64];
	CHECK(upd_inflate(stored, sizeof(stored), buf, sizeof(buf)) == 5 && !memcmp(buf, "hello", 5));
	CHECK(upd_inflate(stored, sizeof(stored), buf, 4) == -1); // more than the room
	static const unsigned char fixed[] = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x90, 0x00};
	long n = upd_inflate(fixed, sizeof(fixed), buf, sizeof(buf));
	CHECK(n == 17 && !memcmp(buf, "hello hello hello", 17));
	static const unsigned char junk[] = {0xff, 0xff, 0xff, 0xff};
	CHECK(upd_inflate(junk, sizeof(junk), buf, sizeof(buf)) == -1);
	CHECK(upd_inflate(fixed, 3, buf, sizeof(buf)) == -1); // cut short

	static const char json[] =
		"{\"url\":\"x\",\"html_url\":\"https://github.com/o/r/releases/tag/v1.5.3\",\"id\":1,\"author\":{\"login\":\"a\",\"name\":\"n\"},"
		"\"tag_name\":\"v1.5.3\",\"draft\":false,\"prerelease\":false,\"published_at\":\"2026-11-25T10:00:00Z\",\"body\":\"line \\\"one\\\"\\nnext \\u00e9 \\ud83d\\ude00\","
		"\"assets\":[{\"name\":\"mw2coop-v1.5.3.zip\",\"uploader\":{\"login\":\"a\",\"name\":\"other\"},\"size\":2319576,\"digest\":\"sha256:A07070DA1957DF12FA2E299E62AA8F914D31EC190E21D812C5FFF61D6D11C65B\","
		"\"browser_download_url\":\"https://github.com/o/r/releases/download/v1.5.3/mw2coop-v1.5.3.zip\",\"label\":null},"
		"{\"name\":\"mw2coop-v1.5.3.zip.sha256\",\"size\":85,\"digest\":null,\"browser_download_url\":\"https://x/y.sha256\"}],\"reactions\":{}}";
	upd_release *r = calloc(1, sizeof(*r));
	CHECK(upd_parse_release(json, strlen(json), r));
	CHECK(!strcmp(r->tag, "v1.5.3") && !r->draft && !r->prerelease && r->asset_count == 2);
	CHECK(!strcmp(r->published, "2026-11-25T10:00:00Z"));
	const upd_asset *a = upd_find_asset(r, "mw2coop-v1.5.3.zip");
	CHECK(a && a->size == 2319576 && !strcmp(a->sha256, "a07070da1957df12fa2e299e62aa8f914d31ec190e21d812c5fff61d6d11c65b"));
	CHECK(a && !strcmp(a->url, "https://github.com/o/r/releases/download/v1.5.3/mw2coop-v1.5.3.zip"));
	a = upd_find_asset(r, "mw2coop-v1.5.3.zip.sha256");
	CHECK(a && a->sha256[0] == 0 && a->size == 85);
	CHECK(!upd_find_asset(r, "nothing"));
	static const char *const bad[] = {"", "[]", "{", "{\"tag_name\":}", "{\"tag_name\":\"v1\"", "{\"assets\":[{\"name\":\"x\"}]}", "{\"tag_name\":\"v1.0.0\",}", "nul"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(!upd_parse_release(bad[i], strlen(bad[i]), r));
	// deep nesting is refused, not followed into a stack overflow
	char *deep = malloc(200000);
	strcpy(deep, "{\"x\":");
	for (int i = 0; i < 90000; i++)
		strcat(deep + 5 + i, "[");
	CHECK(!upd_parse_release(deep, strlen(deep), r));
	free(deep);
	free(r);
}

static int cmd_json(const char *path)
{
	size_t len = 0;
	unsigned char *b = load(path, &len);
	upd_release *r = calloc(1, sizeof(*r));
	if (!b || !upd_parse_release((const char *)b, len, r)) {
		printf("not a release\n");
		return 1;
	}
	printf("tag %s published %s draft %d prerelease %d assets %d\n", r->tag, r->published, r->draft, r->prerelease, r->asset_count);
	for (int i = 0; i < r->asset_count; i++)
		printf("asset %s %lld %s %s\n", r->assets[i].name, r->assets[i].size, r->assets[i].sha256[0] ? r->assets[i].sha256 : "-", r->assets[i].url);
	return 0;
}

static int cmd_zip(const char *path)
{
	size_t len = 0;
	unsigned char *b = load(path, &len);
	static upd_zip_entry e[1024];
	int n = b ? upd_zip_list(b, len, e, 1024) : -1;
	if (n < 0) {
		printf("not a zip\n");
		return 1;
	}
	int bad = 0;
	for (int i = 0; i < n; i++) {
		unsigned char *d = upd_zip_extract(b, len, &e[i]);
		char hex[65], safe[260];
		if (!d) {
			printf("FAIL %s\n", e[i].path);
			bad++;
			continue;
		}
		upd_sha256(d, e[i].usize, hex);
		printf("%s %u %s%s\n", hex, e[i].usize, e[i].path, upd_safe_path(e[i].path, safe, sizeof(safe)) ? "" : " (refused path)");
		HeapFree(GetProcessHeap(), 0, d);
	}
	return bad != 0;
}

static int cmd_fuzz(const char *path, unsigned seed, int count)
{
	size_t len = 0;
	unsigned char *orig = load(path, &len);
	if (!orig)
		return 1;
	static upd_zip_entry good[1024], e[1024];
	int gn = upd_zip_list(orig, len, good, 1024);
	unsigned char *b = malloc(len);
	srand(seed);
	int refused = 0, listed = 0, files_ok = 0, files_bad = 0, wrong = 0;
	for (int k = 0; k < count; k++) {
		memcpy(b, orig, len);
		int changes = 1 + rand() % 8;
		for (int c = 0; c < changes; c++) {
			size_t at = ((size_t)rand() << 15 ^ (size_t)rand()) % len;
			b[at] = (unsigned char)rand();
		}
		int n = upd_zip_list(b, len, e, 1024);
		if (n < 0) {
			refused++;
			continue;
		}
		listed++;
		for (int i = 0; i < n; i++) {
			unsigned char *d = upd_zip_extract(b, len, &e[i]);
			if (!d) {
				files_bad++;
				continue;
			}
			files_ok++;
			// a file that comes out must be the original file (its CRC matched): same name in the original list, same bytes
			for (int g = 0; g < gn; g++)
				if (!strcmp(good[g].path, e[i].path) && good[g].usize == e[i].usize) {
					unsigned char *o = upd_zip_extract(orig, len, &good[g]);
					if (o && memcmp(o, d, e[i].usize) != 0)
						wrong++;
					if (o)
						HeapFree(GetProcessHeap(), 0, o);
				}
			HeapFree(GetProcessHeap(), 0, d);
		}
	}
	printf("fuzz %d: archive refused %d, listed %d, files unpacked %d, files refused %d, wrong bytes %d\n", count, refused, listed, files_ok, files_bad, wrong);
	return wrong != 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "selftest")) {
		selftest();
		printf("selftest: %d failure(s)\n", failures);
		return failures != 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "json"))
		return cmd_json(argv[2]);
	if (argc >= 3 && !strcmp(argv[1], "zip"))
		return cmd_zip(argv[2]);
	if (argc >= 5 && !strcmp(argv[1], "fuzz"))
		return cmd_fuzz(argv[2], (unsigned)atoi(argv[3]), atoi(argv[4]));
	printf("usage: updcore_test selftest | json <file> | zip <file> | fuzz <file> <seed> <n>\n");
	return 2;
}
