// Discord presence, the way H2-Mod and other mods show what the player does: the Discord client listens on a local named pipe
// (\.\pipe\discord-ipc-0 ... 9). A frame is an opcode (int32), a length (int32) and JSON. Opcode 0 is the handshake
// {"v":1,"client_id":"<application id>"}; opcode 1 carries commands, here SET_ACTIVITY. Nothing leaves the PC except what Discord itself
// does with the activity. Off unless settings.ini [discord] client_id is set. The mission comes from the game's dvar mapname, the mode
// from the scripts' mw2coop_mode ("coop" / "solo") and the native split screen flag.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "discord.h"
#include "game.h"
#include "log.h"
#include "settings.h"

static HANDLE pipe = INVALID_HANDLE_VALUE;
static DWORD next_try;
static DWORD last_sent;
static char last_details[96], last_state[48];
static long long started;
static int announced;

static void close_pipe(void)
{
	if (pipe != INVALID_HANDLE_VALUE)
		CloseHandle(pipe);
	pipe = INVALID_HANDLE_VALUE;
	last_details[0] = last_state[0] = 0;
}

static int write_frame(uint32_t op, const char *json)
{
	char buf[1400];
	uint32_t len = (uint32_t)strlen(json);
	if (len > sizeof(buf) - 8)
		return 0;
	memcpy(buf, &op, 4);
	memcpy(buf + 4, &len, 4);
	memcpy(buf + 8, json, len);
	DWORD done;
	return WriteFile(pipe, buf, len + 8, &done, NULL) && done == len + 8;
}

// the answer to a frame (the handshake's READY, a command's result); waits up to ms for it
static int read_frame(char *out, int size, int ms)
{
	DWORD avail = 0, waited = 0;
	while (waited <= (DWORD)ms) {
		if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL))
			return 0;
		if (avail >= 8)
			break;
		Sleep(10);
		waited += 10;
	}
	if (avail < 8)
		return 0;
	uint32_t head[2];
	DWORD got;
	if (!ReadFile(pipe, head, 8, &got, NULL) || got != 8 || head[1] > 4096)
		return 0;
	uint32_t take = head[1] < (uint32_t)size - 1 ? head[1] : (uint32_t)size - 1;
	char *tmp = (char *)LocalAlloc(LPTR, head[1] + 1);
	if (!tmp)
		return 0;
	int ok = head[1] == 0 || (ReadFile(pipe, tmp, head[1], &got, NULL) && got == head[1]);
	if (ok) {
		memcpy(out, tmp, take);
		out[take] = 0;
	}
	LocalFree(tmp);
	return ok ? (int)head[0] + 1 : 0;
}

static int connect_pipe(void)
{
	char name[64];
	const char *forced = getenv("MW2COOP_DISCORD_PIPE"); // QA: a fake Discord
	for (int i = 0; i < 10; i++) {
		if (forced)
			snprintf(name, sizeof(name), "%s", forced);
		else
			snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
		pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (pipe != INVALID_HANDLE_VALUE) {
			char json[128], answer[600];
			snprintf(json, sizeof(json), "{\"v\":1,\"client_id\":\"%s\"}", settings_discord_id());
			if (write_frame(0, json) && read_frame(answer, sizeof(answer), 1500)) {
				log_printf("discord: connected on %s", name);
				return 1;
			}
			log_printf("discord: no answer to the handshake on %s", name);
			close_pipe();
		}
		if (forced)
			break;
	}
	return 0;
}

static const char *mission_title(const char *map)
{
	static const struct { const char *map, *title; } titles[] = {
		{"trainer", "S.S.D.D."}, {"roadkill", "Team Player"}, {"cliffhanger", "Cliffhanger"},
		{"airport", "No Russian"}, {"favela", "Takedown"}, {"invasion", "Wolverines!"},
	};
	for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); i++)
		if (_stricmp(map, titles[i].map) == 0)
			return titles[i].title;
	return map;
}

static int json_safe(const char *s)
{
	for (; *s; s++)
		if (*s == '"' || *s == '\\' || (unsigned char)*s < 32 || (unsigned char)*s > 126)
			return 0;
	return 1;
}

void discord_poll(void)
{
	const char *id = settings_discord_id();
	char map[64] = "", mode[16] = "", split[8] = "", details[96], state[48], json[900];
	DWORD now = GetTickCount();
	if (!id[0])
		return;
	if (pipe == INVALID_HANDLE_VALUE) {
		if ((int)(now - next_try) < 0)
			return;
		next_try = now + 15000;
		if (!connect_pipe())
			return;
		started = (long long)time(NULL);
	}
	game_dvar_raw("mapname", map, sizeof(map));
	game_dvar_raw("mw2coop_mode", mode, sizeof(mode));
	game_dvar_raw("mw2coop_native_split", split, sizeof(split));
	if (!map[0] || !json_safe(map) || !strcmp(mode, "") || !strcmp(map, "ui")) {
		snprintf(details, sizeof(details), "In the menus");
		state[0] = 0;
	} else {
		snprintf(details, sizeof(details), "%s", mission_title(map));
		snprintf(state, sizeof(state), "%s", split[0] == '1' ? "Split screen" : !strcmp(mode, "coop") ? "Co-op" : "Solo");
	}
	/* Discord allows an update every few seconds; send on a change, and again every minute so it stays */
	if (!strcmp(details, last_details) && !strcmp(state, last_state) && now - last_sent < 60000)
		return;
	snprintf(json, sizeof(json),
		"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"details\":\"%s\"%s%s%s,\"timestamps\":{\"start\":%lld}}},\"nonce\":\"%lu\"}",
		(unsigned long)GetCurrentProcessId(), details, state[0] ? ",\"state\":\"" : "", state, state[0] ? "\"" : "", started, (unsigned long)now);
	if (!write_frame(1, json)) {
		log_printf("discord: the pipe closed");
		close_pipe();
		return;
	}
	char answer[600];
	read_frame(answer, sizeof(answer), 200);
	if (!announced || strcmp(details, last_details) || strcmp(state, last_state))
		log_printf("discord: presence '%s' / '%s'", details, state);
	announced = 1;
	snprintf(last_details, sizeof(last_details), "%s", details);
	snprintf(last_state, sizeof(last_state), "%s", state);
	last_sent = now;
}
