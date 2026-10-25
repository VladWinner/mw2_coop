// The first slice of control.c for the 32-bit build: lines appended to <game dir>\mw2coop\commands.txt are run
// in order, as in the 64-bit DLL (QA only: the file must exist at start-up).
//   plain line  -> Cbuf_AddText(0, line)
//   !sleep <ms> -> pause the queue
//   !dvars a b  -> log the current values of those dvars
//   !key ESC|ENTER|SPACE -> press that key in the game window (skips the briefing video)
//   !peek <address hex> [size] -> log game memory at that virtual address (QA, reverse engineering)
//   !pokeptr <address hex> <offset hex> <byte hex> -> QA only: write a byte at (the pointer stored at address) + offset
//   !watch a b  -> log those dvars whenever one changes (checked every 250 ms)
//   # comment
// The level scripts' channel is read as in control.c: mw2coop_state is logged as "script: ...", mw2coop_qa_log as
// "qa: ...". The other "!" commands of control.c come over with the parts of the DLL they need.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

// void Cbuf_AddText(int localClientNum, const char *text); cdecl, locks critical section 0x21 (thread safe),
// "Cbuf_AddText: overflow" inside. Official build FC9E4843...52D7 (docs/32bit-plan.md).
#define RVA_CBUF_ADDTEXT 0x9b40

// dvar_t *Dvar_FindVar(name) at 0x633480: USERCALL, name in EDI, result in EAX; it takes the dvar lock itself.
// 32-bit dvar_t (read from Dvar_GetString 0x4e7470 and the value formatter 0x4c5ae0): name +0x00, type byte +0x0c,
// current value +0x10 (16 bytes), next in the hash chain +0x4c.
#define RVA_DVAR_FINDVAR 0x233480
enum { DVAR_BOOL = 0, DVAR_FLOAT = 1, DVAR_INT = 5, DVAR_ENUM = 6, DVAR_STRING = 7 };

static const uint8_t *find_dvar(const char *name)
{
	const uint8_t *dvar;
	const char *arg = name;
	__asm__ volatile("call *%[fn]" : "=a"(dvar), "+D"(arg) : [fn] "r"((uint8_t *)GetModuleHandleW(NULL) + RVA_DVAR_FINDVAR)
			 : "ecx", "edx", "memory", "cc");
	return dvar;
}

// The current value as text; 0 when the dvar does not exist.
static int dvar_text(const char *name, char *buf, int size)
{
	const uint8_t *d = find_dvar(name);
	if (!d)
		return 0;
	const uint8_t *value = d + 0x10;
	switch (d[0x0c]) {
	case DVAR_BOOL:
		snprintf(buf, size, "%d", *value);
		break;
	case DVAR_FLOAT:
		snprintf(buf, size, "%g", *(const float *)value);
		break;
	case DVAR_INT:
	case DVAR_ENUM:
		snprintf(buf, size, "%d", *(const int32_t *)value);
		break;
	case DVAR_STRING: {
		const char *s = *(const char *const *)value;
		snprintf(buf, size, "%s", s ? s : "");
		break;
	}
	default:
		snprintf(buf, size, "<type %d>", d[0x0c]);
	}
	return 1;
}

// Console command mw2coop_launch_map (as party.c in the 64-bit DLL): starts "map <coop_mapName>" the way the game's own
// co-op launch does (void (int localClient) at 0x47bd80: slot state 3, then the map command), without the check that two
// members are in the lobby. Cmd_AddCommand is cdecl (name, function, storage for the command node, 0) at 0x428090.
#define RVA_COOP_LAUNCH_MAP 0x7bd80
#define RVA_CMD_ADDCOMMAND  0x28090

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

static void add_commands(void)
{
	static uint8_t node[32];
	((void (*)(const char *, void (*)(void), void *, int))((uint8_t *)GetModuleHandleW(NULL) + RVA_CMD_ADDCOMMAND))("mw2coop_launch_map", launch_map_f, node, 0);
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

static void poll(void)
{
	char v[256];
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
