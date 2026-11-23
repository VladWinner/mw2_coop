#pragma once
#include <stdint.h>

// A network campaign session that breaks off goes on (rejoin.c; owner 2026-11-23: "lobby and continue"): the host waits in its lobby, the partner's game joins it
// again, and both go on from the last checkpoint they hold in common. After a crash or a restart of either game, the main menu asks to go on.
enum { REJOIN_NONE, REJOIN_LIVE, REJOIN_HOST_WAIT, REJOIN_CLIENT_JOIN };

typedef struct {
	int host;		// 1 this PC hosted the session
	char name[33];		// the partner's name as the party showed it
	uint64_t xuid;		// the partner's Steam ID (XUID)
	char pid[33];		// the partner's player id (his mw2coop settings.ini: the same after a restart, where the iw4x-sp client's XUID is not)
	uint8_t ip[4];		// the partner's address (the client: the host's, to join it again)
	uint16_t port;		// (host order)
	char save[64];		// the host: the playthrough's save file (mw2coop\progress\save_<n>.txt)
	char map[64];		// the mission when it broke off
	unsigned long at;	// when the session began or broke off (seconds since 1970)
} rejoin_info_t;

void rejoin_install(void);
// A network campaign session launches: the host at its Start, the client when the host's start packet comes. Called again for every launch (a relaunch after a break too).
void rejoin_session_begin(int host, const char *partner_name, uint64_t partner_xuid, const char *partner_id);
// net.c: the engine's coopDis packet ("coopDis\0<reason>"), sent (1) or received (0). A reason with END_GAME is a player leaving on purpose.
void rejoin_on_coopdis(int sent, const char *reason);
// A planned end of the session (the last mission is over, the player left the lobby, said no): nothing to go on with.
void rejoin_end(const char *why);
// Control thread (menus_poll, about every 250 ms): the UI state, the client state and sv_running; finds the session's break.
void rejoin_poll(int ui, int cls, int sv_running);
int rejoin_state(void);
void rejoin_set_state(int state, const char *why);
int rejoin_info(rejoin_info_t *out);
// After a game start: 1 once while a session that broke off (or a game that ended while one ran) is recent enough to go on with; the menus ask then.
int rejoin_offer_take(rejoin_info_t *out);
// net.c: the other PC's "mw2coop_rejoin_hello" came in (the host waits in its lobby, or the partner looks for it); and how long ago the last one came (ms).
void rejoin_note_hello(void);
unsigned long rejoin_hello_age(void);
