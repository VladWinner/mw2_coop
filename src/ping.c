// See ping.h.
//
// The key ([input] ping_key, Z: bound to +mw2coop_ping) and a pad's Back work alike:
// - a short press (let go within 250 ms) is a "tap": the script marks what the player looks at (an enemy, a vehicle, a weapon, a grenade, ... or the place);
// - held 250 ms, the wheel opens: the script shows eight kinds of marker round the cross hair (enemy, attacking, going here, wait here, danger, fall back, watching,
//   covering); the mouse (or the pad's right stick) picks one, and the view stays where it was meanwhile (the mouse's movement goes to the wheel, not to the view:
//   ping_mouse, from the mouse read of CL_MouseMove, src/native.c); letting go places the marker picked, or none when the cursor is back in the middle.
//
// How the events reach the script on both PCs the same way:
// - network co-op: the client command "mr -1 mw2coop_ping[_<verb>_<n>]", as a menu sends its answer (the game's own "cmd mr %i %s", rva 0x354d98). A client's
//   commands travel in the lockstep frames and run on both PCs in the same frame (the frame executor's records -> ClientCommand 0x166710 -> "mr" 0x165ca0: an index
//   that is no menu's (-1) is kept as text, and the player's entity gets the notify "menuresponse" ("-1", "mw2coop_ping_sel_3")). So both PCs' scripts make the same
//   marker in the same frame, and the games do not part (a marker made on one PC only would be a difference of the script's state). Only what the player decided goes
//   this way (the tap, the wheel opened, a sector picked, let go); the mouse's movement stays on his PC.
// - native split screen: one process, both players local: the dvar mw2coop_ping_q<player> holds that player's last events ("<n> <verb> <arg>,..."), which the
//   script reads each frame and takes the ones it has not seen (several can come in one frame: a sector picked and let go).
// - single player: nothing (nobody to tell).
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "native.h"
#include "ping.h"
#include "settings.h"

#define HOLD_MS 250 // held this long: the wheel
#define DEAD 40.0f  // the mouse's way (counts) from the middle before a sector is picked
#define REACH 120.0f // the mouse's way is held within this (so a turn to another sector is short)
#define QUEUE 6     // events the split screen's dvar keeps per player

enum { V_TAP, V_OPEN, V_SEL, V_CLOSE };
static const char *const verb_name[] = {"tap", "open", "sel", "close"};

typedef struct {
	int down;     // the key (or Back) is held
	int pad;      // held on a pad: its right stick picks; else the mouse
	int open;     // the wheel is shown
	DWORD at;     // when it went down
	DWORD tick;   // the last frame that saw it held
	float cx, cy; // the mouse's way since the wheel opened (counts; y down)
	int sector;   // -1: none (the middle), 0..WHEEL_SECTORS-1 clockwise from the top
} wheel_t;
static wheel_t wheel[2];
static LONG seq;
static char queue[2][QUEUE][32];
static float qa_dx[2], qa_dy[2]; // QA (mw2coop_ping_qa <player> move <dx> <dy>): mouse movement for the wheel without a real mouse

static int in_level(void)
{
	char v[8];
	if (!game_dvar_raw("sv_running", v, sizeof(v)) || strcmp(v, "1") != 0)
		return 0; // (no level: a menu)
	return native_split_active() || (game_dvar_raw("coop", v, sizeof(v)) && !strcmp(v, "1"));
}

static void ping_send(int player, int verb, int arg)
{
	char cmd[256];
	if (native_split_active()) {
		memmove(queue[player][0], queue[player][1], sizeof(queue[player][0]) * (QUEUE - 1));
		snprintf(queue[player][QUEUE - 1], sizeof(queue[player][0]), "%ld %s %d", (long)InterlockedIncrement(&seq), verb_name[verb], arg);
		int n = snprintf(cmd, sizeof(cmd), "set mw2coop_ping_q%d \"", player), first = 1;
		for (int i = 0; i < QUEUE; i++)
			if (queue[player][i][0]) {
				n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, "%s%s", first ? "" : ",", queue[player][i]);
				first = 0;
			}
		snprintf(cmd + n, sizeof(cmd) - (size_t)n, "\"\n");
	} else if (verb == V_TAP) {
		snprintf(cmd, sizeof(cmd), "cmd mr -1 mw2coop_ping\n");
	} else if (verb == V_OPEN) {
		snprintf(cmd, sizeof(cmd), "cmd mr -1 mw2coop_ping_open\n");
	} else if (arg < 0) {
		snprintf(cmd, sizeof(cmd), "cmd mr -1 mw2coop_ping_%s_x\n", verb_name[verb]);
	} else {
		snprintf(cmd, sizeof(cmd), "cmd mr -1 mw2coop_ping_%s_%d\n", verb_name[verb], arg);
	}
	game_cbuf_addtext(cmd);
	static int logged;
	if (logged < 400) {
		logged++;
		log_printf("ping: player %d %s %d", player + 1, verb_name[verb], arg);
	}
}

static void press(int player, int pad)
{
	if (player < 0 || player > 1 || !in_level())
		return;
	wheel_t *w = &wheel[player];
	if (w->down)
		return;
	memset(w, 0, sizeof(*w));
	w->down = 1;
	w->pad = pad;
	w->at = w->tick = GetTickCount();
	w->sector = -1;
}

static void release(int player)
{
	if (player < 0 || player > 1)
		return;
	wheel_t *w = &wheel[player];
	if (!w->down)
		return;
	w->down = 0;
	if (w->open) {
		w->open = 0;
		ping_send(player, V_CLOSE, w->sector);
	} else if (in_level()) {
		ping_send(player, V_TAP, 0);
	}
}

// each frame while it is held (from the mouse read, or the pad's frame): opens the wheel after HOLD_MS. A frame that did not come for 300 ms (a menu, the console,
// a load) drops the press: its release may have gone to the menu, and the view must not stay held.
static void tick(int player)
{
	wheel_t *w = &wheel[player];
	const DWORD now = GetTickCount();
	if (now - w->tick > 300) {
		if (w->open)
			ping_send(player, V_CLOSE, -1);
		w->down = w->open = 0;
		return;
	}
	w->tick = now;
	if (!w->open && now - w->at >= HOLD_MS) {
		w->open = 1;
		w->cx = w->cy = 0;
		w->sector = -1;
		ping_send(player, V_OPEN, 0);
	}
}

// 0..WHEEL_SECTORS-1 clockwise from the top for a direction (x right, y up); maps\_mw2coop_ping.gsc draws the same number (wheel_count)
#define WHEEL_SECTORS 10 // (8 until 2026-11-23: "come to me" and "need ammo" were added)
static int sector_of(float x, float y)
{
	const float step = 360.0f / WHEEL_SECTORS;
	float a = atan2f(x, y) * 57.29578f; // 0 up, 90 right
	int s = (int)floorf((a + step / 2) / step);
	return (s % WHEEL_SECTORS + WHEEL_SECTORS) % WHEEL_SECTORS;
}

static void pick(int player, int s)
{
	wheel_t *w = &wheel[player];
	if (s != w->sector) {
		w->sector = s;
		ping_send(player, V_SEL, s);
	}
}

int ping_mouse(int player, float *x, float *y)
{
	if (player < 0 || player > 1)
		return 0;
	wheel_t *w = &wheel[player];
	*x += qa_dx[player]; // (QA: as if the mouse had moved: it turns the view unless the wheel is open)
	*y += qa_dy[player];
	qa_dx[player] = qa_dy[player] = 0;
	if (!w->down || w->pad)
		return 0;
	tick(player);
	if (!w->open)
		return 0;
	w->cx += *x;
	w->cy += *y;
	const float len = sqrtf(w->cx * w->cx + w->cy * w->cy);
	if (len > REACH) {
		w->cx *= REACH / len;
		w->cy *= REACH / len;
	}
	pick(player, len < DEAD ? -1 : sector_of(w->cx, -w->cy));
	*x = *y = 0;
	return 1;
}

int ping_pad(int player, int back, float rx, float ry)
{
	if (player < 0 || player > 1)
		return 0;
	wheel_t *w = &wheel[player];
	if (back && !w->down)
		press(player, 1);
	else if (!back && w->down && w->pad)
		release(player);
	if (!w->down || !w->pad)
		return 0;
	tick(player);
	if (!w->open)
		return 0;
	if (rx * rx + ry * ry > 0.25f) // the stick pushed half way: its direction; let go to the middle, the sector stays
		pick(player, sector_of(rx, ry));
	return 1;
}

static int kb_player(void)
{
	return native_split_active() ? native_kb_player() : 0;
}

static void down_f(void)
{
	press(kb_player(), 0);
}

static void up_f(void)
{
	const int p = kb_player();
	if (!wheel[p].pad)
		release(p);
}

// the old command (a tap at once): a console line, a key bound before the wheel came
static void ping_f(void)
{
	if (in_level())
		ping_send(kb_player(), V_TAP, 0);
}

// QA: mw2coop_ping_qa <player 0|1> down | up | move <dx> <dy>
static void qa_f(void)
{
	const int p = atoi(game_cmd_argv(1));
	const char *what = game_cmd_argv(2);
	if (p < 0 || p > 1)
		return;
	if (!strcmp(what, "down"))
		press(p, 0);
	else if (!strcmp(what, "up"))
		release(p);
	else if (!strcmp(what, "move")) {
		qa_dx[p] += (float)atof(game_cmd_argv(3));
		qa_dy[p] += (float)atof(game_cmd_argv(4));
	}
	log_printf("ping: qa player %d %s %s %s (held %d, wheel %d, sector %d)", p + 1, what, game_cmd_argv(3), game_cmd_argv(4), wheel[p].down, wheel[p].open, wheel[p].sector);
}

static cmd_function_t commands[] = {
    {NULL, "+mw2coop_ping", NULL, NULL, down_f CMD_NODE_FLAG},
    {NULL, "-mw2coop_ping", NULL, NULL, up_f CMD_NODE_FLAG},
    {NULL, "mw2coop_ping", NULL, NULL, ping_f CMD_NODE_FLAG},
    {NULL, "mw2coop_ping_qa", NULL, NULL, qa_f CMD_NODE_FLAG},
};

void ping_install(void)
{
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
}

static int key_ok(const char *k)
{
	if (!k[0] || strlen(k) > 15)
		return 0;
	for (; *k; k++)
		if (!((*k >= 'A' && *k <= 'Z') || (*k >= 'a' && *k <= 'z') || (*k >= '0' && *k <= '9') || *k == '_'))
			return 0;
	return 1;
}

static int contains_ci(const char *text, const char *word)
{
	const size_t n = strlen(word);
	for (; *text; text++)
		if (_strnicmp(text, word, n) == 0)
			return 1;
	return 0;
}

// the config's bind lines: whether one binds `key` ("bind Z ..." / "bind z ...", case blind); `old_key` gets the key of a line bound to the old command
// (mw2coop_ping without the '+': a tap only, from before the wheel)
static int config_binds(const char *text, const char *key, char *old_key, size_t old_size)
{
	const size_t kn = strlen(key);
	int found = 0;
	for (const char *line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
		while (*line == ' ' || *line == '\t')
			line++;
		if (_strnicmp(line, "bind ", 5) != 0)
			continue;
		const char *k = line + 5;
		while (*k == ' ')
			k++;
		const char *ke = k;
		while (*ke && *ke != ' ' && *ke != '\t' && *ke != '\r' && *ke != '\n')
			ke++;
		const char *end = strchr(line, '\n');
		const size_t len = end ? (size_t)(end - line) : strlen(line);
		char l[160];
		snprintf(l, sizeof(l), "%.*s", (int)(len < sizeof(l) - 1 ? len : sizeof(l) - 1), line);
		if (old_key && contains_ci(l, "mw2coop_ping") && !contains_ci(l, "+mw2coop_ping") && (size_t)(ke - k) < old_size)
			snprintf(old_key, old_size, "%.*s", (int)(ke - k), k);
		if ((size_t)(ke - k) == kn && _strnicmp(k, key, kn) == 0)
			found = 1;
	}
	return found;
}

void ping_menu_tick(void)
{
	static int done;
	if (done)
		return;
	done = 1;
	char key[16] = "";
	GetPrivateProfileStringA("input", "ping_key", "Z", key, sizeof(key), settings_ini_path());
	if (!key[0]) {
		log_printf("ping: [input] ping_key is empty: the mod binds no key to the markers (bind one to +mw2coop_ping)");
		return;
	}
	if (!key_ok(key)) {
		log_printf("ping: [input] ping_key '%s' is not a key name: no key bound", key);
		return;
	}
	// the player's config (players\config.cfg next to the game): the game wrote it from what was bound when it last closed
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "players\\config.cfg");
	char *text = NULL;
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD size = GetFileSize(h, NULL), got = 0;
		if (size != INVALID_FILE_SIZE && size < (1u << 20) && (text = malloc((size_t)size + 1)) != NULL) {
			if (!ReadFile(h, text, size, &got, NULL))
				got = 0;
			text[got] = 0;
		}
		CloseHandle(h);
	}
	char old_key[16] = "", cmd[64];
	const int key_used = text ? config_binds(text, key, old_key, sizeof(old_key)) : 0;
	if (old_key[0] && key_ok(old_key)) { // bound to the tap-only command (a build before the wheel): the same key now holds for the wheel too
		snprintf(cmd, sizeof(cmd), "bind %s +mw2coop_ping\n", old_key);
		game_cbuf_addtext(cmd);
		log_printf("ping: %s marks for the partner, held it opens the wheel (bind %s +mw2coop_ping; it was bound to the tap only)", old_key, old_key);
	} else if (text && contains_ci(text, "+mw2coop_ping")) {
		log_printf("ping: a key is bound to the markers already (config.cfg)");
	} else if (key_used) {
		log_printf("ping: %s is bound to something else: no key bound to the markers (bind one to +mw2coop_ping, or set [input] ping_key)", key);
	} else {
		snprintf(cmd, sizeof(cmd), "bind %s +mw2coop_ping\n", key);
		game_cbuf_addtext(cmd);
		log_printf("ping: %s marks for the partner, held it opens the wheel (bind %s +mw2coop_ping)", key, key);
	}
	free(text);
}
