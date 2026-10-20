// Remote control for QA: lines appended to <game dir>\mw2coop\commands.txt are run in order.
//   plain line        -> Cbuf_AddText(0, line)
//   !cmds             -> log every registered console command
//   !dvars a b c ...  -> log the current values of those dvars ("@rva:size" reads memory)
//   !watch a b c ...  -> log those dvars whenever one of them changes (checked every 250 ms)
//   !sleep <ms>       -> pause the queue
//   !xuid_add <n>     -> QA only: shift this copy's XUID (two copies on one Steam account)
//   !loc <NAME>       -> log a localized string's text and bytes
//   !trace <rva>      -> QA: log calls of the builtin method whose table entry is at rva, with GSC stacks
//   # comment
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "game.h"
#include "log.h"
#include "progress.h"
#include "scrdebug.h"

#define MAX_WATCH 24

static char watch_names[MAX_WATCH][64];
static char watch_last[MAX_WATCH][256];
static int watch_count;

static void watch_set(char *args)
{
	watch_count = 0;
	for (char *tok = strtok(args, " \t"); tok && watch_count < MAX_WATCH; tok = strtok(NULL, " \t")) {
		lstrcpynA(watch_names[watch_count], tok, sizeof(watch_names[0]));
		watch_last[watch_count][0] = 0;
		watch_count++;
	}
}

// "@<rva hex>:<1|2|4|8>" reads game memory instead of a dvar;
// "*<rva hex>+<off hex>:<size>" reads the pointer at rva, then <size> bytes at pointer+off
static const char *watch_value(const char *name, char *buf, int size)
{
	if (name[0] == '*') {
		char *end;
		unsigned long long rva = strtoull(name + 1, &end, 16);
		unsigned long long off = *end == '+' ? strtoull(end + 1, &end, 16) : 0;
		int width = *end == ':' ? atoi(end + 1) : 4;
		snprintf(buf, size, "0x%llx", game_peek_ptr(rva, off, width));
		return buf;
	}
	if (name[0] != '@')
		return game_dvar_string(name, buf, size);
	char *end;
	unsigned long long rva = strtoull(name + 1, &end, 16);
	int width = *end == ':' ? atoi(end + 1) : 4;
	unsigned long long v = game_peek(rva, width);
	snprintf(buf, size, "0x%llx", v);
	return buf;
}

// Script channel (content/scripts/maps/_mw2coop.gsc): the level scripts run on both machines
// and set these dvars identically. mw2coop_state and mw2coop_qa_log are logged on both;
// requests in mw2coop_cmd ("<what> @<time>") are carried out by the host only.
static char last_state[256], last_qa[256], last_cmd[256];

static void script_request(const char *cmd)
{
	int host = (int)game_peek(RVA_PARTY_IS_HOST, 1);
	log_printf("script: request '%s' (%s)", cmd, host ? "host, acting" : "client, host acts");
	if (strncmp(cmd, "nextmap ", 8) == 0) {
		char map[64] = "";
		sscanf(cmd + 8, "%63s", map);
		if (map[0] == '@') // last mission: no next one
			map[0] = 0;
		if (host)
			progress_save(map, "");
		// Both machines start the next mission themselves (see mw2coop_launch_map in party.c).
		// ChangeLevel() cannot: its exit handler (rva 0x170e60) skips gameMode "so", which a
		// co-op session uses.
		if (map[0]) {
			char launch[128];
			snprintf(launch, sizeof(launch), "set coop_mapName %s\nmw2coop_launch_map\n", map);
			game_cbuf_addtext(launch);
		}
		return;
	}
	if (!host)
		return;
	if (strncmp(cmd, "restart", 7) == 0) {
		game_cbuf_addtext("fast_restart\n"); // SV_MapRestart sends coopRestart to the client
	} else if (strncmp(cmd, "checkpoint ", 11) == 0) {
		char map[64] = "", start[64] = "";
		if (sscanf(cmd + 11, "%63s %63s", map, start) == 2 && start[0] != '@')
			progress_save(map, start);
	}
}

// In a co-op session GSC getdvar only returns the value of dvars whose flags contain one of
// 0x20c4 (SV_GetDvarCoop @0x230fc0); for all others it returns the default ("") so both
// lockstep machines see the same thing. Dvars made with "set" or script setdvar only carry
// 0x100 (external). These are the ones mw2coop needs scripts to read: give them 0x80 as soon
// as they exist (they keep 0x100, so script setdvar still works). The harness creates them
// before the level starts, so both machines flag them before any script reads them.
static const char *const synced_dvars[] = {"start", "mw2coop_god", "mw2coop_qa", "mw2coop_qa_phase"};

static void sync_dvar_flags(void)
{
	static int done[sizeof(synced_dvars) / sizeof(synced_dvars[0])];
	for (size_t i = 0; i < sizeof(synced_dvars) / sizeof(synced_dvars[0]); i++) {
		dvar_t *d = game_find_dvar(synced_dvars[i]);
		if (!d || (d->flags & 0x20c4))
			continue;
		d->flags |= 0x80;
		if (!done[i]++)
			log_printf("control: dvar %s made readable by co-op scripts (flags now 0x%x)", synced_dvars[i], d->flags);
	}
}

static void script_channel_poll(void)
{
	char v[256];
	sync_dvar_flags();
	if (game_dvar_raw("mw2coop_state", v, sizeof(v)) && strcmp(v, last_state) != 0) {
		log_printf("script: %s", v);
		lstrcpynA(last_state, v, sizeof(last_state));
	}
	if (game_dvar_raw("mw2coop_qa_log", v, sizeof(v)) && strcmp(v, last_qa) != 0) {
		log_printf("qa: %s", v);
		lstrcpynA(last_qa, v, sizeof(last_qa));
	}
	if (game_dvar_raw("mw2coop_cmd", v, sizeof(v)) && v[0] && strcmp(v, last_cmd) != 0) {
		lstrcpynA(last_cmd, v, sizeof(last_cmd));
		script_request(v);
	}
}

static void watch_poll(void)
{
	char now[256];
	script_channel_poll();
	for (int i = 0; i < watch_count; i++) {
		watch_value(watch_names[i], now, sizeof(now));
		if (strcmp(now, watch_last[i]) != 0) {
			log_printf("watch: %s = %s", watch_names[i], now);
			lstrcpynA(watch_last[i], now, sizeof(watch_last[i]));
		}
	}
}

static void run_line(char *line)
{
	size_t n = strlen(line);
	while (n && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' '))
		line[--n] = 0;
	if (!n || line[0] == '#')
		return;
	if (strncmp(line, "!sleep ", 7) == 0) {
		int ms = atoi(line + 7);
		log_printf("control: sleep %d ms", ms);
		for (int t = 0; t < ms; t += 250) {
			Sleep(250);
			watch_poll();
		}
		return;
	}
	if (strncmp(line, "!xuid_add ", 10) == 0) {
		// QA only: two test copies on one Steam account share an XUID, and the host ignores a
		// coopConnect from its own XUID. Shift this copy's XUID so they can pair up.
		game_xuid_add(strtoll(line + 10, NULL, 10));
		return;
	}
	if (strcmp(line, "!cmds") == 0) {
		game_log_commands();
		return;
	}
	if (strncmp(line, "!trace ", 7) == 0) {
		scrdebug_trace_method((uintptr_t)strtoull(line + 7, NULL, 16));
		return;
	}
	if (strncmp(line, "!dvars ", 7) == 0) {
		char val[256];
		for (char *tok = strtok(line + 7, " \t"); tok; tok = strtok(NULL, " \t"))
			log_printf("dvar: %s = %s", tok, watch_value(tok, val, sizeof(val)));
		return;
	}
	if (strncmp(line, "!loc ", 5) == 0) {
		// a localized string's text and bytes (ASSET_TYPE_LOCALIZE_ENTRY 27: {value, name})
		typedef void *(*find_t)(int, const char *);
		const char **entry = ((find_t)((uint8_t *)GetModuleHandleA(NULL) + RVA_DB_FIND_HEADER))(27, line + 5);
		const char *v = entry ? entry[0] : NULL;
		char hex[512] = "";
		for (int i = 0; v && v[i] && i < 80; i++)
			snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02x ", (unsigned char)v[i]);
		log_printf("loc: %s = '%s' [%s]", line + 5, v ? v : "(none)", hex);
		return;
	}
	if (strncmp(line, "!watch ", 7) == 0) {
		watch_set(line + 7);
		watch_poll();
		return;
	}
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "%s\n", line);
	log_printf("control: cbuf <- %s", line);
	game_cbuf_addtext(cmd);
}

static DWORD WINAPI control_thread(LPVOID path_arg)
{
	const char *path = (const char *)path_arg;
	LONGLONG offset = 0;
	char pending[4096];
	int plen = 0;
	for (;;) {
		Sleep(250);
		watch_poll();
		HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				       OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		LARGE_INTEGER size;
		GetFileSizeEx(f, &size);
		if (size.QuadPart < offset)
			offset = 0; // file was replaced
		LARGE_INTEGER pos = {.QuadPart = offset};
		SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
		char buf[1024];
		DWORD got;
		while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got) {
			offset += got;
			for (DWORD i = 0; i < got; i++) {
				if (buf[i] == '\n' || plen == (int)sizeof(pending) - 1) {
					pending[plen] = 0;
					run_line(pending);
					plen = 0;
				} else {
					pending[plen++] = buf[i];
				}
			}
		}
		CloseHandle(f);
	}
	return 0;
}

// A dvar a level script sets itself (GSC setdvar) has the same value on both lockstep machines,
// so scripts may read it back. Without this, getdvar reads "" in co-op for any dvar the script
// made: roadkill.gsc sets newintro 1 and then reads 0, which turns the SP-only slam-zoom intro
// back on in co-op and unlinks player 1 from the opening scene. The builtin's table entry
// ({name, func, developer}, rva 0x3c1568) is pointed at a wrapper that adds 0x80 after the set.
#define RVA_FN_SETDVAR_ENTRY 0x3c1568
#define RVA_SCR_GET_STRING   0x223440 // const char *Scr_GetString(unsigned int index)
static void (*setdvar_orig)(void);

static void setdvar_wrap(void)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	char name[64] = "";
	const char *s = ((const char *(*)(unsigned int))(base + RVA_SCR_GET_STRING))(0);
	if (s)
		lstrcpynA(name, s, sizeof(name));
	setdvar_orig();
	dvar_t *d = game_find_dvar(name);
	if (d && !(d->flags & 0x20c4))
		d->flags |= 0x80;
}

static void hook_script_setdvar(void)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *entry = base + RVA_FN_SETDVAR_ENTRY;
	if (lstrcmpA(*(const char **)entry, "setdvar") != 0) {
		log_printf("control: no setdvar entry at rva 0x%x, script dvars stay unreadable in co-op", RVA_FN_SETDVAR_ENTRY);
		return;
	}
	void (**func)(void) = (void (**)(void))(entry + 8);
	setdvar_orig = *func;
	DWORD old;
	VirtualProtect(func, sizeof(*func), PAGE_READWRITE, &old);
	*func = setdvar_wrap;
	VirtualProtect(func, sizeof(*func), old, &old);
	log_printf("control: dvars set by scripts are readable by co-op scripts");
}

void control_start(void)
{
	static char path[MAX_PATH];
	hook_script_setdvar();
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\commands.txt");
	log_printf("control: reading %s", path);
	CloseHandle(CreateThread(NULL, 0, control_thread, path, 0, NULL));
}
