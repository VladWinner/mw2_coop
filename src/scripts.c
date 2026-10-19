// GSC loading: Scr_AddSourceBuffer is replaced so that mw2coop can
//   - add or fully replace a script:  <game dir>\mw2coop\scripts\<name>   (our own code)
//   - patch a game script:            <game dir>\mw2coop\patches\<name>.patch
// Patches are applied to the zone's own text at load time, so the mod never ships game code.
//
// Patch format (text; '\r' is ignored everywhere):
//   @@@ replace          exact text that must occur exactly once in the script
//   ...
//   @@@ with             what to put there instead
//   ...
//   @@@ end
//   @@@ append           text added at the end of the script (new functions)
//   ...
//   @@@ end
// Lines outside blocks are comments.
//
// Co-op runs in lockstep (both PCs run the level scripts), so every machine must end up with
// the same text. The log prints an FNV-1a hash of each changed script to compare host/client.
//
// Original (rva 0x217950), fastfile-only in this build:
//   raw = DB_FindXAssetHeader(ASSET_TYPE_RAWFILE, ext);
//   if (DB_IsXAssetDefault(ASSET_TYPE_RAWFILE, ext)) return NULL;
//   len = DB_GetRawFileLen(raw); buf = Hunk_AllocateTempMemoryHigh(len);
//   DB_GetRawBuffer(raw, buf, len); return buf;
// The replacement does the same through the game's own functions, so no trampoline is needed.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "scripts.h"

#define ASSET_TYPE_RAWFILE 0x24

static uint8_t *base;
static char mod_dir[MAX_PATH];

#define FN(rva, type) ((type)(base + (rva)))
typedef void *(*db_find_header_t)(int type, const char *name);
typedef int (*db_is_default_t)(int type, const char *name);
typedef int (*db_rawfile_len_t)(void *raw);
typedef char *(*hunk_temp_high_t)(int64_t size);
typedef void (*db_raw_buffer_t)(void *raw, char *buf, int size);

// Reads <mod_dir>\<sub>\<ext><suffix> into a malloc'd, NUL-terminated buffer with '\r' removed.
static char *read_mod_file(const char *sub, const char *ext, const char *suffix, int *size)
{
	char path[MAX_PATH];
	int n = snprintf(path, sizeof(path), "%s\\%s\\%s%s", mod_dir, sub, ext, suffix);
	if (n <= 0 || n >= (int)sizeof(path))
		return NULL;
	for (char *p = path; *p; p++)
		if (*p == '/')
			*p = '\\';
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return NULL;
	LARGE_INTEGER len;
	GetFileSizeEx(f, &len);
	if (len.QuadPart <= 0 || len.QuadPart > 16 * 1024 * 1024) {
		CloseHandle(f);
		return NULL;
	}
	char *buf = malloc(len.QuadPart + 1);
	DWORD got = 0;
	ReadFile(f, buf, (DWORD)len.QuadPart, &got, NULL);
	CloseHandle(f);
	int w = 0;
	for (DWORD i = 0; i < got; i++)
		if (buf[i] != '\r')
			buf[w++] = buf[i];
	buf[w] = 0;
	*size = w;
	return buf;
}

static uint32_t fnv1a(const char *s, int len)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < len; i++)
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	return h;
}

static int count_occurrences(const char *hay, const char *needle, const char **first)
{
	int n = 0;
	size_t nl = strlen(needle);
	*first = NULL;
	for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) {
		if (!n)
			*first = p;
		n++;
		if (nl == 0)
			break;
	}
	return n;
}

// Takes the next "@@@ <word>" block body from *cursor. Returns the marker word in kind.
static char *next_block(char **cursor, char *kind, int kind_size)
{
	char *p = strstr(*cursor, "@@@ ");
	if (!p)
		return NULL;
	char *eol = strchr(p, '\n');
	if (!eol)
		return NULL;
	int k = (int)(eol - (p + 4));
	if (k >= kind_size)
		k = kind_size - 1;
	memcpy(kind, p + 4, k);
	kind[k] = 0;
	char *body = eol + 1;
	char *end = strstr(body, "\n@@@ ");
	if (!end)
		return NULL;
	*end = 0; // body ends before the newline that precedes the next marker
	*cursor = end + 1;
	return body;
}

// Applies <ext>.patch to src (malloc'd). Returns the new malloc'd text, or src unchanged.
static char *apply_patches(const char *ext, char *src, int *len, int *applied, int *total)
{
	int psize;
	char *patch = read_mod_file("patches", ext, ".patch", &psize);
	*applied = *total = 0;
	if (!patch)
		return src;
	char *cursor = patch, kind[32];
	char *body;
	while ((body = next_block(&cursor, kind, sizeof(kind))) != NULL) {
		if (strcmp(kind, "replace") == 0) {
			char kind2[32];
			char *with = next_block(&cursor, kind2, sizeof(kind2));
			if (!with || strcmp(kind2, "with") != 0) {
				log_printf("scripts: %s: malformed patch (replace without with)", ext);
				break;
			}
			(*total)++;
			const char *at;
			int n = count_occurrences(src, body, &at);
			if (n != 1) {
				log_printf("scripts: %s: patch %d FAILED, text found %d times: %.60s", ext, *total, n, body);
				continue;
			}
			size_t bl = strlen(body), wl = strlen(with), off = at - src;
			char *out = malloc(*len - bl + wl + 1);
			memcpy(out, src, off);
			memcpy(out + off, with, wl);
			memcpy(out + off + wl, at + bl, *len - off - bl + 1);
			*len = (int)(*len - bl + wl);
			free(src);
			src = out;
			(*applied)++;
		} else if (strcmp(kind, "append") == 0) {
			(*total)++;
			size_t bl = strlen(body);
			char *out = malloc(*len + bl + 3);
			memcpy(out, src, *len);
			out[*len] = '\n';
			memcpy(out + *len + 1, body, bl);
			out[*len + 1 + bl] = '\n';
			out[*len + 2 + bl] = 0;
			*len += (int)bl + 2;
			free(src);
			src = out;
			(*applied)++;
		} else if (strcmp(kind, "end") != 0 && strcmp(kind, "with") != 0) {
			log_printf("scripts: %s: unknown patch block '%s'", ext, kind);
		}
	}
	free(patch);
	return src;
}

static char *to_hunk(const char *text, int len)
{
	char *buf = FN(RVA_HUNK_TEMP_HIGH, hunk_temp_high_t)((int64_t)len + 1);
	memcpy(buf, text, len);
	buf[len] = 0;
	return buf;
}

static char *add_source_buffer(const char *filename, const char *ext, const char *code_pos, int archive)
{
	(void)filename;
	(void)code_pos;
	(void)archive;
	int size;
	char *own = read_mod_file("scripts", ext, "", &size);
	if (own) {
		char *buf = to_hunk(own, size);
		log_printf("scripts: %s <- mw2coop (%d bytes, hash %08x)", ext, size, fnv1a(own, size));
		free(own);
		return buf;
	}
	void *raw = FN(RVA_DB_FIND_HEADER, db_find_header_t)(ASSET_TYPE_RAWFILE, ext);
	if (FN(RVA_DB_IS_DEFAULT, db_is_default_t)(ASSET_TYPE_RAWFILE, ext))
		return NULL;
	int len = FN(RVA_DB_RAWFILE_LEN, db_rawfile_len_t)(raw);
	char *buf = FN(RVA_HUNK_TEMP_HIGH, hunk_temp_high_t)(len);
	FN(RVA_DB_RAW_BUFFER, db_raw_buffer_t)(raw, buf, len);

	// Patch a working copy; the zone text keeps its '\r', which the compiler ignores.
	int plen;
	char *probe = read_mod_file("patches", ext, ".patch", &plen);
	if (!probe)
		return buf;
	free(probe);
	int n = 0;
	char *work = malloc(len + 1);
	for (int i = 0; i < len && buf[i]; i++)
		if (buf[i] != '\r')
			work[n++] = buf[i];
	work[n] = 0;
	int applied, total;
	work = apply_patches(ext, work, &n, &applied, &total);
	char *out = to_hunk(work, n);
	log_printf("scripts: %s <- zone + %d/%d patches (%d bytes, hash %08x)%s", ext, applied, total, n,
		   fnv1a(work, n), applied == total ? "" : "  !!! PATCH FAILURES");
	free(work);
	return out;
}

// Expected first bytes of the original, to make sure we patch the right code.
static const uint8_t expected[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x56, 0x48, 0x83, 0xec, 0x20, 0xb9, 0x24, 0x00, 0x00, 0x00};

void scripts_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	DWORD n = GetModuleFileNameA(NULL, mod_dir, MAX_PATH);
	while (n > 0 && mod_dir[n - 1] != '\\')
		n--;
	mod_dir[n] = 0;
	lstrcatA(mod_dir, "mw2coop");

	uint8_t *target = base + RVA_SCR_ADD_SOURCE_BUFFER;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("scripts: unexpected bytes at Scr_AddSourceBuffer, not patching");
		return;
	}
	uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t to = (uint64_t)(uintptr_t)add_source_buffer;
	memcpy(jmp + 6, &to, 8);
	DWORD old;
	VirtualProtect(target, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, jmp, sizeof(jmp));
	VirtualProtect(target, sizeof(jmp), old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, sizeof(jmp));
	log_printf("scripts: Scr_AddSourceBuffer replaced, mod files from %s", mod_dir);
}
