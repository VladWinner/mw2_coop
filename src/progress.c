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
// "mw2coop_start <start>" (progress_launch_packet); the client sets its start dvar from it
// before the level loads (progress_on_packet).
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

int progress_launch_packet(char *buf, int size)
{
	char start[64] = "";
	// with no start point set, the level writes its list of start points into "start"
	if (!game_dvar_raw("start", start, sizeof(start)) || !is_name(start))
		start[0] = 0;
	return snprintf(buf, size, "\xff\xff\xff\xffmw2coop_start %s", start);
}

int progress_on_packet(const char *buf, int len)
{
	static const char tag[] = "\xff\xff\xff\xffmw2coop_start";
	int tl = (int)sizeof(tag) - 1;
	if (len < tl || memcmp(buf, tag, tl) != 0)
		return 0;
	char start[64] = "";
	int n = len - tl - 1;
	if (n > 0 && buf[tl] == ' ') {
		if (n >= (int)sizeof(start))
			n = sizeof(start) - 1;
		memcpy(start, buf + tl + 1, n);
		start[n] = 0;
	}
	if (!is_name(start))
		return 1;
	char cur[64] = "";
	game_dvar_raw("start", cur, sizeof(cur));
	if (strcmp(cur, start) != 0) {
		char cmd[96];
		snprintf(cmd, sizeof(cmd), "set start \"%s\"\n", start);
		game_cbuf_addtext(cmd);
		log_printf("progress: host start point '%s'", start);
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
