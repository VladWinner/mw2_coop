// The first slice of control.c for the 32-bit build: lines appended to <game dir>\mw2coop\commands.txt are run
// in order, as in the 64-bit DLL (QA only: the file must exist at start-up).
//   plain line  -> Cbuf_AddText(0, line)
//   !sleep <ms> -> pause the queue
//   !dvars a b  -> log the current values of those dvars
//   !key ESC|ENTER|SPACE -> press that key in the game window (skips the briefing video)
//   !peek <address hex> [size] -> log game memory at that virtual address (QA, reverse engineering)
//   !pokeptr <address hex> <offset hex> <byte hex> -> QA only: write a byte at (the pointer stored at address) + offset
//   !xuid_add <n> -> QA only: shift this copy's XUID (two copies on one Steam account; the host rejects its own XUID)
//   !watch a b  -> log those dvars whenever one changes (checked every 250 ms)
//   # comment
// The level scripts' channel is read as in control.c: mw2coop_state is logged as "script: ...", mw2coop_qa_log as
// "qa: ...". The other "!" commands of control.c come over with the parts of the DLL they need.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"

// void Cbuf_AddText(int localClientNum, const char *text); cdecl, locks critical section 0x21 (thread safe),
// "Cbuf_AddText: overflow" inside. Official build FC9E4843...52D7 (docs/32bit-plan.md).

// The 32-bit dvar_t (game.h): type byte +0x0c, current value +0x10.
// The current value as text; 0 when the dvar does not exist.
static int dvar_text(const char *name, char *buf, int size)
{
	const dvar_t *d = game_find_dvar(name);
	if (!d)
		return 0;
	const uint8_t *value = (const uint8_t *)&d->current;
	switch (d->type) {
	case DVAR_TYPE_BOOL:
		snprintf(buf, size, "%d", *value);
		break;
	case DVAR_TYPE_FLOAT:
		snprintf(buf, size, "%g", *(const float *)value);
		break;
	case DVAR_TYPE_INT:
	case DVAR_TYPE_ENUM:
		snprintf(buf, size, "%d", *(const int32_t *)value);
		break;
	case DVAR_TYPE_STRING: {
		const char *s = *(const char *const *)value;
		snprintf(buf, size, "%s", s ? s : "");
		break;
	}
	default:
		snprintf(buf, size, "<type %d>", d->type);
	}
	return 1;
}

// Console command mw2coop_launch_map (as party.c in the 64-bit DLL): starts "map <coop_mapName>" the way the game's own
// co-op launch does (void (int localClient) at 0x47bd80: slot state 3, then the map command), without the check that two
// members are in the lobby. Cmd_AddCommand is cdecl (name, function, storage for the command node, 0) at 0x428090.

static void launch_map_f(void)
{
	char map[64] = "";
	if (!dvar_text("coop_mapName", map, sizeof(map)) || !map[0]) {
		log_printf("control: mw2coop_launch_map: coop_mapName is empty");
		return;
	}
	log_printf("control: launching '%s'", map);
	((void (*)(int))((uint8_t *)GetModuleHandleW(NULL) + RVA_COOP_LAUNCH_MAP))(0);
}

// mw2coop_join <ip> [port]: join a host by address without a Steam lobby (as party.c in the 64-bit DLL). The game's
// own client party join, PartyClient_Join at 0x42fc40 (cdecl: local client, host info, out ip, out port): Party_Reset,
// Lobby_Join, the address translation, Party_Activate. The host info is the one the Steam lobby would give: +0 xuid,
// +8 internal IPv4, +0xc external IPv4, +0x10 internal port, +0x12 external port (host order). Then "connect ip port"
// with the port byte-swapped (connect stores it as is into a network-order field).
#define RVA_CBUF_ADDTEXT_JOIN RVA_CBUF_ADDTEXT

static void join_f(void)
{
	unsigned a, b, c, d;
	char tail;
	const char *ip = game_cmd_argv(1), *port_text = game_cmd_argv(2);
	log_printf("party: mw2coop_join '%s' '%s'", ip, port_text);
	if (sscanf(ip, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
		log_printf("party: usage: mw2coop_join <ip> [port]");
		return;
	}
	unsigned port = port_text[0] ? (unsigned)atoi(port_text) : 28960;
	if (port == 0 || port > 65535) {
		log_printf("party: bad port '%s'", port_text);
		return;
	}
	uint8_t info[64] = {0};
	uint8_t addr[4] = {(uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d};
	uint16_t p = (uint16_t)port;
	memcpy(info + 8, addr, 4);
	memcpy(info + 0xc, addr, 4);
	memcpy(info + 0x10, &p, 2);
	memcpy(info + 0x12, &p, 2);
	uint32_t out_ip = 0;
	uint16_t out_port = 0;
	uint8_t *base = (uint8_t *)GetModuleHandleW(NULL);
	char ok = ((char (*)(int, void *, uint32_t *, uint16_t *))(base + RVA_PARTY_JOIN))(0, info, &out_ip, &out_port);
	log_printf("party: client party joined (%d, host %u.%u.%u.%u:%u, active %d, host flag %d)", ok, a, b, c, d, port,
		   base[0x534449], base[0x534448]);
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "connect %u.%u.%u.%u %u\n", a, b, c, d, (unsigned)(uint16_t)((p >> 8) | (p << 8)));
	((void (*)(int, const char *))(base + RVA_CBUF_ADDTEXT_JOIN))(0, cmd);
}

static void add_commands(void)
{
	static uint8_t node[2][32];
	uint8_t *base = (uint8_t *)GetModuleHandleW(NULL);
	void (*add)(const char *, void (*)(void), void *, int) = (void (*)(const char *, void (*)(void), void *, int))(base + RVA_CMD_ADDCOMMAND);
	add("mw2coop_launch_map", launch_map_f, node[0], 0);
	add("mw2coop_join", join_f, node[1], 0);
}

static BOOL CALLBACK find_game_window(HWND h, LPARAM out)
{
	DWORD pid;
	char cls[32];
	GetWindowThreadProcessId(h, &pid);
	if (pid == GetCurrentProcessId() && GetClassNameA(h, cls, sizeof(cls)) && lstrcmpA(cls, "IW4") == 0) {
		*(HWND *)out = h;
		return FALSE;
	}
	return TRUE;
}

static void press_key(const char *name)
{
	WPARAM vk = strcmp(name, "ESC") == 0 ? VK_ESCAPE : strcmp(name, "ENTER") == 0 ? VK_RETURN : strcmp(name, "SPACE") == 0 ? VK_SPACE : 0;
	HWND game = NULL;
	EnumWindows(find_game_window, (LPARAM)&game);
	if (!vk || !game) {
		log_printf("control: !key %s: %s", name, vk ? "no game window" : "unknown key");
		return;
	}
	UINT scan = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
	PostMessageA(game, WM_KEYDOWN, vk, 1 | (scan << 16));
	Sleep(80);
	PostMessageA(game, WM_KEYUP, vk, 1 | (scan << 16) | (3u << 30));
	log_printf("control: key %s", name);
}

#define MAX_WATCH 24
static char watch_names[MAX_WATCH][64];
static char watch_last[MAX_WATCH][256];
static int watch_count;
static char last_state[256], last_qa[256];

// In a co-op session GSC getdvar only returns dvars whose flags (dvar +8) contain one of 0x20c4; for all others it returns
// "" so both lockstep machines see the same. The dvars the mod's scripts read get 0x80 as soon as they exist (see
// sync_dvar_flags in control.c, the same list and flag).
static const char *const synced_dvars[] = {"start", "mw2coop_god", "mw2coop_skill", "mw2coop_ff", "mw2coop_cp_start", "mw2coop_cp_pos",
					   "mw2coop_qa", "mw2coop_qa_phase"};

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

// The scripts' requests (maps\_mw2coop.gsc request(): mw2coop_cmd "<what> @<time>#<n>"), as script_request in control.c:
// the next mission starts on both machines (each runs the launch itself), a restart is the host's fast_restart (the
// game sends the client coopRestart), a checkpoint is progress the host would save (progress.c is not ported yet).
static char last_cmd[256];

static void script_request(const char *cmd)
{
	uint8_t *base = (uint8_t *)GetModuleHandleW(NULL);
	int host = base[0x534448]; // the party's host flag (listen sets it, a client's join clears it)
	log_printf("script: request '%s' (%s)", cmd, host ? "host, acting" : "client, host acts");
	if (strncmp(cmd, "nextmap ", 8) == 0) {
		char map[64] = "", text[128];
		sscanf(cmd + 8, "%63s", map);
		if (map[0] == '@') // last mission: no next one
			map[0] = 0;
		if (map[0]) {
			snprintf(text, sizeof(text), "set coop_mapName %s\nmw2coop_launch_map\n", map);
			((void (*)(int, const char *))(base + RVA_CBUF_ADDTEXT))(0, text);
		}
		return;
	}
	if (!host)
		return;
	if (strncmp(cmd, "restart", 7) == 0)
		((void (*)(int, const char *))(base + RVA_CBUF_ADDTEXT))(0, "fast_restart\n");
	else if (strncmp(cmd, "checkpoint ", 11) == 0)
		log_printf("script: checkpoint not saved (progress.c is not ported to the 32-bit build yet)");
}

static void poll(void)
{
	char v[256];
	sync_dvar_flags();
	if (dvar_text("mw2coop_cmd", v, sizeof(v)) && v[0] && strcmp(v, last_cmd) != 0) {
		lstrcpynA(last_cmd, v, sizeof(last_cmd));
		script_request(v);
	}
	if (dvar_text("mw2coop_state", v, sizeof(v)) && strcmp(v, last_state) != 0) {
		log_printf("script: %s", v);
		lstrcpynA(last_state, v, sizeof(last_state));
	}
	if (dvar_text("mw2coop_qa_log", v, sizeof(v)) && strcmp(v, last_qa) != 0) {
		log_printf("qa: %s", v);
		lstrcpynA(last_qa, v, sizeof(last_qa));
	}
	for (int i = 0; i < watch_count; i++) {
		if (!dvar_text(watch_names[i], v, sizeof(v)))
			snprintf(v, sizeof(v), "<missing>");
		if (strcmp(v, watch_last[i]) != 0) {
			log_printf("watch: %s = %s", watch_names[i], v);
			lstrcpynA(watch_last[i], v, sizeof(watch_last[i]));
		}
	}
}

static void run_line(char *line)
{
	while (*line == ' ' || *line == '\t')
		line++;
	if (!*line || *line == '#')
		return;
	if (*line == '!') {
		if (strncmp(line, "!sleep ", 7) == 0) {
			int ms = atoi(line + 7);
			log_printf("control: sleep %d ms", ms);
			for (int t = 0; t < ms; t += 250) {
				Sleep(250);
				poll();
			}
		} else if (strncmp(line, "!peek ", 6) == 0) {
			char *end;
			unsigned long addr = strtoul(line + 6, &end, 16);
			int n = atoi(end);
			char hex[160] = "";
			if (n <= 0 || n > 32)
				n = 4;
			if (IsBadReadPtr((void *)addr, n))
				snprintf(hex, sizeof(hex), "unreadable");
			for (int i = 0; i < n && hex[0] != 'u'; i++)
				snprintf(hex + 3 * i, 4, "%02x ", ((const uint8_t *)addr)[i]);
			log_printf("peek: %08lx = %s", addr, hex);
		} else if (strncmp(line, "!pokeptr ", 9) == 0) {
			char *end;
			unsigned long at = strtoul(line + 9, &end, 16);
			unsigned long off = strtoul(end, &end, 16);
			unsigned long val = strtoul(end, &end, 16);
			uint8_t *obj = IsBadReadPtr((void *)at, 4) ? NULL : *(uint8_t **)at;
			if (!obj || IsBadWritePtr(obj + off, 1)) {
				log_printf("control: !pokeptr %08lx: no object there", at);
			} else {
				obj[off] = (uint8_t)val;
				log_printf("control: poked %02lx at *%08lx+%lx", val, at, off);
			}
		} else if (strncmp(line, "!xuid_add ", 10) == 0) {
			// uint64 XUID of local client 0 at 0x19fbf88 (stride 0x68 per local client; read by 0x46f800)
			uint64_t *xuid = (uint64_t *)((uint8_t *)GetModuleHandleW(NULL) + 0x15fbf88);
			uint64_t before = *xuid;
			*xuid = before + (uint64_t)strtoll(line + 10, NULL, 10);
			log_printf("control: xuid %016llx -> %016llx", (unsigned long long)before, (unsigned long long)*xuid);
		} else if (strncmp(line, "!key ", 5) == 0) {
			press_key(line + 5);
		} else if (strncmp(line, "!watch ", 7) == 0) {
			watch_count = 0;
			for (char *tok = strtok(line + 7, " 	"); tok && watch_count < MAX_WATCH; tok = strtok(NULL, " 	")) {
				lstrcpynA(watch_names[watch_count], tok, sizeof(watch_names[0]));
				watch_last[watch_count++][0] = 0;
			}
		} else if (strncmp(line, "!dvars ", 7) == 0) {
			char v[256];
			for (char *tok = strtok(line + 7, " 	"); tok; tok = strtok(NULL, " 	"))
				log_printf("dvar: %s = %s", tok, dvar_text(tok, v, sizeof(v)) ? v : "<missing>");
		} else
			log_printf("control: command %s is not ported to the 32-bit build yet", line);
		return;
	}
	char text[1024];
	snprintf(text, sizeof(text), "%s\n", line);
	log_printf("control: > %s", line);
	((void (*)(int, const char *))((uint8_t *)GetModuleHandleW(NULL) + RVA_CBUF_ADDTEXT))(0, text);
}

static DWORD WINAPI control_thread(LPVOID path_arg)
{
	const char *path = path_arg;
	LONGLONG offset = 0;
	char carry[1024];
	int carried = 0;
	for (;;) {
		Sleep(250);
		poll();
		HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		LARGE_INTEGER size;
		GetFileSizeEx(f, &size);
		if (size.QuadPart < offset) {
			offset = 0; // file was replaced
			carried = 0;
		}
		if (size.QuadPart > offset) {
			char buf[4096];
			DWORD got = 0;
			LARGE_INTEGER pos = {.QuadPart = offset};
			SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
			if (ReadFile(f, buf, sizeof(buf), &got, NULL) && got > 0) {
				offset += got;
				for (DWORD i = 0; i < got; i++) {
					if (buf[i] == '\n') {
						carry[carried] = 0;
						carried = 0;
						run_line(carry);
					} else if (buf[i] != '\r' && carried < (int)sizeof(carry) - 1) {
						carry[carried++] = buf[i];
					}
				}
			}
		}
		CloseHandle(f);
	}
	return 0;
}

void control32_start(void)
{
	static char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\commands.txt");
	if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		log_printf("control: no %s at start-up, the command file is not read", path);
		return;
	}
	log_printf("control: reading %s (QA)", path);
	add_commands();
	CloseHandle(CreateThread(NULL, 0, control_thread, path, 0, NULL));
}
