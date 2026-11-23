// The mod's updater (owner, 2026-11-21: "add the ability to update the mod from the game itself").
//
// At start (once the game's window is up) a thread asks GitHub's API for the repository's newest release (api.github.com/repos/stxlvn/mw2_enhanced/releases/latest,
// one HTTPS request with no data about the player; settings.ini [update] check=0 turns it off). When its version is newer than this build's, the main menu shows a
// popup: "Update" downloads the archive of this build (the 64-bit game's, or the 32-bit game's and iw4x-sp's), and the DLSS archive too when its DLL is installed;
// checks each against the SHA-256 GitHub gives for it (the asset's digest, and the <archive>.sha256 file of the release when there is one: both must agree);
// unpacks it into mw2coop\update\staging; then, back at the main menu, moves each file into place. A file in use (winmm.dll itself, SDL3.dll) is renamed
// out of the way first (mw2coop\update\old, emptied at the next start), so nothing waits for the game to quit; a step that fails puts every file back.
// The archive's manifest (mw2coop\release.txt) lists its files: a file the previous release had and this one does not is moved out too, when it is unchanged.
// Then a popup restarts the game (a rundll32 of the previous winmm.dll waits for this process to end and starts the exe with the same command line) or quits it.
// Never: a file outside the game folder, the player's settings.ini, anything under mw2coop\update; a draft, a pre-release or an archive without a checksum.
//
// QA: MW2COOP_UPDATE_URL=<url of a release JSON> (http allowed then: a local test server), MW2COOP_UPDATE_AS=<version> (pretend to be that version), console
// command mw2coop_update (check now and show the answer even when there is nothing new).
#include <windows.h>
#include <winhttp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update.h"
#include "updcore.h"
#include "ctexts.h"
#include "game.h"
#include "localize.h"
#include "log.h"
#include "menus.h"
#include "menus_ui.h"
#include "settings.h"

#ifndef MW2COOP_VERSION
#define MW2COOP_VERSION "dev"
#endif
#define API_URL "https://api.github.com/repos/stxlvn/mw2_enhanced/releases/latest"
#define MAX_ARCHIVE (64u * 1024 * 1024)
#define MAX_FILES 512

enum { ST_IDLE, ST_CHECKING, ST_AVAILABLE, ST_NEWEST, ST_DEV, ST_CHECK_FAILED, ST_WORKING, ST_DONE, ST_FAILED };
enum { E_NONE, E_NET, E_HASH, E_ARCHIVE, E_WRITE, E_ASSET };
static const char *const reason_keys[] = {"", "MW2COOP_UPDATE_E_NET", "MW2COOP_UPDATE_E_HASH", "MW2COOP_UPDATE_E_ARCHIVE", "MW2COOP_UPDATE_E_WRITE", "MW2COOP_UPDATE_E_ASSET"};

static volatile LONG state, manual, checking, fail_reason, progress, phase, at_main_menu, menu_tick_at;
static upd_release rel;			  // the newest release, written by the check before state becomes ST_AVAILABLE
static char game_dir[MAX_PATH];		  // with the trailing backslash
static char old_dll[MAX_PATH];		  // where this process's winmm.dll was moved by an update ("" while it is in place)
static char archive_name[2][128];	  // the archives being installed (for the progress text)
static volatile LONG archive_at;

static int qa_url(void)
{
	return GetEnvironmentVariableA("MW2COOP_UPDATE_URL", NULL, 0) > 0;
}

const char *update_version(void)
{
	static char as[48];
	if (!as[0] && GetEnvironmentVariableA("MW2COOP_UPDATE_AS", as, sizeof(as)) == 0)
		lstrcpynA(as, MW2COOP_VERSION, sizeof(as));
	return as;
}

static void dir_init(void)
{
	if (game_dir[0])
		return;
	DWORD n = GetModuleFileNameA(NULL, game_dir, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		game_dir[0] = 0;
	char *slash = strrchr(game_dir, '\\');
	if (slash)
		slash[1] = 0;
}

static void game_path(char *out, size_t size, const char *rel_path)
{
	snprintf(out, size, "%s%s", game_dir, rel_path);
}

// ---------------------------------------------------------------------------------------------- HTTPS

// GET <url> into a HeapAlloc block (at most max bytes); 0 on any failure. Redirects (GitHub sends downloads to its asset host) are followed by WinHTTP.
static uint8_t *http_get(const char *url, size_t max, size_t *got, int api, volatile LONG *pct, size_t expect)
{
	*got = 0;
	wchar_t wurl[1024], host[256], path[1024], extra[512];
	if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024))
		return NULL;
	URL_COMPONENTS uc;
	memset(&uc, 0, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 1024;
	uc.lpszExtraInfo = extra;
	uc.dwExtraInfoLength = 512;
	if (!WinHttpCrackUrl(wurl, 0, 0, &uc))
		return NULL;
	int secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
	if (!secure && !qa_url()) {
		log_printf("update: refused a plain http address");
		return NULL;
	}
	if (extra[0])
		wcsncat(path, extra, 1023 - wcslen(path));
	wchar_t agent[96];
	swprintf(agent, 96, L"mw2coop-updater/%hs", update_version());
	HINTERNET session = WinHttpOpen(agent, 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, Windows 8.1+ */, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return NULL;
	WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
	uint8_t *buf = NULL;
	size_t cap = 0, len = 0;
	int ok = 0;
	HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
	HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
	const wchar_t *headers = api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n" : L"Accept: application/octet-stream\r\n";
	if (req && WinHttpSendRequest(req, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, NULL)) {
		DWORD status = 0, sz = sizeof(status);
		WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
		if (status != 200) {
			log_printf("update: %s answered HTTP %lu", url, (unsigned long)status);
		} else {
			ok = 1;
			for (;;) {
				DWORD avail = 0, read = 0;
				if (!WinHttpQueryDataAvailable(req, &avail)) {
					ok = 0;
					break;
				}
				if (!avail)
					break;
				if (len + avail > max) {
					log_printf("update: %s is larger than %u bytes", url, (unsigned)max);
					ok = 0;
					break;
				}
				if (len + avail > cap) {
					size_t want = cap ? cap * 2 : (expect && expect <= max ? expect : 65536);
					while (want < len + avail)
						want *= 2;
					if (want > max)
						want = max;
					uint8_t *more = buf ? HeapReAlloc(GetProcessHeap(), 0, buf, want) : HeapAlloc(GetProcessHeap(), 0, want);
					if (!more) {
						ok = 0;
						break;
					}
					buf = more;
					cap = want;
				}
				if (!WinHttpReadData(req, buf + len, avail, &read)) {
					ok = 0;
					break;
				}
				len += read;
				if (pct && expect)
					InterlockedExchange(pct, (LONG)(len >= expect ? 100 : len * 100 / expect));
			}
		}
	} else
		log_printf("update: no answer from %s (error %lu)", url, (unsigned long)GetLastError());
	if (req)
		WinHttpCloseHandle(req);
	if (conn)
		WinHttpCloseHandle(conn);
	WinHttpCloseHandle(session);
	if (!ok) {
		if (buf)
			HeapFree(GetProcessHeap(), 0, buf);
		return NULL;
	}
	if (!buf)
		buf = HeapAlloc(GetProcessHeap(), 0, 1);
	*got = len;
	return buf;
}

// ---------------------------------------------------------------------------------------------- the check

static int dlss_installed(void)
{
#ifdef _WIN64
	char p[MAX_PATH + 64];
	game_path(p, sizeof(p), "mw2coop\\dlss\\mw2coop_dlss.dll");
	return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
	return 0;
#endif
}

static void archive_names(const char *tag, char main_name[128], char dlss_name[128])
{
#ifdef _WIN64
	snprintf(main_name, 128, "mw2coop-%s.zip", tag);
	snprintf(dlss_name, 128, "mw2coop-dlss-%s.zip", tag);
#else
	snprintf(main_name, 128, "mw2coop-%s-x86.zip", tag);
	dlss_name[0] = 0;
#endif
}

// 1 newer, 0 not newer, -1 this build cannot tell (a development build)
static int release_is_newer(const upd_release *r)
{
	int mine[3], theirs[3];
	const char *me = update_version();
	if (!upd_parse_version(r->tag, theirs))
		return 0;
	if (upd_parse_version(me, mine))
		return upd_compare_versions(theirs, mine) > 0;
	long nightly = upd_nightly_date(me), published = upd_iso_date(r->published);
	if (nightly)
		return published > nightly; // a nightly is newer than every release before its day
	return -1;
}

static DWORD WINAPI check_thread(LPVOID unused)
{
	(void)unused;
	char url[512];
	if (!GetEnvironmentVariableA("MW2COOP_UPDATE_URL", url, sizeof(url)))
		lstrcpynA(url, API_URL, sizeof(url));
	size_t len = 0;
	char *json = (char *)http_get(url, 1u << 20, &len, 1, NULL, 0);
	upd_release *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
	LONG next = ST_CHECK_FAILED;
	if (!json || !r || !upd_parse_release(json, len, r)) {
		log_printf("update: the newest release could not be read%s", json ? " (not a release)" : "");
	} else if (r->draft || r->prerelease) {
		log_printf("update: the newest release %s is a draft or a pre-release: left alone", r->tag);
		next = ST_NEWEST;
	} else {
		int newer = release_is_newer(r);
		char main_name[128], dlss_name[128];
		archive_names(r->tag, main_name, dlss_name);
		log_printf("update: this build %s, the newest release %s (%.10s, %d files)%s", update_version(), r->tag, r->published, r->asset_count,
			   newer > 0 ? ": newer" : newer < 0 ? ": this is a development build, nothing offered" : ": nothing newer");
		if (newer > 0 && !upd_find_asset(r, main_name)) {
			log_printf("update: the release has no %s", main_name);
			newer = 0;
		}
		if (newer > 0) {
			memcpy(&rel, r, sizeof(rel));
			next = ST_AVAILABLE;
		} else
			next = newer < 0 ? ST_DEV : ST_NEWEST;
	}
	if (json)
		HeapFree(GetProcessHeap(), 0, json);
	if (r)
		HeapFree(GetProcessHeap(), 0, r);
	if (InterlockedCompareExchange(&state, 0, 0) != ST_WORKING && InterlockedCompareExchange(&state, 0, 0) != ST_DONE)
		InterlockedExchange(&state, next);
	InterlockedExchange(&checking, 0);
	return 0;
}

static void start_check(void)
{
	if (InterlockedExchange(&checking, 1))
		return;
	LONG s = InterlockedCompareExchange(&state, 0, 0);
	if (s == ST_WORKING || s == ST_DONE) {
		InterlockedExchange(&checking, 0);
		return;
	}
	InterlockedExchange(&state, ST_CHECKING);
	HANDLE t = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	else
		InterlockedExchange(&checking, 0);
}

// ---------------------------------------------------------------------------------------------- installing

typedef struct {
	char rel[260];	  // path in the game folder
	char staged[MAX_PATH + 300];
	char moved[MAX_PATH + 300]; // where the file it replaces went ("" when there was none)
	int done;
} job_t;

static job_t *jobs;
static int job_count;
static LONG old_seq;

static int make_dirs(const char *file_path)
{
	char p[MAX_PATH + 300];
	lstrcpynA(p, file_path, sizeof(p));
	for (char *s = p + 3; *s; s++)
		if (*s == '\\') {
			*s = 0;
			if (!CreateDirectoryA(p, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
				*s = '\\';
				return 0;
			}
			*s = '\\';
		}
	return 1;
}

static int write_file(const char *path, const void *data, size_t len)
{
	if (!make_dirs(path))
		return 0;
	HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return 0;
	DWORD wrote = 0;
	BOOL ok = WriteFile(f, data, (DWORD)len, &wrote, NULL) && wrote == len && FlushFileBuffers(f);
	CloseHandle(f);
	return ok;
}

static uint8_t *read_file(const char *path, size_t *len, size_t max)
{
	*len = 0;
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return NULL;
	LARGE_INTEGER size;
	uint8_t *buf = NULL;
	if (GetFileSizeEx(f, &size) && (unsigned long long)size.QuadPart <= max) {
		buf = HeapAlloc(GetProcessHeap(), 0, size.QuadPart ? (size_t)size.QuadPart : 1);
		DWORD got = 0;
		if (buf && (!size.QuadPart || (ReadFile(f, buf, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart)))
			*len = (size_t)size.QuadPart;
		else if (buf) {
			HeapFree(GetProcessHeap(), 0, buf);
			buf = NULL;
		}
	}
	CloseHandle(f);
	return buf;
}

// a whole folder's files removed (the staging area, the old files); what is in use stays for the next start
static void remove_tree(const char *dir)
{
	char pattern[MAX_PATH + 300];
	snprintf(pattern, sizeof(pattern), "%s\\*", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
			continue;
		char p[MAX_PATH + 300];
		snprintf(p, sizeof(p), "%s\\%s", dir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			remove_tree(p);
			RemoveDirectoryA(p);
		} else {
			SetFileAttributesA(p, FILE_ATTRIBUTE_NORMAL);
			DeleteFileA(p);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

// out of the way: the file goes to mw2coop\update\old (a file in use can be renamed on the same drive, not written)
static int move_old(const char *target, char *moved, size_t size)
{
	const char *base = strrchr(target, '\\');
	base = base ? base + 1 : target;
	char dir[MAX_PATH + 64];
	game_path(dir, sizeof(dir), "mw2coop\\update\\old");
	CreateDirectoryA(dir, NULL);
	snprintf(moved, size, "%s\\%ld_%s", dir, (long)InterlockedIncrement(&old_seq), base);
	SetFileAttributesA(target, FILE_ATTRIBUTE_NORMAL);
	return MoveFileExA(target, moved, MOVEFILE_REPLACE_EXISTING) != 0;
}

static void rollback(void)
{
	for (int i = job_count; i-- > 0;) {
		job_t *j = &jobs[i];
		if (!j->done)
			continue;
		char target[MAX_PATH + 300];
		game_path(target, sizeof(target), j->rel);
		char tmp[MAX_PATH + 300];
		move_old(target, tmp, sizeof(tmp)); // the new file out again
		if (j->moved[0] && !MoveFileExA(j->moved, target, MOVEFILE_REPLACE_EXISTING))
			log_printf("update: could not put back %s (it is %s)", j->rel, j->moved);
	}
	old_dll[0] = 0;
	log_printf("update: every file put back as it was");
}

typedef struct {
	char rel[260];
	char sha[65];
} manifest_line;

// mw2coop\release.txt: "<sha256> <path>" a line (the first line names the release)
static int manifest_read(const uint8_t *text, size_t len, manifest_line *out, int max)
{
	int n = 0;
	const char *p = (const char *)text, *end = p + len;
	while (p < end && n < max) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol)
			eol = end;
		size_t l = (size_t)(eol - p);
		while (l && (p[l - 1] == '\r' || p[l - 1] == ' '))
			l--;
		if (l > 66 && p[64] == ' ' && l - 65 < sizeof(out[n].rel)) {
			int hex = 1;
			for (int i = 0; i < 64; i++)
				hex &= (p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f');
			if (hex) {
				memcpy(out[n].sha, p, 64);
				out[n].sha[64] = 0;
				memcpy(out[n].rel, p + 65, l - 65);
				out[n].rel[l - 65] = 0;
				char safe[260];
				if (upd_safe_path(out[n].rel, safe, sizeof(safe))) {
					lstrcpynA(out[n].rel, safe, sizeof(out[n].rel));
					n++;
				}
			}
		}
		p = eol + 1;
	}
	return n;
}

static int fail(int reason, const char *what)
{
	log_printf("update: %s", what);
	InterlockedExchange(&fail_reason, reason);
	return 0;
}

// download, check and unpack one archive into the staging area (its jobs added)
static int stage_archive(const upd_asset *a, const char *staging)
{
	char want[65] = "";
	lstrcpynA(want, a->sha256, sizeof(want));
	char sum_name[160];
	snprintf(sum_name, sizeof(sum_name), "%s.sha256", a->name);
	const upd_asset *sum = upd_find_asset(&rel, sum_name);
	if (sum) { // the release's own checksum file: "<hex>  <name>"
		size_t len = 0;
		char *text = (char *)http_get(sum->url, 4096, &len, 0, NULL, 0);
		if (!text)
			return fail(E_NET, "the checksum file could not be downloaded");
		char hex[65] = "";
		if (len >= 64) {
			for (int i = 0; i < 64; i++)
				hex[i] = (char)(text[i] >= 'A' && text[i] <= 'F' ? text[i] + 32 : text[i]);
			hex[64] = 0;
		}
		HeapFree(GetProcessHeap(), 0, text);
		if (want[0] && strcmp(want, hex) != 0)
			return fail(E_HASH, "GitHub's digest and the release's checksum file differ");
		lstrcpynA(want, hex, sizeof(want));
	}
	if (strlen(want) != 64)
		return fail(E_HASH, "the release gives no SHA-256 for the archive");
	InterlockedExchange(&progress, 0);
	size_t len = 0;
	uint8_t *zip = http_get(a->url, MAX_ARCHIVE, &len, 0, &progress, a->size > 0 ? (size_t)a->size : 0);
	if (!zip)
		return fail(E_NET, "the archive could not be downloaded");
	char got[65] = "";
	if ((a->size > 0 && (long long)len != a->size) || !upd_sha256(zip, len, got) || strcmp(got, want) != 0) {
		HeapFree(GetProcessHeap(), 0, zip);
		log_printf("update: %s: %u bytes, SHA-256 %s, expected %s", a->name, (unsigned)len, got, want);
		return fail(E_HASH, "the archive's SHA-256 is not the release's");
	}
	log_printf("update: %s downloaded, %u bytes, SHA-256 %s", a->name, (unsigned)len, got);
	upd_zip_entry *entries = HeapAlloc(GetProcessHeap(), 0, MAX_FILES * sizeof(*entries));
	int n = entries ? upd_zip_list(zip, len, entries, MAX_FILES) : -1;
	int ok = n > 0;
	if (!ok)
		fail(E_ARCHIVE, "the archive's file list cannot be read");
	for (int i = 0; ok && i < n; i++) {
		char rel_path[260];
		if (!upd_safe_path(entries[i].path, rel_path, sizeof(rel_path))) {
			log_printf("update: %s is not a file the mod's archive may write: refused", entries[i].path);
			ok = fail(E_ARCHIVE, "the archive has a file outside the mod's places");
			break;
		}
		uint8_t *data = upd_zip_extract(zip, len, &entries[i]);
		if (!data) {
			log_printf("update: %s could not be unpacked", entries[i].path);
			ok = fail(E_ARCHIVE, "a file of the archive is damaged");
			break;
		}
		if (job_count >= MAX_FILES) {
			HeapFree(GetProcessHeap(), 0, data);
			ok = fail(E_ARCHIVE, "too many files");
			break;
		}
		job_t *j = &jobs[job_count];
		memset(j, 0, sizeof(*j));
		lstrcpynA(j->rel, rel_path, sizeof(j->rel));
		snprintf(j->staged, sizeof(j->staged), "%s\\%s", staging, rel_path);
		if (!write_file(j->staged, data, entries[i].usize)) {
			HeapFree(GetProcessHeap(), 0, data);
			ok = fail(E_WRITE, "the staging folder cannot be written");
			break;
		}
		HeapFree(GetProcessHeap(), 0, data);
		job_count++;
	}
	if (entries)
		HeapFree(GetProcessHeap(), 0, entries);
	HeapFree(GetProcessHeap(), 0, zip);
	return ok;
}

static int same_content(const char *a, const char *b)
{
	size_t la = 0, lb = 0;
	uint8_t *da = read_file(a, &la, MAX_ARCHIVE), *db = da ? read_file(b, &lb, MAX_ARCHIVE) : NULL;
	int same = da && db && la == lb && memcmp(da, db, la) == 0;
	if (da)
		HeapFree(GetProcessHeap(), 0, da);
	if (db)
		HeapFree(GetProcessHeap(), 0, db);
	return same;
}

static int install(void)
{
	char main_name[128], dlss_name[128];
	archive_names(rel.tag, main_name, dlss_name);
	const upd_asset *assets[2] = {upd_find_asset(&rel, main_name), dlss_installed() && dlss_name[0] ? upd_find_asset(&rel, dlss_name) : NULL};
	if (!assets[0])
		return fail(E_ASSET, "the release has no archive for this build");
	char staging[MAX_PATH + 64], dir[MAX_PATH + 64];
	game_path(dir, sizeof(dir), "mw2coop\\update");
	CreateDirectoryA(dir, NULL);
	game_path(staging, sizeof(staging), "mw2coop\\update\\staging");
	remove_tree(staging);
	CreateDirectoryA(staging, NULL);
	job_count = 0;
	for (int k = 0; k < 2; k++) {
		if (!assets[k])
			continue;
		lstrcpynA(archive_name[k], assets[k]->name, sizeof(archive_name[k]));
		InterlockedExchange(&archive_at, k);
		InterlockedExchange(&phase, 1);
		if (!stage_archive(assets[k], staging))
			return 0;
	}
	int has_dll = 0;
	for (int i = 0; i < job_count; i++)
		has_dll |= !lstrcmpiA(jobs[i].rel, "winmm.dll");
	if (!has_dll)
		return fail(E_ARCHIVE, "the archive has no winmm.dll");
	// the previous release's manifest, read before it is replaced
	char path[MAX_PATH + 64];
	game_path(path, sizeof(path), "mw2coop\\release.txt");
	size_t mlen = 0;
	uint8_t *mtext = read_file(path, &mlen, 1u << 20);
	manifest_line *previous = mtext ? HeapAlloc(GetProcessHeap(), 0, MAX_FILES * sizeof(*previous)) : NULL;
	int previous_count = previous ? manifest_read(mtext, mlen, previous, MAX_FILES) : 0;
	if (mtext)
		HeapFree(GetProcessHeap(), 0, mtext);
	// the files go in while the main menu is up (never while a lobby or a level uses them)
	InterlockedExchange(&phase, 2);
	// (the menu tick stops while a level loads or runs: a stale "main menu" does not count)
	while (!InterlockedCompareExchange(&at_main_menu, 0, 0) || GetTickCount() - (DWORD)InterlockedCompareExchange(&menu_tick_at, 0, 0) > 1000)
		Sleep(250);
	int changed = 0, ok = 1;
	for (int i = 0; ok && i < job_count; i++) {
		job_t *j = &jobs[i];
		char target[MAX_PATH + 300];
		game_path(target, sizeof(target), j->rel);
		DWORD attr = GetFileAttributesA(target);
		if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
			ok = fail(E_WRITE, "a folder stands where a file goes");
			break;
		}
		if (attr != INVALID_FILE_ATTRIBUTES && same_content(target, j->staged))
			continue;
		if (!make_dirs(target)) {
			ok = fail(E_WRITE, "a folder of the game cannot be made");
			break;
		}
		if (attr != INVALID_FILE_ATTRIBUTES && !move_old(target, j->moved, sizeof(j->moved))) {
			log_printf("update: %s cannot be moved (error %lu)", j->rel, (unsigned long)GetLastError());
			ok = fail(E_WRITE, "a file of the game cannot be replaced");
			break;
		}
		if (!MoveFileExA(j->staged, target, MOVEFILE_REPLACE_EXISTING)) {
			log_printf("update: %s cannot be put in place (error %lu)", j->rel, (unsigned long)GetLastError());
			if (j->moved[0])
				MoveFileExA(j->moved, target, MOVEFILE_REPLACE_EXISTING);
			ok = fail(E_WRITE, "a file of the game cannot be written");
			break;
		}
		j->done = 1;
		changed++;
		if (!lstrcmpiA(j->rel, "winmm.dll"))
			lstrcpynA(old_dll, j->moved, sizeof(old_dll));
	}
	if (!ok) {
		rollback();
		if (previous)
			HeapFree(GetProcessHeap(), 0, previous);
		return 0;
	}
	// what the previous release had and this one has not: moved out when the player did not change it
	int removed = 0;
	for (int i = 0; i < previous_count; i++) {
		int kept = 0;
		for (int k = 0; k < job_count && !kept; k++)
			kept = !lstrcmpiA(jobs[k].rel, previous[i].rel);
		if (kept || !_strnicmp(previous[i].rel, "mw2coop\\dlss\\", 13))
			continue;
		char target[MAX_PATH + 300], hex[65] = "", moved[MAX_PATH + 300];
		game_path(target, sizeof(target), previous[i].rel);
		size_t flen = 0;
		uint8_t *data = read_file(target, &flen, MAX_ARCHIVE);
		if (!data)
			continue;
		int same = upd_sha256(data, flen, hex) && !strcmp(hex, previous[i].sha);
		HeapFree(GetProcessHeap(), 0, data);
		if (same && move_old(target, moved, sizeof(moved))) {
			removed++;
			log_printf("update: %s is no longer part of the mod: moved out", previous[i].rel);
		}
	}
	if (previous)
		HeapFree(GetProcessHeap(), 0, previous);
	game_path(path, sizeof(path), "mw2coop\\update\\installed.txt");
	char note[160];
	int nl = snprintf(note, sizeof(note), "%s\r\n%s\r\n", update_version(), rel.tag);
	write_file(path, note, (size_t)nl);
	remove_tree(staging);
	log_printf("update: %s installed: %d files replaced or added, %d unchanged, %d removed", rel.tag, changed, job_count - changed, removed);
	return 1;
}

static DWORD WINAPI install_thread(LPVOID unused)
{
	(void)unused;
	jobs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_FILES * sizeof(*jobs));
	int ok = jobs && install();
	if (jobs) {
		HeapFree(GetProcessHeap(), 0, jobs);
		jobs = NULL;
	}
	if (!ok && !InterlockedCompareExchange(&fail_reason, 0, 0))
		InterlockedExchange(&fail_reason, E_WRITE);
	if (!ok) { // (what was unpacked goes; the files put back are in place)
		char staging[MAX_PATH + 64];
		game_path(staging, sizeof(staging), "mw2coop\\update\\staging");
		remove_tree(staging);
		RemoveDirectoryA(staging);
	}
	InterlockedExchange(&state, ok ? ST_DONE : ST_FAILED);
	return 0;
}

// ---------------------------------------------------------------------------------------------- restart

// mw2coop\update\relaunch.txt: the pid to wait for, the exe, its command line, its folder
static int write_relaunch(char *path, size_t size)
{
	game_path(path, size, "mw2coop\\update\\relaunch.txt");
	char exe[MAX_PATH], text[4 * MAX_PATH + 2048];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	int n = snprintf(text, sizeof(text), "%lu\r\n%s\r\n%s\r\n%s\r\n", (unsigned long)GetCurrentProcessId(), exe, GetCommandLineA(), game_dir);
	return n > 0 && (size_t)n < sizeof(text) && write_file(path, text, (size_t)n);
}

// rundll32 "<winmm.dll>",mw2coop_relaunch <relaunch.txt>: in the helper process (proxy.c lets the DLL do nothing else there). Waits up to a minute for the game
// to end, then starts it again.
void CALLBACK mw2coop_relaunch(HWND hwnd, HINSTANCE inst, LPSTR cmd, int show);
void CALLBACK mw2coop_relaunch(HWND hwnd, HINSTANCE inst, LPSTR cmd, int show)
{
	(void)hwnd, (void)inst, (void)show;
	char path[MAX_PATH + 64];
	const char *c = cmd ? cmd : "";
	while (*c == ' ' || *c == '"')
		c++;
	lstrcpynA(path, c, sizeof(path));
	for (size_t n = strlen(path); n && (path[n - 1] == '"' || path[n - 1] == ' ');)
		path[--n] = 0;
	size_t len = 0;
	char *text = (char *)read_file(path, &len, 64 * 1024);
	if (!text)
		return;
	char *lines[4] = {0};
	int k = 0;
	for (char *p = text; k < 4 && p < text + len;) {
		lines[k++] = p;
		char *eol = memchr(p, '\n', (size_t)(text + len - p));
		if (!eol)
			break;
		*eol = 0;
		if (eol > p && eol[-1] == '\r')
			eol[-1] = 0;
		p = eol + 1;
	}
	if (k == 4) {
		HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)strtoul(lines[0], NULL, 10));
		DWORD wait = game ? WaitForSingleObject(game, 60000) : WAIT_OBJECT_0;
		if (game)
			CloseHandle(game);
		if (wait == WAIT_OBJECT_0) {
			Sleep(500);
			STARTUPINFOA si;
			PROCESS_INFORMATION pi;
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			if (CreateProcessA(lines[1], lines[2], NULL, NULL, FALSE, 0, NULL, lines[3], &si, &pi)) {
				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
			}
		}
	}
	HeapFree(GetProcessHeap(), 0, text);
	DeleteFileA(path);
}

// mw2coop_update_restart: the popup's Restart
static void restart_f(void)
{
	if (InterlockedCompareExchange(&state, 0, 0) != ST_DONE)
		return;
	char path[MAX_PATH + 64], dll[MAX_PATH + 300], sys[MAX_PATH], cmd[3 * MAX_PATH + 400];
	if (old_dll[0])
		lstrcpynA(dll, old_dll, sizeof(dll)); // the code that runs now (the new winmm.dll is in its place)
	else
		game_path(dll, sizeof(dll), "winmm.dll");
	GetSystemDirectoryA(sys, MAX_PATH); // (a 32-bit game gets the 32-bit rundll32 through the file system redirection)
	snprintf(cmd, sizeof(cmd), "\"%s\\rundll32.exe\" \"%s\",mw2coop_relaunch %s", sys, dll, write_relaunch(path, sizeof(path)) ? path : "");
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, game_dir, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		log_printf("update: restarting the game");
	} else
		log_printf("update: the restart helper could not be started (error %lu): the game quits, start it again", (unsigned long)GetLastError());
	game_cbuf_addtext("quit\n");
}

// ---------------------------------------------------------------------------------------------- the popups and commands

// mw2coop_update_go: the popup's Update
static void go_f(void)
{
	if (InterlockedCompareExchange(&state, ST_WORKING, ST_AVAILABLE) != ST_AVAILABLE)
		return;
	InterlockedExchange(&fail_reason, E_NONE);
	InterlockedExchange(&phase, 1);
	InterlockedExchange(&progress, 0);
	log_printf("update: installing %s", rel.tag);
	HANDLE t = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
	if (!t) {
		InterlockedExchange(&state, ST_FAILED);
		InterlockedExchange(&fail_reason, E_WRITE);
		return;
	}
	CloseHandle(t);
	game_cbuf_addtext("openmenu mw2coop_update_wait\n");
}

// mw2coop_update: check now (console; also what QA uses), and say what was found
static void check_f(void)
{
	InterlockedExchange(&manual, 1);
	start_check();
}

static void set_question(const char *alias, const char *fmt_key, const char *want, const char *a, const char *b)
{
	const char *fmt = localize_text(fmt_key);
	char text[256];
	if (!fmt || !ctexts_fmt_ok(fmt, want))
		return;
	if (want[0] == 0)
		lstrcpynA(text, fmt, sizeof(text));
	else if (!strcmp(want, "s"))
		snprintf(text, sizeof(text), fmt, a);
	else if (!strcmp(want, "ss"))
		snprintf(text, sizeof(text), fmt, a, b);
	else
		return;
	localize_set_text(alias, text);
}

static void tag_text(char *out, size_t size, const char *tag)
{
	snprintf(out, size, "%s", tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
}

static LONG asked, done_shown, fail_shown;

void update_tick(int main_menu)
{
	static const char *const popups[] = {"mw2coop_update", "mw2coop_update_wait", "mw2coop_update_done", "mw2coop_update_note"};
	for (int i = 0; i < 4; i++)
		menus_yes_no_popup(popups[i]);
	menus_show_row(ui_find_menu("mw2coop_update_wait"), "no", 0); // (one-button popups: their second row is hidden)
	menus_show_row(ui_find_menu("mw2coop_update_note"), "no", 0);
	InterlockedExchange(&menu_tick_at, (LONG)GetTickCount());
	static DWORD menu_since;
	if (!main_menu)
		menu_since = 0;
	else if (!menu_since)
		menu_since = GetTickCount() | 1;
	int menu_up = main_menu && GetTickCount() - menu_since > 1500; // the main menu has been up a moment (not a popup over the intro's fade)
	InterlockedExchange(&at_main_menu, menu_up);
	LONG s = InterlockedCompareExchange(&state, 0, 0);
	if (s == ST_WORKING) { // the wait popup's line
		char text[200], name[96];
		LONG k = InterlockedCompareExchange(&archive_at, 0, 0);
		lstrcpynA(name, archive_name[k & 1], sizeof(name));
		const char *fmt = localize_text("MW2COOP_UPDATE_DL_FMT");
		if (InterlockedCompareExchange(&phase, 0, 0) == 2) {
			const char *t = localize_text("MW2COOP_UPDATE_INSTALLING");
			localize_set_text("MW2COOP_UPDATE_WAIT_Q", t ? t : "Installing...");
		} else if (fmt && ctexts_fmt_ok(fmt, "sd")) {
			snprintf(text, sizeof(text), fmt, name, (int)InterlockedCompareExchange(&progress, 0, 0));
			localize_set_text("MW2COOP_UPDATE_WAIT_Q", text);
		}
	}
	if (!menu_up || ui_error_popup_open()) // (the popups wait for an engine error popup to be closed)
		return;
	char now[48], next[48];
	tag_text(now, sizeof(now), update_version());
	switch (s) {
	case ST_AVAILABLE: {
		LONG was_asked = InterlockedExchange(&asked, 1), was_manual = InterlockedExchange(&manual, 0);
		if (was_asked && !was_manual)
			break; // asked once a run ("Later"); the console's mw2coop_update asks again
	}
		tag_text(next, sizeof(next), rel.tag);
		set_question("MW2COOP_UPDATE_Q", "MW2COOP_UPDATE_FMT", "ss", next, now);
		game_cbuf_addtext("openmenu mw2coop_update\n");
		log_printf("update: %s offered", rel.tag);
		break;
	case ST_NEWEST:
	case ST_DEV:
	case ST_CHECK_FAILED:
		if (!InterlockedExchange(&manual, 0))
			break;
		if (s == ST_NEWEST)
			set_question("MW2COOP_UPDATE_NOTE_Q", "MW2COOP_UPDATE_NEWEST_FMT", "s", now, NULL);
		else
			set_question("MW2COOP_UPDATE_NOTE_Q", s == ST_DEV ? "MW2COOP_UPDATE_DEV" : "MW2COOP_UPDATE_NOCHECK", "", NULL, NULL);
		game_cbuf_addtext("openmenu mw2coop_update_note\n");
		break;
	case ST_DONE:
		if (InterlockedExchange(&done_shown, 1))
			break;
		tag_text(next, sizeof(next), rel.tag);
		set_question("MW2COOP_UPDATE_DONE_Q", "MW2COOP_UPDATE_DONE_FMT", "s", next, NULL);
		game_cbuf_addtext("closemenu mw2coop_update_wait\nopenmenu mw2coop_update_done\n");
		break;
	case ST_FAILED:
		if (InterlockedExchange(&fail_shown, 1))
			break;
		{
			LONG r = InterlockedCompareExchange(&fail_reason, 0, 0);
			const char *why = localize_text(reason_keys[r > 0 && r <= E_ASSET ? r : E_WRITE]);
			set_question("MW2COOP_UPDATE_NOTE_Q", "MW2COOP_UPDATE_FAIL_FMT", "s", why ? why : "", NULL);
		}
		game_cbuf_addtext("closemenu mw2coop_update_wait\nopenmenu mw2coop_update_note\n");
		break;
	}
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_update", NULL, NULL, check_f CMD_NODE_FLAG},
	{NULL, "mw2coop_update_go", NULL, NULL, go_f CMD_NODE_FLAG},
	{NULL, "mw2coop_update_restart", NULL, NULL, restart_f CMD_NODE_FLAG},
};

// the start: the previous update's leftovers, then the check
static DWORD WINAPI start_thread(LPVOID unused)
{
	(void)unused;
	char path[MAX_PATH + 64];
	game_path(path, sizeof(path), "mw2coop\\update\\old");
	remove_tree(path);
	RemoveDirectoryA(path);
	game_path(path, sizeof(path), "mw2coop\\update\\staging");
	remove_tree(path);
	RemoveDirectoryA(path);
	game_path(path, sizeof(path), "mw2coop\\update\\installed.txt");
	size_t len = 0;
	char *note = (char *)read_file(path, &len, 1024);
	if (note) {
		char from[48] = "", to[48] = "";
		sscanf(note, "%47s %47s", from, to);
		log_printf("update: this start runs %s, installed in the game over %s", to, from);
		HeapFree(GetProcessHeap(), 0, note);
		DeleteFileA(path);
	}
	const char *ini = settings_ini_path();
	int on = ini && ini[0] ? (int)GetPrivateProfileIntA("update", "check", 1, ini) : 1;
	log_printf("update: this build is %s%s%s; the check at start is %s", update_version(), strcmp(update_version(), MW2COOP_VERSION) ? " (MW2COOP_UPDATE_AS; built as " MW2COOP_VERSION : "",
		   strcmp(update_version(), MW2COOP_VERSION) ? ")" : "", on ? "on" : "off ([update] check=0)");
	if (!on)
		return 0;
	int v[3];
	if (!upd_parse_version(update_version(), v) && !upd_nightly_date(update_version())) {
		InterlockedExchange(&state, ST_DEV);
		return 0; // a development build: only the console's mw2coop_update asks
	}
	Sleep(3000);
	start_check();
	return 0;
}

void update_install(void)
{
	dir_init();
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
	HANDLE t = CreateThread(NULL, 0, start_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
}
