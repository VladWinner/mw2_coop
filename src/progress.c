// Campaign progress that outlives the game: the host keeps the mission and the checkpoint
// (level start point) reached last in <game dir>\mw2coop\progress.txt:
//   map trainer
//   start timed_ads
// Level scripts report progress through the request channel (control.c), which the host acts
// on. The game itself saves nothing in a co-op session, and mw2coop turns its autosaves off
// (each PC would load its own save and break lockstep).
//
// mw2coop_continue (host console): sets coop_mapName and start from the file; then listen and
// coopLaunch as usual.
//
// The client must use the same start point, or the two lockstep machines run different level
// scripts. When the host sends coopLaunch, net.c first sends the client an out-of-band packet
// "mw2coop_start start=<start> mw2coop_god=<0|1|> mw2coop_skill=<0..3>" (progress_launch_packet);
// the client sets those dvars from it before the level loads (progress_on_packet). mw2coop_god
// is the test god mode (_mw2coop.gsc); mw2coop_skill is the difficulty the host picked. Both must
// match on the two machines.
//
// The difficulty: level scripts read each player's from g_gameskill_player_<n>
// (GetPlayerSetting "gameskill", rva 0x18de0a: the dvar's current integer) and play the higher
// of the two. g_gameskill and g_gameskill_player_1/2 are latched dvars (flag 2): the difficulty
// popup's setdvar only sets the latched value, which devmap applies when it loads the map. A
// co-op session starts with coopLaunch (the lobby's devmap is ours, menus.c), so the pick never
// applied: the host played the difficulty his profile loaded at startup and the client his own.
// The console cannot write them either (flag 0x2000 on g_gameskill: set, setdvar and seta change
// nothing). Each popup button also runs "profile_difficultySave <n>", a console command: its
// argument is the host's pick. Hooked here; the host applies the pick (or, with no pick this
// session, the difficulty it plays) to all three dvars and to the party members' difficulty in
// memory. The level start sets g_gameskill_player_<n> again from the party (on the client after
// our packet), so the pick also goes to both machines as mw2coop_skill, and _gameskill.gsc's
// updateGameSkill() takes each player's difficulty from it in co-op
// (maps\_mw2coop::player_gameskill).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "progress.h"

static char path[MAX_PATH];

void progress_save(const char *map, const char *start)
{
	FILE *f = fopen(path, "w");
	if (!f) {
		log_printf("progress: cannot write %s", path);
		return;
	}
	fprintf(f, "map %s\nstart %s\n", map, start);
	fclose(f);
	log_printf("progress: saved map '%s' start '%s'", map, start);
}

static int progress_load(char *map, int map_size, char *start, int start_size)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	char line[128];
	map[0] = start[0] = 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (strncmp(line, "map ", 4) == 0)
			lstrcpynA(map, line + 4, map_size);
		else if (strncmp(line, "start ", 6) == 0)
			lstrcpynA(start, line + 6, start_size);
	}
	fclose(f);
	return map[0] != 0;
}

// Map and start point names are plain identifiers; anything else in the file is ignored.
static int is_name(const char *s)
{
	for (; *s; s++)
		if (!(*s == '_' || (*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')))
			return 0;
	return 1;
}

int progress_last(char *map, int map_size, char *start, int start_size)
{
	char m[64], st[64];
	if (!progress_load(m, sizeof(m), st, sizeof(st)) || !is_name(m) || !is_name(st))
		return 0;
	lstrcpynA(map, m, map_size);
	lstrcpynA(start, st, start_size);
	return 1;
}

static void continue_f(void)
{
	char map[64], start[64], cmd[192];
	if (!progress_load(map, sizeof(map), start, sizeof(start)) || !is_name(map) || !is_name(start)) {
		log_printf("progress: nothing to continue (%s)", path);
		return;
	}
	snprintf(cmd, sizeof(cmd), "set coop_mapName %s\nset start \"%s\"\n", map, start);
	game_cbuf_addtext(cmd);
	log_printf("progress: continue at map '%s' start '%s'", map, start);
}

// Host dvars the client must match before the level loads (level scripts read them).
static const char *const session_dvars[] = {"start", "mw2coop_god", "mw2coop_skill", "mw2coop_ff"};
#define SESSION_DVARS (int)(sizeof(session_dvars) / sizeof(session_dvars[0]))

static const char *const skill_dvars[] = {"g_gameskill", "g_gameskill_player_1", "g_gameskill_player_2"};

static volatile LONG picked = -1;	// the difficulty popup's last button this session
static void (*difficulty_save_native)(void);

static void difficulty_save_f(void)
{
	const char *arg = game_cmd_argv(1);
	if (arg && arg[0] >= '0' && arg[0] <= '3' && !arg[1]) {
		InterlockedExchange(&picked, arg[0] - '0');
		log_printf("progress: difficulty %c picked", arg[0]);
	}
	difficulty_save_native();
}

// Hooks profile_difficultySave once the game has registered it.
void progress_poll(void)
{
	if (difficulty_save_native)
		return;
	cmd_function_t *c = *(cmd_function_t **)((uint8_t *)GetModuleHandleA(NULL) + RVA_CMD_FUNCTIONS);
	for (; c; c = c->next) {
		if (c->name && strcmp(c->name, "profile_difficultySave") == 0 && c->function) {
			difficulty_save_native = c->function;
			c->function = difficulty_save_f;
			log_printf("progress: profile_difficultySave hooked");
			return;
		}
	}
}

// The difficulty both play (0 Recruit .. 3 Veteran): the host's pick, else the one it plays;
// -1 if unknown.
static int skill_pick(void)
{
	if (picked >= 0)
		return (int)picked;
	dvar_t *d = game_find_dvar("g_gameskill");
	if (!d || d->type != DVAR_TYPE_INT || d->current.integer < 0 || d->current.integer > 3)
		return -1;
	return d->current.integer;
}

// Both the current and the latched value, so a later latch apply keeps it; and both party
// members' difficulty, from which the level start (rva 0x22e1a0) sets g_gameskill_player_<n>
// again (each member's own profile difficulty).
static void skill_apply(int skill)
{
	int changed = 0;
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	for (int i = 0; i < 2; i++) {
		volatile int32_t *member = (int32_t *)(base + RVA_PARTY_SKILL + i * 0x38);
		changed |= *member != skill;
		*member = skill;
	}
	for (int i = 0; i < 3; i++) {
		dvar_t *d = game_find_dvar(skill_dvars[i]);
		if (!d || d->type != DVAR_TYPE_INT)
			continue;
		changed |= d->current.integer != skill || d->latched.integer != skill;
		d->current.integer = skill;
		d->latched.integer = skill;
		d->modified = 1;
	}
	if (changed)
		log_printf("progress: difficulty %d for both players", skill);
}

int progress_launch_packet(char *buf, int size)
{
	int skill = skill_pick();
	if (skill >= 0) {
		skill_apply(skill);
		char cur[8] = "";
		if (!game_dvar_raw("mw2coop_skill", cur, sizeof(cur)) || cur[0] != '0' + skill || cur[1]) {
			char cmd[40];
			snprintf(cmd, sizeof(cmd), "set mw2coop_skill %d\n", skill);
			game_cbuf_addtext(cmd);
		}
	}
	int n = snprintf(buf, size, "\xff\xff\xff\xffmw2coop_start");
	for (int i = 0; i < SESSION_DVARS && n < size; i++) {
		char value[64] = "";
		// with no start point set, the level writes its list of start points into "start"
		if (!game_dvar_raw(session_dvars[i], value, sizeof(value)) || !is_name(value))
			value[0] = 0;
		// the host's own "set mw2coop_skill" above is still queued
		if (strcmp(session_dvars[i], "mw2coop_skill") == 0)
			snprintf(value, sizeof(value), "%s", skill >= 0 ? (const char[]){(char)('0' + skill), 0} : "");
		n += snprintf(buf + n, size - n, " %s=%s", session_dvars[i], value);
	}
	return n < size ? n : size - 1;
}

// "mw2coop_start start=<value> mw2coop_god=<value> mw2coop_skill=<0..3>"
int progress_on_packet(const char *buf, int len)
{
	static const char tag[] = "\xff\xff\xff\xffmw2coop_start";
	int tl = (int)sizeof(tag) - 1;
	if (len < tl || memcmp(buf, tag, tl) != 0)
		return 0;
	char text[256];
	int n = len - tl < (int)sizeof(text) - 1 ? len - tl : (int)sizeof(text) - 1;
	memcpy(text, buf + tl, n);
	text[n] = 0;
	for (char *tok = strtok(text, " "); tok; tok = strtok(NULL, " ")) {
		char *eq = strchr(tok, '=');
		if (!eq)
			continue;
		*eq = 0;
		const char *value = eq + 1;
		if (strcmp(tok, "mw2coop_skill") == 0 && value[0] >= '0' && value[0] <= '3' && !value[1])
			skill_apply(value[0] - '0');
		int known = 0;
		for (int i = 0; i < SESSION_DVARS; i++)
			known |= strcmp(tok, session_dvars[i]) == 0;
		if (!known || !is_name(value))
			continue;
		char cur[64] = "";
		game_dvar_raw(tok, cur, sizeof(cur));
		if (strcmp(cur, value) != 0) {
			char cmd[128];
			snprintf(cmd, sizeof(cmd), "set %s \"%s\"\n", tok, value);
			game_cbuf_addtext(cmd);
			log_printf("progress: host %s '%s'", tok, value);
		}
	}
	return 1;
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_continue", NULL, NULL, continue_f},
};

void progress_install(void)
{
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\progress.txt");

	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		commands[i].next = *head;
		*head = &commands[i];
		log_printf("progress: added console command %s", commands[i].name);
	}
}
