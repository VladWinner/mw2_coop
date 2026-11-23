#pragma once
// The parts of the mod's updater (update.c) that touch neither the game nor the network: the version, the release's JSON from GitHub's API, the zip archive (its
// directory, raw deflate, CRC-32), SHA-256 and the paths an archive may write. Unit-tested outside the game by tools/updcore_test.c.
#include <stddef.h>
#include <stdint.h>

// "v1.5.2", "1.5.2", "v1.5.2-beta" -> {1, 5, 2}; 1 when it has the three numbers
int upd_parse_version(const char *s, int v[3]);
// <0, 0, >0 as a is older, the same, newer than b
int upd_compare_versions(const int a[3], const int b[3]);
// "nightly-20261121-abcdef0" -> 20261121; 0 when it is not a nightly's name
long upd_nightly_date(const char *s);
// "2026-11-21T01:25:00Z" -> 20261121; 0 when it is not a date
long upd_iso_date(const char *s);

#define UPD_MAX_ASSETS 32
typedef struct {
	char name[96];
	long long size;
	char sha256[65]; // from the asset's "digest" ("sha256:<hex>"), lower case; empty when GitHub gave none
	char url[512];	 // browser_download_url
} upd_asset;

typedef struct {
	char tag[48];
	char published[24];
	char html_url[256];
	int draft, prerelease;
	int asset_count;
	upd_asset assets[UPD_MAX_ASSETS];
} upd_release;

// GitHub's "get a release" answer (api.github.com/repos/<owner>/<repo>/releases/latest); 1 when it is a release with a tag
int upd_parse_release(const char *json, size_t len, upd_release *out);
const upd_asset *upd_find_asset(const upd_release *r, const char *name);

uint32_t upd_crc32(uint32_t crc, const void *data, size_t len);
// raw deflate (RFC 1951) into out; the number of bytes made, or -1 (a damaged stream, or more than outlen)
long upd_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);

typedef struct {
	char path[260]; // as stored in the archive (forward slashes)
	uint32_t crc, csize, usize;
	uint16_t method, flags;
	uint32_t local_offset;
} upd_zip_entry;

// The archive's files (directories are left out): their count, or -1 when it is not a zip this reader takes (no ZIP64, no encryption)
int upd_zip_list(const uint8_t *zip, size_t len, upd_zip_entry *out, int max);
// One file's bytes in a new HeapAlloc block (its CRC-32 checked), or NULL
uint8_t *upd_zip_extract(const uint8_t *zip, size_t len, const upd_zip_entry *e);

// A path inside an archive -> the path in the game folder it may be written to (backslashes), or 0: absolute paths, drive letters, "..", empty parts and control
// characters are refused, and only the root files of the mod's archives (winmm.dll and the text files) and what is under mw2coop\ (not mw2coop\update\) are taken
int upd_safe_path(const char *zip_path, char *out, size_t size);

// SHA-256 as 64 lower-case hex digits; 1 when it worked
int upd_sha256(const void *data, size_t len, char hex[65]);
