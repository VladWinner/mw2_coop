// "Report a bug" (owner 2026-11-23, the night list: "one button gathers into a zip the log of both PCs (the partner's over the network), the netwatch
// records, a screenshot and crash.dmp, without private paths; Vlad's and his friends' reports become complete the first time").
//
// The pause menu's "Report a bug" (and the console command mw2coop_report) makes <game dir>\mw2coop\reports\mw2coop-report-<date>-<time>.zip:
//   README.txt         what is in it: the mod's version and build, the game's exe, Windows' version, the map, the time, what could not be added
//   mw2coop.log        this PC's log (its last 8 MB); netwatch writes its notices and its records of the two games' state hashes there
//   partner-mw2coop.log the partner's log (its last 1 MB), asked for over the network in a co-op session (the partner's PC sends it in pieces of 1000 bytes)
//   screenshot.bmp     the frame the game draws when the report is made (the pause menu closes first)
//   crash.dmp          the last crash's dump, if there is one
//   settings.ini       the mod's settings
// Private paths go out of every text: the Windows user folder becomes %USERPROFILE%, the game's folder <game dir>, the user and the PC's names <user> and
// <pc>. The zip is stored (no compression: the format needs nothing but a CRC), so any archiver opens it.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctexts.h"
#include "game.h"
#include "localize.h"
#include "log.h"
#include "menus_ui.h"
#include "native.h"
#include "net.h"
#include "netwatch.h"
#include "report.h"
#include "shot.h"

#ifndef MW2COOP_VERSION
#define MW2COOP_VERSION "dev"
#endif

#define OWN_LOG_MAX (8u << 20)
#define PARTNER_LOG_MAX (1u << 20)
#define PART 1000 // bytes of the partner's log in one packet

static char dir[MAX_PATH]; // <game dir>\ (with the backslash)
static volatile LONG busy;
static char report_id[32];

// ---- private paths out of a text (in place; the text may grow: the caller gives room)

static char user_dir[MAX_PATH], game_dir_noslash[MAX_PATH], user_name[128], pc_name[128];

static int replace_all(char *buf, int len, int cap, const char *what, const char *with)
{
	const int wl = (int)strlen(what), nl = (int)strlen(with);
	if (wl < 3)
		return len;
	for (int i = 0; i + wl <= len;) {
		if (_strnicmp(buf + i, what, wl) == 0) {
			if (len - wl + nl >= cap)
				return len;
			memmove(buf + i + nl, buf + i + wl, len - i - wl);
			memcpy(buf + i, with, nl);
			len += nl - wl;
			i += nl;
		} else
			i++;
	}
	return len;
}

static int scrub(char *buf, int len, int cap)
{
	len = replace_all(buf, len, cap, game_dir_noslash, "<game dir>"); // (first: the game's folder may be inside the user's)
	len = replace_all(buf, len, cap, user_dir, "%USERPROFILE%");
	len = replace_all(buf, len, cap, user_name, "<user>");
	len = replace_all(buf, len, cap, pc_name, "<pc>");
	return len;
}

// the last <max> bytes of a file (from the start of a line), scrubbed; NULL if there is none
static char *read_tail(const char *path, unsigned max, int *out_len)
{
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return NULL;
	LARGE_INTEGER size;
	GetFileSizeEx(h, &size);
	unsigned n = size.QuadPart > max ? max : (unsigned)size.QuadPart;
	LARGE_INTEGER at;
	at.QuadPart = size.QuadPart - n;
	SetFilePointerEx(h, at, NULL, FILE_BEGIN);
	const int cap = (int)n + 65536;
	char *buf = HeapAlloc(GetProcessHeap(), 0, cap);
	DWORD got = 0;
	if (buf && !ReadFile(h, buf, n, &got, NULL))
		got = 0;
	CloseHandle(h);
	if (!buf)
		return NULL;
	int start = 0;
	if (at.QuadPart > 0) // (a cut line at the start goes)
		while (start < (int)got && buf[start] != '\n')
			start++;
	int len = (int)got - start;
	memmove(buf, buf + start, len);
	for (int i = 0; i < len; i++)
		if (!buf[i])
			buf[i] = ' '; // (the partner's pieces travel as text)
	*out_len = scrub(buf, len, cap);
	return buf;
}

// ---- the zip (stored)

static uint32_t crc_table[256];
static uint32_t crc32_of(const void *data, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			crc_table[i] = c;
		}
	uint32_t c = 0xffffffffu;
	const uint8_t *p = data;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

typedef struct {
	char name[64];
	uint32_t crc, size, offset;
} zip_entry_t;

typedef struct {
	HANDLE h;
	uint32_t at;
	zip_entry_t e[12];
	int n;
	uint16_t dos_time, dos_date;
} zip_t;

static void put(zip_t *z, const void *data, uint32_t n)
{
	DWORD w = 0;
	WriteFile(z->h, data, n, &w, NULL);
	z->at += w;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v), put16(p + 2, (uint16_t)(v >> 16));
}

static void zip_add(zip_t *z, const char *name, const void *data, uint32_t size)
{
	if (z->n >= 12 || !data)
		return;
	zip_entry_t *e = &z->e[z->n++];
	lstrcpynA(e->name, name, sizeof(e->name));
	e->crc = crc32_of(data, size);
	e->size = size;
	e->offset = z->at;
	uint8_t hdr[30] = {0};
	const uint16_t nl = (uint16_t)strlen(e->name);
	put32(hdr, 0x04034b50);
	put16(hdr + 4, 20);
	put16(hdr + 10, z->dos_time);
	put16(hdr + 12, z->dos_date);
	put32(hdr + 14, e->crc);
	put32(hdr + 18, size);
	put32(hdr + 22, size);
	put16(hdr + 26, nl);
	put(z, hdr, 30);
	put(z, e->name, nl);
	put(z, data, size);
}

static void zip_end(zip_t *z)
{
	const uint32_t cd = z->at;
	for (int i = 0; i < z->n; i++) {
		zip_entry_t *e = &z->e[i];
		uint8_t hdr[46] = {0};
		const uint16_t nl = (uint16_t)strlen(e->name);
		put32(hdr, 0x02014b50);
		put16(hdr + 4, 20);
		put16(hdr + 6, 20);
		put16(hdr + 12, z->dos_time);
		put16(hdr + 14, z->dos_date);
		put32(hdr + 16, e->crc);
		put32(hdr + 20, e->size);
		put32(hdr + 24, e->size);
		put16(hdr + 28, nl);
		put32(hdr + 42, e->offset);
		put(z, hdr, 46);
		put(z, e->name, nl);
	}
	uint8_t end[22] = {0};
	put32(end, 0x06054b50);
	put16(end + 8, (uint16_t)z->n);
	put16(end + 10, (uint16_t)z->n);
	put32(end + 12, z->at - cd);
	put32(end + 16, cd);
	put(z, end, 22);
}

static char *read_whole(const char *path, int *len, unsigned max)
{
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return NULL;
	LARGE_INTEGER size;
	GetFileSizeEx(h, &size);
	if (size.QuadPart > max) {
		CloseHandle(h);
		return NULL;
	}
	char *buf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart + 1);
	DWORD got = 0;
	if (buf)
		ReadFile(h, buf, (DWORD)size.QuadPart, &got, NULL);
	CloseHandle(h);
	*len = (int)got;
	return buf;
}

// ---- the partner's log over the network

static CRITICAL_SECTION plock;
static char *pbuf;		    // the partner's log as it comes
static int ptotal = -1;		    // its length (-1: nothing yet)
static uint8_t *phave;		    // a flag for each piece
static char pid[32];		    // the report it is for

// requester: "mw2coop_report_part <id> <offset> <total> <data>"
static void on_part(const char *text, int len)
{
	char id[32];
	int off = -1, total = -1, used = 0;
	if (sscanf(text, "%31s %d %d %n", id, &off, &total, &used) < 3 || used <= 0)
		return;
	EnterCriticalSection(&plock);
	if (strcmp(id, pid) == 0 && total > 0 && total <= (int)PARTNER_LOG_MAX && off >= 0 && off < total && off % PART == 0) {
		if (ptotal < 0) {
			ptotal = total;
			pbuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total + 1);
			phave = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total / PART + 1);
		}
		const int n = len - used;
		if (pbuf && phave && total == ptotal && n > 0 && off + n <= total) {
			memcpy(pbuf + off, text + used, n);
			phave[off / PART] = 1;
		}
	}
	LeaveCriticalSection(&plock);
}

static int missing_pieces(int *list, int max)
{
	int k = 0;
	EnterCriticalSection(&plock);
	if (ptotal > 0 && phave)
		for (int i = 0; i * PART < ptotal && k < max; i++)
			if (!phave[i])
				list[k++] = i * PART;
	LeaveCriticalSection(&plock);
	return k;
}

// the partner: sends its log's tail in pieces (all, or the offsets asked again)
typedef struct {
	char id[32];
	int offsets[40];
	int n; // 0: all
} send_job_t;

static DWORD WINAPI send_thread(LPVOID arg)
{
	send_job_t *job = arg;
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%smw2coop\\mw2coop.log", dir);
	int len = 0;
	char *text = read_tail(path, PARTNER_LOG_MAX, &len);
	if (text && len > (int)PARTNER_LOG_MAX)
		len = PARTNER_LOG_MAX; // (scrubbing may have grown it)
	if (text) {
		char pkt[PART + 96];
		const int count = job->n ? job->n : (len + PART - 1) / PART;
		for (int i = 0; i < count; i++) {
			const int off = job->n ? job->offsets[i] : i * PART;
			if (off < 0 || off >= len)
				continue;
			const int n = len - off < PART ? len - off : PART;
			const int h = snprintf(pkt, sizeof(pkt), "\xff\xff\xff\xffmw2coop_report_part %s %d %d ", job->id, off, len);
			memcpy(pkt + h, text + off, n);
			net_send_mod_bin(pkt, h + n); // (net_send_mod takes about 250 bytes)
			Sleep(4); // (about 250 pieces a second: the partner's socket buffer is not flooded)
		}
		log_printf("report: sent %d piece(s) of this PC's log (%d bytes) for the partner's report %s", count, len, job->id);
		HeapFree(GetProcessHeap(), 0, text);
	}
	HeapFree(GetProcessHeap(), 0, job);
	return 0;
}

int report_on_packet(const char *buf, int len)
{
	static const char part[] = "\xff\xff\xff\xffmw2coop_report_part ", req[] = "\xff\xff\xff\xffmw2coop_report_req ", again[] = "\xff\xff\xff\xffmw2coop_report_again ";
	if (len > (int)sizeof(part) - 1 && memcmp(buf, part, sizeof(part) - 1) == 0) {
		on_part(buf + sizeof(part) - 1, len - (int)(sizeof(part) - 1));
		return 1;
	}
	const int is_req = len > (int)sizeof(req) - 1 && memcmp(buf, req, sizeof(req) - 1) == 0;
	const int is_again = len > (int)sizeof(again) - 1 && memcmp(buf, again, sizeof(again) - 1) == 0;
	if (!is_req && !is_again)
		return 0;
	char text[512];
	const int skip = is_req ? (int)sizeof(req) - 1 : (int)sizeof(again) - 1, n = len - skip < (int)sizeof(text) - 1 ? len - skip : (int)sizeof(text) - 1;
	memcpy(text, buf + skip, n);
	text[n] = 0;
	send_job_t *job = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*job));
	if (!job)
		return 1;
	char *p = text;
	if (sscanf(p, "%31s", job->id) != 1) {
		HeapFree(GetProcessHeap(), 0, job);
		return 1;
	}
	for (char *c = job->id; *c; c++)
		if (!((*c >= '0' && *c <= '9') || *c == '-'))
			*c = '_';
	if (is_again) {
		p = strchr(p, ' ');
		while (p && job->n < 40) {
			int off;
			if (sscanf(p, " %d", &off) != 1)
				break;
			job->offsets[job->n++] = off;
			p = strchr(p + 1, ' ');
		}
	}
	log_printf("report: the partner asks for this PC's log (%s%s)", job->id, is_again ? ", pieces again" : "");
	CloseHandle(CreateThread(NULL, 0, send_thread, job, 0, NULL));
	return 1;
}

// ---- making the report

static void os_version(char *out, int size)
{
	typedef LONG(WINAPI * rtl_t)(OSVERSIONINFOW *);
	OSVERSIONINFOW v;
	memset(&v, 0, sizeof(v));
	v.dwOSVersionInfoSize = sizeof(v);
	rtl_t rtl = (rtl_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
	if (rtl && rtl(&v) == 0)
		snprintf(out, size, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
	else
		lstrcpynA(out, "Windows (version unknown)", size);
}

static DWORD WINAPI make_thread(LPVOID arg)
{
	const int asked_partner = (int)(INT_PTR)arg;
	char path[MAX_PATH], shot_path[MAX_PATH], notes[2048] = "";
	snprintf(shot_path, sizeof(shot_path), "%smw2coop\\shots\\report-%s.bmp", dir, report_id);
	// the screenshot (the next frames), the partner's log (pieces, one more ask for the missing ones)
	for (int i = 0; i < 40 && GetFileAttributesA(shot_path) == INVALID_FILE_ATTRIBUTES; i++)
		Sleep(100);
	if (asked_partner) {
		DWORD start = GetTickCount();
		int asked_again = 0;
		for (;;) {
			Sleep(250);
			int list[40];
			EnterCriticalSection(&plock);
			const int have_total = ptotal;
			LeaveCriticalSection(&plock);
			const int missing = have_total > 0 ? missing_pieces(list, 40) : -1;
			if (missing == 0)
				break;
			if (GetTickCount() - start > 6000 && missing > 0 && !asked_again) {
				char text[600];
				int n = snprintf(text, sizeof(text), "\xff\xff\xff\xffmw2coop_report_again %s", report_id);
				for (int i = 0; i < missing && n < (int)sizeof(text) - 12; i++)
					n += snprintf(text + n, sizeof(text) - n, " %d", list[i]);
				net_send_mod_bin(text, n);
				asked_again = 1;
			}
			if (GetTickCount() - start > 15000)
				break;
		}
	}
	SYSTEMTIME t;
	GetLocalTime(&t);
	snprintf(path, sizeof(path), "%smw2coop\\reports", dir);
	CreateDirectoryA(path, NULL);
	snprintf(path, sizeof(path), "%smw2coop\\reports\\mw2coop-report-%s.zip", dir, report_id);
	zip_t z = {0};
	z.h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (z.h == INVALID_HANDLE_VALUE) {
		log_printf("report: %s could not be made (%lu)", path, GetLastError());
		InterlockedExchange(&busy, 0);
		return 0;
	}
	z.dos_time = (uint16_t)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
	z.dos_date = (uint16_t)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
	// README first (filled in as the parts are added)
	char file[MAX_PATH], osv[64], map[64] = "";
	os_version(osv, sizeof(osv));
	game_dvar_raw("mapname", map, sizeof(map));
	int len = 0;
	snprintf(file, sizeof(file), "%smw2coop\\mw2coop.log", dir);
	char *own = read_tail(file, OWN_LOG_MAX, &len);
	if (own) {
		zip_add(&z, "mw2coop.log", own, (uint32_t)len);
		HeapFree(GetProcessHeap(), 0, own);
	} else
		lstrcatA(notes, "- this PC's log could not be read\r\n");
	if (asked_partner) {
		EnterCriticalSection(&plock);
		if (ptotal > 0 && pbuf) {
			int missing = 0;
			for (int i = 0; i * PART < ptotal; i++)
				missing += !phave[i];
			zip_add(&z, "partner-mw2coop.log", pbuf, (uint32_t)ptotal);
			if (missing) {
				char m[96];
				snprintf(m, sizeof(m), "- the partner's log: %d piece(s) of 1000 bytes did not arrive (blank there)\r\n", missing);
				lstrcatA(notes, m);
			}
		} else
			lstrcatA(notes, "- the partner's log did not arrive (an older mod on his PC, or no connection)\r\n");
		LeaveCriticalSection(&plock);
	} else
		lstrcatA(notes, "- no partner in a network session: no partner's log\r\n");
	char *bmp = read_whole(shot_path, &len, 64u << 20);
	if (bmp) {
		zip_add(&z, "screenshot.bmp", bmp, (uint32_t)len);
		HeapFree(GetProcessHeap(), 0, bmp);
		DeleteFileA(shot_path);
	} else
		lstrcatA(notes, "- no screenshot (no frame was drawn)\r\n");
	snprintf(file, sizeof(file), "%smw2coop\\crash.dmp", dir);
	char *dmp = read_whole(file, &len, 256u << 20);
	if (dmp && !len) { // (an empty file: the crash handler started it but could not write the dump; nothing to send)
		HeapFree(GetProcessHeap(), 0, dmp);
		dmp = NULL;
		lstrcatA(notes, "- crash.dmp is empty (the last crash's dump was not written)\r\n");
	}
	if (dmp) {
		WIN32_FILE_ATTRIBUTE_DATA a;
		SYSTEMTIME ct = {0}, lt;
		if (GetFileAttributesExA(file, GetFileExInfoStandard, &a) && FileTimeToSystemTime(&a.ftLastWriteTime, &ct) && SystemTimeToTzSpecificLocalTime(NULL, &ct, &lt)) {
			char m[96];
			snprintf(m, sizeof(m), "- crash.dmp is from %04u-%02u-%02u %02u:%02u\r\n", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute);
			lstrcatA(notes, m);
		}
		zip_add(&z, "crash.dmp", dmp, (uint32_t)len);
		HeapFree(GetProcessHeap(), 0, dmp);
	}
	snprintf(file, sizeof(file), "%smw2coop\\settings.ini", dir);
	char *ini = read_tail(file, 1u << 20, &len);
	if (ini) {
		zip_add(&z, "settings.ini", ini, (uint32_t)len);
		HeapFree(GetProcessHeap(), 0, ini);
	}
	char readme[4096];
	const int rn = snprintf(readme, sizeof(readme),
				"MW2 Enhanced bug report %s\r\n\r\nmod version: %s\r\nbuild: %s\r\n%s\r\nmap: %s\r\nmade: %04u-%02u-%02u %02u:%02u:%02u\r\n\r\n"
				"Private paths are replaced: %%USERPROFILE%% (the Windows user folder), <game dir>, <user>, <pc>.\r\n\r\n%s%s",
				report_id, MW2COOP_VERSION,
#ifdef _WIN64
				"64-bit",
#else
				game_is_iw4x() ? "32-bit (iw4x-sp client)" : "32-bit",
#endif
				osv, map[0] ? map : "(none: menus)", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, notes[0] ? "Notes:\r\n" : "", notes);
	zip_add(&z, "README.txt", readme, (uint32_t)(rn < (int)sizeof(readme) ? rn : (int)sizeof(readme) - 1));
	zip_end(&z);
	CloseHandle(z.h);
	log_printf("report: written %s (%d file(s))", path + strlen(dir), z.n);
	EnterCriticalSection(&plock);
	ptotal = -1;
	if (pbuf)
		HeapFree(GetProcessHeap(), 0, pbuf);
	if (phave)
		HeapFree(GetProcessHeap(), 0, phave);
	pbuf = NULL, phave = NULL;
	pid[0] = 0;
	LeaveCriticalSection(&plock);
	char cmd[160];
	snprintf(cmd, sizeof(cmd), "mw2coop_report_done mw2coop\\reports\\mw2coop-report-%s.zip\n", report_id);
	game_cbuf_addtext(cmd);
	InterlockedExchange(&busy, 0);
	return 0;
}

// mw2coop_report: the pause menu's "Report a bug"
static void report_f(void)
{
	if (InterlockedExchange(&busy, 1)) {
		log_printf("report: one is being made already");
		return;
	}
	SYSTEMTIME t;
	GetLocalTime(&t);
	snprintf(report_id, sizeof(report_id), "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	char shot[48];
	snprintf(shot, sizeof(shot), "report-%s", report_id);
	game_cbuf_addtext("closemenu pausedmenu\n"); // (the picture of the game, not of the menu)
	shot_request(shot);
	char coop[8] = "";
	game_dvar_raw("coop", coop, sizeof(coop));
	const int ask = coop[0] == '1' && net_partner_known() && !GetEnvironmentVariableA("MW2COOP_NO_PARTNER_REPORT", NULL, 0);
	if (ask) {
		EnterCriticalSection(&plock);
		lstrcpynA(pid, report_id, sizeof(pid));
		ptotal = -1;
		LeaveCriticalSection(&plock);
		char text[96];
		snprintf(text, sizeof(text), "mw2coop_report_req %s", report_id);
		net_send_mod(text);
	}
	log_printf("report: making %s (%s)", report_id, ask ? "the partner's log is asked for" : "no partner in a network session");
	CloseHandle(CreateThread(NULL, 0, make_thread, (LPVOID)(INT_PTR)ask, 0, NULL));
}

// mw2coop_report_done <path>: the line in the game's message window (a level) and the log
static void report_done_f(void)
{
	char text[256];
	const char *fmt = localize_text("MW2COOP_REPORT_DONE_FMT");
	if (!fmt || !ctexts_fmt_ok(fmt, "s"))
		fmt = "Bug report saved: %s";
	snprintf(text, sizeof(text), fmt, game_cmd_argv(1));
	netwatch_say(text);
}

// ---- the pause menu's button: a copy of its Quit button, a row under the others ("REPORT A BUG": exec mw2coop_report)
// The pause menu comes with a level (the game's menus tick does not run while one does): report_poll queues mw2coop_report_menu every 3 s while a level runs,
// and the main thread adds the button to each copy of pausedmenu that has none (the UI's list; the cgame's list if the UI's has none).
#define ITEM_VISIBLE_EXP (ITEM_DISABLED_EXP - (int)sizeof(void *)) // (visibleExp right before disabledExp)

static int patch_pause_menu(uint8_t *menu)
{
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	const int count = *(int *)(menu + MENU_ITEM_COUNT);
	if (!items || count <= 0 || count > 256)
		return 0;
	int last = -1, quit = -1;
	float last_y = -1e9f, quit_y = -1e9f;
	float lowest_y;
	for (int i = 0; i < count; i++) {
		const char *name = *(const char **)items[i];
		if (name && !strcmp(name, "mw2coop_report_bug"))
			return 0;
		// a button: type 1 with an action and a text. The new one is a copy of Quit ('quit', else the lowest button named "...quit", else the lowest
		// button) as a row of its own two rows under the lowest row: the rows are shown by expressions and placed by expressions (a hidden row moves
		// the ones under it up), so a place right under Quit would sometimes be a gap and sometimes the airport's Skip mission.
		if (*(int *)(items[i] + ITEM_TYPE) == 1 && *(void **)(items[i] + ITEM_ACTION) && *(const char **)(items[i] + ITEM_TEXT)) {
			const float y = *(float *)(items[i] + ITEM_RECT_Y);
			if (y > last_y)
				last = i, last_y = y;
			if (name && strstr(name, "quit") && (y > quit_y || (y == quit_y && !strcmp(name, "quit"))))
				quit = i, quit_y = y;
		}
	}
	static int told;
	if (!told++) // (once: what the pause menu holds, for the button's place)
		for (int i = 0; i < count; i++) {
			const char *name = *(const char **)items[i], *text = *(const char **)(items[i] + ITEM_TEXT);
			log_printf("report: pausedmenu item %d '%s' text '%s' type %d y %.1f%s", i, name ? name : "", text && !IsBadStringPtrA(text, 64) ? text : "",
				   *(int *)(items[i] + ITEM_TYPE), *(float *)(items[i] + ITEM_RECT_Y), *(void **)(items[i] + ITEM_VISIBLE_EXP) ? " (shown by an expression)" : "");
		}
	lowest_y = last_y;
	if (quit >= 0)
		last = quit, last_y = quit_y;
	if (last < 0) {
		log_printf("report: the pause menu has no button to copy: no Report a bug button");
		return 0;
	}
	uint8_t *src = items[last];
	const float h = *(float *)(src + ITEM_RECT + 12); // (the rect's height: the next row's step)
	uint8_t **list = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(*list));
	uint8_t *item = list ? ui_clone_item(src, menu) : NULL;
	if (!item) {
		if (list)
			HeapFree(GetProcessHeap(), 0, list);
		return 0;
	}
	memcpy(list, items, count * sizeof(*list));
	*(const char **)item = "mw2coop_report_bug";
	*(const char **)(item + ITEM_TEXT) = "@MW2COOP_REPORT_BUG";
	*(void **)(item + ITEM_ACTION) = ui_script_set("\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_report\" ; ");
	*(uint32_t *)(item + ITEM_DYNAMIC_FLAGS) &= ~WINDOW_HASFOCUS;
	*(void **)(item + ITEM_TEXT_EXP) = NULL;    // (a text expression would show the copied button's text)
	*(void **)(item + ITEM_VISIBLE_EXP) = NULL; // (always shown)
	*(void **)(item + ITEM_DISABLED_EXP) = NULL;
	// Quit's place comes from its float expressions: this row keeps its own. itemDef_t: ..., typeData, int imageTrack, int floatExpressionCount,
	// floatExpressions: the count is the int right before the pointer (+0x194 / +0x13c), set first (a crash at rva 0x2549aa when only the pointer was cleared)
	*(int *)(item + ITEM_FLOAT_EXPS - 4) = 0;
	*(void **)(item + ITEM_FLOAT_EXPS) = NULL;
	const float step = h > 4 ? h : 20, y = lowest_y + 2 * step;
	*(float *)(item + ITEM_RECT_Y) = y;
	*(float *)(item + ITEM_RECT_CLIENT_Y) = y;
	list[count] = item; // (the old list stays allocated: the game's zone owns it)
	*(uint8_t ***)(menu + MENU_ITEMS) = list;
	*(int *)(menu + MENU_ITEM_COUNT) = count + 1;
	log_printf("report: the pause menu's Report a bug button added: a copy of item %d '%s' at y %.1f", last, *(const char **)src ? *(const char **)src : "", y);
	return 1;
}

void report_menu_tick(void)
{
	uint8_t *menus[8];
	int n = ui_menu_instances("pausedmenu", menus, 8);
	if (!n) {
		uint8_t *cg = native_cg_find_menu("pausedmenu");
		if (cg)
			menus[n++] = cg;
	}
	for (int i = 0; i < n; i++)
		patch_pause_menu(menus[i]);
}

static void report_menu_f(void)
{
	report_menu_tick();
}

// menus_poll's thread: while a level runs, the button is looked for every 3 s (the main thread does it: mw2coop_report_menu)
void report_poll(int in_level)
{
	static DWORD at;
	if (!in_level || GetTickCount() - at < 3000)
		return;
	at = GetTickCount();
	game_cbuf_addtext("mw2coop_report_menu\n");
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_report", NULL, NULL, report_f CMD_NODE_FLAG},
	{NULL, "mw2coop_report_done", NULL, NULL, report_done_f CMD_NODE_FLAG},
	{NULL, "mw2coop_report_menu", NULL, NULL, report_menu_f CMD_NODE_FLAG},
};

void report_install(void)
{
	InitializeCriticalSection(&plock);
	DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != '\\')
		n--;
	dir[n] = 0;
	lstrcpynA(game_dir_noslash, dir, sizeof(game_dir_noslash));
	if (n > 0)
		game_dir_noslash[n - 1] = 0;
	if (!GetEnvironmentVariableA("USERPROFILE", user_dir, sizeof(user_dir)))
		user_dir[0] = 0;
	DWORD un = sizeof(user_name);
	if (!GetUserNameA(user_name, &un))
		user_name[0] = 0;
	DWORD pn = sizeof(pc_name);
	if (!GetComputerNameA(pc_name, &pn))
		pc_name[0] = 0;
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
}
