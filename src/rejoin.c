// Going on with a network campaign session that broke off (owner, 2026-11-23: "lobby and continue").
//
// What the engine does when the link breaks in a level (QA 2026-11-23, network pairs on S.S.D.D.: the client's packets held back 25 s, the client's process
// killed, the host's process killed): after 10 s without the partner the host ends the level, sends the partner "coopDis COOP_CLIENTDROP" and is back in
// its co-op lobby alone, with the game's error "the connection with the other player was lost" over it; the client is back in the lobby too, and about
// 12 s later its party has timed out and it hosts an empty lobby of its own. Nothing goes on: the host's Start would begin the mission again from its
// start, not from the last checkpoint, and the partner has to find the lobby himself.
//
// What the mod does instead (the lobby side is menus.c, rejoin_lobby_tick):
// - Each launch of a network campaign session is noted here and in mw2coop\progress\rejoin.txt: who hosted, the partner's name, Steam ID and address,
//   the host's save file of the playthrough.
// - The break: the level ends here (sv_running 0, client state 0) and the UI is back in the lobby or a main menu while the session runs, without a player
//   having left on purpose (the engine's coopDis reasons with END_GAME), and without the mod ending it (the last mission's end).
// - The host's lobby then continues the playthrough (its save, its last checkpoint), lets the partner in whatever the access, and waits; the partner's
//   game joins the host's address again every few seconds. When both are back and ready, the host's lobby starts after a short count and both load the
//   checkpoint they hold in common (saves.c), as "Continue" does.
// - A game that closed (or crashed) while its session ran, or while it waited, asks at the next start, within 30 minutes: open the lobby again (the
//   host), join the host again (the partner).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "log.h"
#include "net.h"
#include "progress.h"
#include "rejoin.h"

#define OFFER_SECONDS (30 * 60) // a session that broke off this long ago is not offered any more

static CRITICAL_SECTION lock;
static volatile LONG state = REJOIN_NONE;
static rejoin_info_t info;
static char file[MAX_PATH];
static volatile LONG sv_seen;	 // the session's level has run since its launch
static volatile LONG planned;	 // a player left on purpose (END_GAME), or the mod ended the session
static char last_reason[48];	 // the engine's last coopDis reason during the session
static volatile LONG offer;	 // a session of an earlier run of the game to go on with (rejoin_offer_take)

static const char *state_name(int s)
{
	return s == REJOIN_LIVE ? "live" : s == REJOIN_HOST_WAIT ? "host_wait" : s == REJOIN_CLIENT_JOIN ? "client_join" : "none";
}

static int state_of(const char *name)
{
	return !strcmp(name, "live") ? REJOIN_LIVE : !strcmp(name, "host_wait") ? REJOIN_HOST_WAIT : !strcmp(name, "client_join") ? REJOIN_CLIENT_JOIN : REJOIN_NONE;
}

// a name, a map or a save file: printable, no spaces' worth of trouble in a line of the record
static void clean(char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 32)
			*s = ' ';
}

static void write_record(void) // (lock held)
{
	if (!file[0])
		return;
	const int s = (int)state;
	if (s == REJOIN_NONE) {
		DeleteFileA(file);
		return;
	}
	char temp[MAX_PATH + 8];
	snprintf(temp, sizeof(temp), "%s.tmp", file);
	FILE *f = fopen(temp, "w");
	if (!f)
		return;
	fprintf(f, "state %s\nrole %s\nat %lu\nxuid %016llx\npid %s\naddr %u.%u.%u.%u %u\nsave %s\nmap %s\nname %s\n", state_name(s), info.host ? "host" : "client",
		info.at, (unsigned long long)info.xuid, info.pid, info.ip[0], info.ip[1], info.ip[2], info.ip[3], (unsigned)info.port, info.save, info.map, info.name);
	fclose(f);
	MoveFileExA(temp, file, MOVEFILE_REPLACE_EXISTING);
}

static int read_record(rejoin_info_t *v, int *s)
{
	FILE *f = fopen(file, "r");
	if (!f)
		return 0;
	char line[160], word[16], rest[140];
	memset(v, 0, sizeof(*v));
	*s = REJOIN_NONE;
	int have_role = 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		rest[0] = 0;
		if (sscanf(line, "%15s %139[^\n]", word, rest) < 1)
			continue;
		if (!strcmp(word, "state"))
			*s = state_of(rest);
		else if (!strcmp(word, "role"))
			v->host = !strcmp(rest, "host"), have_role = 1;
		else if (!strcmp(word, "at"))
			v->at = strtoul(rest, NULL, 10);
		else if (!strcmp(word, "xuid"))
			v->xuid = strtoull(rest, NULL, 16);
		else if (!strcmp(word, "addr")) {
			unsigned a, b, c, d, p;
			if (sscanf(rest, "%u.%u.%u.%u %u", &a, &b, &c, &d, &p) == 5 && a < 256 && b < 256 && c < 256 && d < 256 && p < 65536)
				v->ip[0] = (uint8_t)a, v->ip[1] = (uint8_t)b, v->ip[2] = (uint8_t)c, v->ip[3] = (uint8_t)d, v->port = (uint16_t)p;
		} else if (!strcmp(word, "pid"))
			lstrcpynA(v->pid, rest, sizeof(v->pid));
		else if (!strcmp(word, "save"))
			lstrcpynA(v->save, rest, sizeof(v->save));
		else if (!strcmp(word, "map"))
			lstrcpynA(v->map, rest, sizeof(v->map));
		else if (!strcmp(word, "name"))
			lstrcpynA(v->name, rest, sizeof(v->name));
	}
	fclose(f);
	return have_role && *s != REJOIN_NONE;
}

void rejoin_session_begin(int host, const char *partner_name, uint64_t partner_xuid, const char *partner_id)
{
	EnterCriticalSection(&lock);
	const int again = state == REJOIN_LIVE && !sv_seen && (unsigned long)time(NULL) - info.at < 15; // (the host's start packet comes several times)
	rejoin_info_t v;
	memset(&v, 0, sizeof(v));
	v.host = host;
	lstrcpynA(v.name, partner_name ? partner_name : "", sizeof(v.name));
	clean(v.name);
	v.xuid = partner_xuid;
	lstrcpynA(v.pid, partner_id ? partner_id : "", sizeof(v.pid));
	clean(v.pid);
	net_partner_address(v.ip, &v.port);
	if (host)
		progress_current_save(v.save, sizeof(v.save));
	v.at = (unsigned long)time(NULL);
	info = v;
	InterlockedExchange(&state, REJOIN_LIVE);
	InterlockedExchange(&sv_seen, 0);
	InterlockedExchange(&planned, 0);
	last_reason[0] = 0;
	write_record();
	LeaveCriticalSection(&lock);
	if (!again)
		log_printf("rejoin: a network session begins (%s, partner '%s', %016llx, %u.%u.%u.%u:%u%s%s)", host ? "host" : "client", v.name, (unsigned long long)v.xuid, v.ip[0],
			   v.ip[1], v.ip[2], v.ip[3], (unsigned)v.port, v.save[0] ? ", save " : "", v.save);
}

void rejoin_on_coopdis(int sent, const char *reason)
{
	if (state != REJOIN_LIVE || !reason)
		return;
	char r[48];
	lstrcpynA(r, reason, sizeof(r));
	clean(r);
	EnterCriticalSection(&lock);
	const int changed = strcmp(r, last_reason) != 0;
	lstrcpynA(last_reason, r, sizeof(last_reason));
	LeaveCriticalSection(&lock);
	if (strstr(r, "END_GAME"))
		InterlockedExchange(&planned, 1);
	if (changed)
		log_printf("rejoin: coopDis %s '%s'%s", sent ? "sent" : "received", r, strstr(r, "END_GAME") ? " (a player left on purpose: nothing to go on with)" : "");
}

void rejoin_end(const char *why)
{
	EnterCriticalSection(&lock);
	const LONG was = InterlockedExchange(&state, REJOIN_NONE);
	write_record();
	LeaveCriticalSection(&lock);
	InterlockedExchange(&offer, 0);
	if (was != REJOIN_NONE)
		log_printf("rejoin: over (%s)", why);
}

void rejoin_set_state(int s, const char *why)
{
	EnterCriticalSection(&lock);
	const LONG was = InterlockedExchange(&state, s);
	if (s == REJOIN_HOST_WAIT || s == REJOIN_CLIENT_JOIN)
		info.at = (unsigned long)time(NULL);
	write_record();
	LeaveCriticalSection(&lock);
	if (was != s)
		log_printf("rejoin: %s -> %s (%s)", state_name(was), state_name(s), why);
}

int rejoin_state(void)
{
	return (int)state;
}

int rejoin_info(rejoin_info_t *out)
{
	EnterCriticalSection(&lock);
	*out = info;
	LeaveCriticalSection(&lock);
	return state != REJOIN_NONE;
}

static volatile LONG hello_at; // GetTickCount of the last hello (0: none)

void rejoin_note_hello(void)
{
	InterlockedExchange(&hello_at, (LONG)(GetTickCount() | 1));
}

unsigned long rejoin_hello_age(void)
{
	const DWORD at = (DWORD)hello_at;
	return at ? GetTickCount() - at : 0xffffffffUL;
}

int rejoin_offer_take(rejoin_info_t *out)
{
	if (!InterlockedExchange(&offer, 0))
		return 0;
	return rejoin_info(out), 1;
}

void rejoin_poll(int ui, int cls, int sv_running)
{
	static int menu_polls;
	if (state != REJOIN_LIVE) {
		menu_polls = 0;
		return;
	}
	if (sv_running) {
		static DWORD map_read;
		const int first = !InterlockedExchange(&sv_seen, 1);
		if (first || GetTickCount() - map_read > 4000) { // (the next mission of the session is the one it breaks off in)
			map_read = GetTickCount();
			char map[64] = "";
			game_dvar_raw("mapname", map, sizeof(map));
			clean(map);
			if (!strncmp(map, "so_", 3)) { // a Special Ops op over the network (the host's start packet goes with its launch too): the game's own lobby, no playthrough
				rejoin_end("a Special Ops op");
				return;
			}
			EnterCriticalSection(&lock);
			const int changed = map[0] && strcmp(map, info.map) != 0;
			if (changed) {
				lstrcpynA(info.map, map, sizeof(info.map));
				write_record();
			}
			LeaveCriticalSection(&lock);
		}
		menu_polls = 0;
		return;
	}
	// the level is gone and the menus are up: the lobby (11), the main menu (1) or Special Ops' main page (14), twice in a row (about half a second)
	if (!sv_seen || cls != 0 || (ui != 11 && ui != 1 && ui != 14)) {
		menu_polls = 0;
		return;
	}
	if (++menu_polls < 2)
		return;
	menu_polls = 0;
	char reason[48];
	EnterCriticalSection(&lock);
	lstrcpynA(reason, last_reason[0] ? last_reason : "none seen", sizeof(reason));
	LeaveCriticalSection(&lock);
	if (planned) {
		rejoin_end(reason[0] ? reason : "left on purpose");
		return;
	}
	log_printf("rejoin: the session broke off in '%s' (the engine's reason: %s; ui state %d)", info.map, reason, ui);
	rejoin_set_state(info.host ? REJOIN_HOST_WAIT : REJOIN_CLIENT_JOIN, "the link broke");
}

void rejoin_install(void)
{
	InitializeCriticalSection(&lock);
	DWORD n = GetModuleFileNameA(NULL, file, MAX_PATH);
	while (n > 0 && file[n - 1] != '\\')
		n--;
	file[n] = 0;
	char sub[MAX_PATH];
	snprintf(sub, sizeof(sub), "%smw2coop\\progress", file);
	CreateDirectoryA(sub, NULL);
	lstrcatA(file, "mw2coop\\progress\\rejoin.txt");
	rejoin_info_t v;
	int s;
	if (!read_record(&v, &s))
		return;
	const unsigned long age = (unsigned long)time(NULL) - v.at;
	if (age > OFFER_SECONDS) {
		log_printf("rejoin: the session of an earlier run broke off %lu min ago: not offered", age / 60);
		DeleteFileA(file);
		return;
	}
	info = v;
	InterlockedExchange(&offer, 1);
	log_printf("rejoin: the %s session of an earlier run (%s, partner '%s', %s) ended %lu s ago without a planned end: the main menu offers to go on", info.host ? "hosted" : "joined",
		   state_name(s), info.name, info.map, age);
}
