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
//   @@@ subst <function> text replaced at every whole-word occurrence inside one function
//   ...                  of the script (from its header to its closing brace); at least one
//   @@@ to               occurrence must exist. Large reworks (say, level.player -> self in a
//   ...                  function) need no game code in the patch this way.
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
//
// Another mod may replace the function the same way (a 14-byte "jmp [rip+0]; dq target" at its
// start), e.g. a fix mod loaded under another DLL name. Whichever installs second calls the
// first one for the script text and patches what it returns, so both work in either order.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN64
#include "game.h"
#endif
#include "hook.h"
#include "log.h"
#include "scrdebug.h"
#include "scripts.h"

#define ASSET_TYPE_RAWFILE 0x24
#ifdef _WIN64
#define JUMP_SIZE_FOR_SCRIPTS 14
#else
#define JUMP_SIZE_FOR_SCRIPTS 5
#endif

static uint8_t *base;
static char mod_dir[MAX_PATH];
typedef char *(*add_source_buffer_t)(const char *filename, const char *ext, const char *code_pos, int archive);
static add_source_buffer_t chained;	// another mod's replacement installed before ours

#define FN(rva, type) ((type)(base + (rva)))
typedef void *(*db_find_header_t)(int type, const char *name);
typedef int (*db_is_default_t)(int type, const char *name);
typedef int (*db_rawfile_len_t)(void *raw);
typedef void (*db_raw_buffer_t)(void *raw, char *buf, int size);
#ifdef _WIN64
typedef char *(*hunk_temp_high_t)(int64_t size);
#else
// The 32-bit official Steam build (code SHA-256 FC9E4843...52D7, image base 0x400000), docs/32bit-plan.md.
// All of these are cdecl; the wrapper takes the same four arguments as the 64-bit function.
typedef char *(*hunk_temp_high_t)(int size);
#define RVA_SCR_ADD_SOURCE_BUFFER 0x7f8e0
#define RVA_DB_FIND_HEADER        0xf6f10
#define RVA_DB_IS_DEFAULT         0xc32b0
#define RVA_DB_RAWFILE_LEN        0xe3080
#define RVA_DB_RAW_BUFFER         0xe55f0
#define RVA_HUNK_TEMP_HIGH        0x0eed0
#endif

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
	if (strncmp(body, "@@@ ", 4) == 0) { // empty body, the next marker follows right away
		*eol = 0;
		*cursor = body;
		return eol;
	}
	char *end = strstr(body, "\n@@@ ");
	if (!end)
		return NULL;
	*end = 0; // body ends before the newline that precedes the next marker
	*cursor = end + 1;
	return body;
}

static int is_ident_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The top-level function `name` in src: [*start, *end) from the start of its header line to its
// closing brace. A header starts a line; strings and comments are skipped when matching braces.
static int find_function(const char *src, const char *name, const char **start, const char **end)
{
	size_t nl = strlen(name);
	for (const char *p = strstr(src, name); p; p = strstr(p + 1, name)) {
		if ((p != src && p[-1] != '\n') || is_ident_char(p[nl]))
			continue;
		const char *q = p + nl;
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q != '(')
			continue;
		const char *c = strchr(q, '{');
		int depth = 0;
		for (; c && *c; c++) {
			if (c[0] == '/' && c[1] == '/') {
				while (*c && *c != '\n')
					c++;
				if (!*c)
					break;
			} else if (c[0] == '/' && c[1] == '*') {
				c = strstr(c + 2, "*/");
				if (!c)
					return 0;
				c++;
			} else if (*c == '"') {
				for (c++; *c && *c != '"'; c++)
					if (*c == '\\' && c[1])
						c++;
				if (!*c)
					break;
			} else if (*c == '{') {
				depth++;
			} else if (*c == '}' && --depth == 0) {
				*start = p;
				*end = c + 1;
				return 1;
			}
		}
		return 0;
	}
	return 0;
}

// Replaces every whole-word `from` with `to` inside function `name`. Returns the new malloc'd
// text (src freed) and the number of replacements in *count (src unchanged when 0).
static char *subst_in_function(char *src, int *len, const char *name, const char *from, const char *to, int *count)
{
	const char *fs, *fe;
	*count = 0;
	size_t fl = strlen(from), tl = strlen(to);
	if (!fl || !find_function(src, name, &fs, &fe))
		return src;
	int lead = is_ident_char(from[0]), trail = is_ident_char(from[fl - 1]);
	size_t cap = (size_t)*len + 1, n = 0;
	char *out = malloc(cap);
	size_t pre = fs - src;
	memcpy(out, src, pre);
	n = pre;
	for (const char *p = fs; p < fe;) {
		if ((size_t)(fe - p) >= fl && memcmp(p, from, fl) == 0 && !(lead && p > src && is_ident_char(p[-1])) &&
		    !(trail && is_ident_char(p[fl]))) {
			if (n + tl + (size_t)(*len - (p - src)) + 1 > cap) {
				cap = (n + tl + (size_t)(*len - (p - src)) + 1) * 2;
				out = realloc(out, cap);
			}
			memcpy(out + n, to, tl);
			n += tl;
			p += fl;
			(*count)++;
		} else {
			out[n++] = *p++;
		}
	}
	size_t rest = (size_t)*len - (fe - src);
	if (n + rest + 1 > cap)
		out = realloc(out, n + rest + 1);
	memcpy(out + n, fe, rest + 1);
	n += rest;
	if (!*count) {
		free(out);
		return src;
	}
	free(src);
	*len = (int)n;
	return out;
}

// Applies <ext><suffix> (".patch", then ".cp.patch": the checkpoint hooks, written by their own
// generator) to src (malloc'd). Returns the new malloc'd text, or src unchanged; adds to *applied
// and *total.
static char *apply_patches(const char *ext, const char *suffix, char *src, int *len, int *applied, int *total)
{
	int psize;
	char *patch = read_mod_file("patches", ext, suffix, &psize);
	if (!patch)
		return src;
	char *cursor = patch, kind[128];
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
		} else if (strncmp(kind, "subst ", 6) == 0) {
			char kind2[32];
			char *to = next_block(&cursor, kind2, sizeof(kind2));
			if (!to || strcmp(kind2, "to") != 0) {
				log_printf("scripts: %s: malformed patch (subst without to)", ext);
				break;
			}
			(*total)++;
			int count;
			src = subst_in_function(src, len, kind + 6, body, to, &count);
			if (!count)
				log_printf("scripts: %s: patch %d FAILED, '%.40s' not found in %s", ext, *total, body, kind + 6);
			else
				(*applied)++;
		} else if (strcmp(kind, "end") != 0 && strcmp(kind, "with") != 0 && strcmp(kind, "to") != 0) {
			log_printf("scripts: %s: unknown patch block '%s'", ext, kind);
		}
	}
	free(patch);
	return src;
}

static char *to_hunk(const char *text, int len)
{
	char *buf = FN(RVA_HUNK_TEMP_HIGH, hunk_temp_high_t)(len + 1);
	memcpy(buf, text, len);
	buf[len] = 0;
	return buf;
}

static char *add_source_buffer(const char *filename, const char *ext, const char *code_pos, int archive)
{
	int size;
	char *own = read_mod_file("scripts", ext, "", &size);
	if (own) {
		char *buf = to_hunk(own, size);
		log_printf("scripts: %s <- mw2coop (%d bytes, hash %08x)", ext, size, fnv1a(own, size));
		scrdebug_note_source(ext, own, size);
		free(own);
		return buf;
	}
	char *buf;
	int len;
	if (chained) {
		buf = chained(filename, ext, code_pos, archive);
		if (!buf)
			return NULL;
		len = (int)strlen(buf);
	} else {
		void *raw = FN(RVA_DB_FIND_HEADER, db_find_header_t)(ASSET_TYPE_RAWFILE, ext);
		if (FN(RVA_DB_IS_DEFAULT, db_is_default_t)(ASSET_TYPE_RAWFILE, ext))
			return NULL;
		len = FN(RVA_DB_RAWFILE_LEN, db_rawfile_len_t)(raw);
		buf = FN(RVA_HUNK_TEMP_HIGH, hunk_temp_high_t)(len);
		FN(RVA_DB_RAW_BUFFER, db_raw_buffer_t)(raw, buf, len);
	}

	// Patch a working copy; the zone text keeps its '\r', which the compiler ignores.
	int plen;
	char *probe = read_mod_file("patches", ext, ".patch", &plen);
	if (!probe)
		probe = read_mod_file("patches", ext, ".cp.patch", &plen);
	if (!probe) {
		scrdebug_note_source(ext, buf, len);
		return buf;
	}
	free(probe);
	int n = 0;
	char *work = malloc(len + 1);
	for (int i = 0; i < len && buf[i]; i++)
		if (buf[i] != '\r')
			work[n++] = buf[i];
	work[n] = 0;
	int applied = 0, total = 0;
	work = apply_patches(ext, ".patch", work, &n, &applied, &total);
	work = apply_patches(ext, ".cp.patch", work, &n, &applied, &total);
	char *out = to_hunk(work, n);
	scrdebug_note_source(ext, work, n);
	log_printf("scripts: %s <- zone + %d/%d patches (%d bytes, hash %08x)%s", ext, applied, total, n,
		   fnv1a(work, n), applied == total ? "" : "  !!! PATCH FAILURES");
	free(work);
	return out;
}

#ifdef _WIN64
// Expected first bytes of the original, to make sure we patch the right code.
static const uint8_t expected[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x56, 0x48, 0x83, 0xec, 0x20, 0xb9, 0x24, 0x00, 0x00, 0x00};
#else
// The first two instructions and more of the cdecl wrapper at 0x47f8e0 (it forwards to a usercall body).
static const uint8_t expected[] = {0x8b, 0x44, 0x24, 0x10, 0x8b, 0x4c, 0x24, 0x04, 0x53, 0x8b, 0x5c, 0x24, 0x10, 0x56, 0x8b, 0x74, 0x24, 0x10, 0x50, 0x51};
#endif
void scripts_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	DWORD n = GetModuleFileNameA(NULL, mod_dir, MAX_PATH);
	while (n > 0 && mod_dir[n - 1] != '\\')
		n--;
	mod_dir[n] = 0;
	lstrcatA(mod_dir, "mw2coop");

	uint8_t *target = base + RVA_SCR_ADD_SOURCE_BUFFER;
#ifdef _WIN64
	static const uint8_t jmp_rip[6] = {0xff, 0x25, 0, 0, 0, 0};
	if (memcmp(target, jmp_rip, sizeof(jmp_rip)) == 0) {
		memcpy(&chained, target + 6, sizeof(chained));
		log_printf("scripts: Scr_AddSourceBuffer already replaced by another mod (%p): chained", (void *)chained);
	} else
#endif
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("scripts: unexpected bytes at Scr_AddSourceBuffer, not patching");
		return;
	}
	hook_jump(target, JUMP_SIZE_FOR_SCRIPTS, add_source_buffer, 0x90);
	log_printf("scripts: Scr_AddSourceBuffer replaced, mod files from %s", mod_dir);
}
