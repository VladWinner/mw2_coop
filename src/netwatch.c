// Network co-op watch (Vlad, 2026-11-19: "a friend hosted; there were moments when on his screen I was already dead while I played on through the level on mine,
// and the other way round. An on-screen notice when the other player shows no sign of life for more than 3 seconds would help: now there is a 12 s answer check
// only when the lobby gathers, none during the game itself").
//
// The network co-op is the game's own lockstep (docs/native-splitscreen-research/coop-host-frame.md): the host builds a frame of both players' usercmds every
// server tick and both PCs execute the same frames. Nothing corrects a divergence: once the two simulations differ (one PC's player dies, the other's does not),
// each PC goes on with its own game. Two checks, both shown in the game's own message window (top left: CG_GameMessage, this PC only; nothing of the lockstep game
// changes, so the check itself cannot make the PCs differ) and written to the log:
//
//  1. Silence: no packet from the partner for 3 s while a level runs here (the client state 6 or more); the time is said again every 5 s, and once more when his
//     packets come back.
//  2. Divergence: the game keeps a running state hash on both PCs (64: rva 0x192d7e4, 32: VA 0x1a7f0e0): in every server frame each player and each actor adds
//     its number, health, position and angles (G_RunFrameForEntity, 64: rva 0x174920). The host writes its hash into the header of every lockstep frame
//     (64: rva 0x22f830; serverId, hash, frame number, validate byte), and the partner's game reads it and ignores it. Here the partner reads it when it starts
//     executing a frame (the frame executor, 64: rva 0x2302d0, 32: VA 0x40c4f0) and takes the difference to its own hash: while both PCs play the same game, both
//     hashes take the same additions and the difference stays the same (whatever it started at: a restart, a loaded checkpoint). The frame where it changes is
//     the first that differed: logged, told to the host (packet mw2coop_desync), shown on both PCs. When the difference holds still again for 10 s (a checkpoint
//     restart brought the games together), that is said too.
//
// [netwatch] in settings.ini is not needed: the checks are on in every network co-op session; [native] netwatch=0 turns both off, netwatchlog=1 logs every
// frame's hashes (QA).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "localize.h"
#include "log.h"
#include "net.h"
#include "netwatch.h"
#include "saves.h"
#include "settings.h"

static int cfg(const char *key, int def) // [native] in settings.ini, as native.c reads it
{
	return (int)GetPrivateProfileIntA("native", key, def, settings_ini_path());
}

#ifdef _WIN64
#define NW_EXEC XR(0x2302d0)          // void (int serverId, int time): execute the lockstep frame stamped `time`
#define NW_RING XR(0x192d380)         // the frame ring: 20 entries {int frameNo; int serverId; msg_t msg (data pointer at +8)}
#define NW_RING_STRIDE 0x38
#define NW_HASH XR(0x192d7e4)         // int: the running state hash
#define NW_IS_HOST XR(0x1905302)      // uint8: this PC is the host of the lockstep session
#define NW_GAME_MESSAGE XR(0xa6400)   // void CG_GameMessage(int localClientNum, const char *text, int flags): channel 2 (gamenotify), message window 0
#define NW_CLS_STATE XR(0x533cb8)     // int: the client's connection state; 6 and up: a level runs
static const uint8_t exec_bytes[] = {0x89, 0x54, 0x24, 0x10, 0x53, 0x41, 0x57, 0x48, 0x81, 0xec, 0x88, 0x08, 0x00, 0x00}; // mov [rsp+10],edx; push rbx; push r15; sub rsp,0x888
#else
#define NW_EXEC XR(0xc4f0)            // VA 0x40c4f0, cdecl
#define NW_RING (XV(0x1a6ad00) - 0x400000u)
#define NW_RING_STRIDE 0x30
#define NW_HASH (XV(0x1a7f0e0) - 0x400000u)
#define NW_IS_HOST (XV(0x1a7f0d9) - 0x400000u)
#define NW_GAME_MESSAGE XR(0x87010)   // VA 0x487010, cdecl (localClientNum, text, flags)
#define NW_CLS_STATE XR(0x525bc0)
static const uint8_t exec_bytes[] = {0x51, 0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x55, 0x8b, 0x6c, 0x24, 0x14}; // push ecx; push ebx; mov ebx,[esp+0xc]; push ebp; mov ebp,[esp+0x14]
#endif
#define NW_RING_ENTRIES 20
static uint8_t *base; // the game's image (netwatch_install)

static int enabled = -1;
static volatile LONG last_packet;	  // GetTickCount of the partner's last packet (net.c)
static volatile LONG last_exec;		  // GetTickCount of the last lockstep frame executed here
static volatile LONG pending;		  // the notice the main thread is to show (NOTE_*)
static volatile LONG pending_secs;	  // its number of seconds
static volatile LONG partner_desync_time; // the host: the level time the client reported a divergence at (packet), 0 none
static volatile LONG desync_since;	  // the client: the level time of the first differing frame (0: they agree)
static volatile LONG desync_state;	  // the client: 1 found (the host is told), 2 the host loads the common checkpoint, 3 it cannot
static volatile LONG resync_wait;	  // the client: a checkpoint load for this is on its way: say so when the hashes agree after it
static volatile LONG partner_insync;	  // the host: the client says the games agree again
enum { NOTE_NONE, NOTE_SILENT, NOTE_BACK, NOTE_DESYNC, NOTE_INSYNC, NOTE_RESYNC, NOTE_RESYNCFAIL, NOTE_RESYNCED };

// --- divergence (the frame executor's hook: main thread, every lockstep frame) ---
static void (*exec_original)(int, int);
static struct {
	int server_id, last_time, have_diff, diff, frames, changes, said;
	DWORD steady_since;
	unsigned long long matched;
} dv;

static void diverged(int time, int was, int now, int host_hash, int ours)
{
	dv.changes++;
	if (dv.changes <= 20 || dv.changes % 200 == 0)
		log_printf("netwatch: the games differ from level time %d on: the host's state hash %08x, ours %08x (the difference was %08x, now %08x; %llu frames agreed before, change %d)", time,
			   (unsigned)host_hash, (unsigned)ours, (unsigned)was, (unsigned)now, dv.matched, dv.changes);
	dv.matched = 0;
	dv.steady_since = GetTickCount();
	if (!dv.said) {
		dv.said = 1;
		InterlockedExchange(&desync_since, time ? time : 1); // netwatch_poll shows it and tells the host, who loads the common checkpoint (saves_resync)
		InterlockedExchange(&desync_state, 1);
	}
}

static void exec_hook(int server_id, int time)
{
	InterlockedExchange(&last_exec, (LONG)GetTickCount());
	if (enabled && !base[NW_IS_HOST]) {
		if (server_id != dv.server_id || time < dv.last_time) { // a new session or a new level: begin again
			if (dv.frames)
				log_printf("netwatch: %d frames compared in the last level (%d changes of the hash difference)", dv.frames, dv.changes);
			memset(&dv, 0, sizeof(dv));
			dv.server_id = server_id;
			InterlockedExchange(&desync_since, 0);
			InterlockedExchange(&desync_state, 0); // (a checkpoint load for it keeps resync_wait: the comparison after it says whether it worked)
		}
		dv.last_time = time;
		const uint8_t *e = base + NW_RING;
		for (int i = 0; i < NW_RING_ENTRIES; i++, e += NW_RING_STRIDE) {
			if (*(const int *)e != time || *(const int *)(e + 4) != server_id)
				continue;
			const uint8_t *data = *(const uint8_t *const *)(e + 0x10);
			if (!data || *(const int *)data != server_id) // the header begins with the serverId: else the layout is not what this reads
				break;
			const int host_hash = *(const int *)(data + 4), ours = *(const int *)(base + NW_HASH);
			const int d = host_hash - ours;
			dv.frames++;
			static int frame_log = -1;
			if (frame_log < 0)
				frame_log = cfg("netwatchlog", 0);
			if (frame_log && dv.frames % 20 == 1)
				log_printf("netwatch: frame %d host %08x ours %08x difference %08x", time, (unsigned)host_hash, (unsigned)ours, (unsigned)d);
			if (!dv.have_diff) {
				dv.have_diff = 1, dv.diff = d, dv.steady_since = GetTickCount();
				log_printf("netwatch: comparing the host's state hash with ours from level time %d on (difference %08x)", time, (unsigned)d);
			} else if (d != dv.diff) {
				diverged(time, dv.diff, d, host_hash, ours);
				dv.diff = d;
			} else {
				dv.matched++;
				if (resync_wait && !dv.said && GetTickCount() - dv.steady_since > 3000) { // the checkpoint loaded for it: the hashes agree for 3 s
					InterlockedExchange(&resync_wait, 0);
					log_printf("netwatch: after the checkpoint load the games agree again (level time %d, %llu frames)", time, dv.matched);
					InterlockedExchange(&pending, NOTE_RESYNCED);
					net_send_mod("mw2coop_nw resynced");
				}
				if (dv.said && GetTickCount() - dv.steady_since > 10000) { // the same for 10 s again: together again (a checkpoint restart)
					dv.said = 0;
					InterlockedExchange(&desync_since, 0);
					log_printf("netwatch: the hashes agree again from level time %d on (%llu frames)", time, dv.matched);
					InterlockedExchange(&pending, NOTE_INSYNC);
				}
			}
			break;
		}
	}
	exec_original(server_id, time);
}

// --- silence (net.c and the control thread) ---
void netwatch_partner_packet(void)
{
	InterlockedExchange(&last_packet, (LONG)GetTickCount());
}

void netwatch_on_desync_packet(int time)
{
	log_printf("netwatch: the partner's PC says the games differ from level time %d on", time);
	InterlockedExchange(&partner_desync_time, time ? time : 1);
}

// "mw2coop_nw <word> [n]": the host could not bring the games together (fail), the client says they agree again (insync)
void netwatch_on_packet(const char *text)
{
	if (!strncmp(text, "fail", 4)) {
		static DWORD at;
		if (GetTickCount() - at > 5000) // (one a report)
			log_printf("netwatch: the host cannot load a common checkpoint for this");
		at = GetTickCount();
		if (InterlockedExchange(&desync_state, 3) != 3) { // the notice at once (then every 15 s with the report's repeats)
			InterlockedExchange(&pending_secs, desync_since / 1000);
			InterlockedExchange(&pending, NOTE_RESYNCFAIL);
		}
	} else if (!strncmp(text, "resynced", 8)) {
		log_printf("netwatch: the partner's PC says the games agree again after the checkpoint load");
		InterlockedExchange(&partner_insync, 2);
	} else if (!strncmp(text, "insync", 6)) {
		log_printf("netwatch: the partner's PC says the games agree again");
		InterlockedExchange(&partner_insync, 1);
	}
}

// The client: the host's mw2coop_resync came (saves.c): both PCs load the common checkpoint
void netwatch_resync_started(void)
{
	InterlockedExchange(&desync_state, 2);
	InterlockedExchange(&resync_wait, 1);
	InterlockedExchange(&pending_secs, desync_since / 1000);
	InterlockedExchange(&pending, NOTE_RESYNC);
}

static int level_runs(void)
{
	return *(volatile int *)(base + NW_CLS_STATE) >= 6;
}

// Every 250 ms (control.c): decides the notices; the main thread shows them (netnotice_f).
void netwatch_poll(void)
{
	if (enabled <= 0)
		return;
	static int silent_said, silent_secs;
	static DWORD silent_next;
	const DWORD now = GetTickCount(), pkt = (DWORD)last_packet, ex = (DWORD)last_exec;
	const int peer = net_partner_known();
	// silence: the level ran here when his packets stopped (this PC executed lockstep frames within 2 s of his last packet), and a level still runs here
	if (peer && pkt && level_runs() && now - pkt > 3000 && (int)(ex - pkt) > -2000) {
		const int secs = (int)((now - pkt) / 1000);
		if (!silent_said || (int)(now - silent_next) >= 0) {
			if (!silent_said)
				log_printf("netwatch: no packet from the partner for %d s while the level runs", secs);
			silent_said = 1, silent_secs = secs, silent_next = now + 5000;
			InterlockedExchange(&pending_secs, secs);
			InterlockedExchange(&pending, NOTE_SILENT);
		}
	} else if (silent_said && pkt && now - pkt < 1000) {
		log_printf("netwatch: the partner's packets are back after about %d s", silent_secs);
		InterlockedExchange(&pending_secs, silent_secs);
		InterlockedExchange(&pending, NOTE_BACK);
		silent_said = 0;
	} else if (silent_said && !level_runs())
		silent_said = 0; // (the level ended meanwhile: a load)
	// the host: the client found the games apart: both load the last checkpoint they hold in common (saves.c), or the players are told they cannot
	const LONG pd = InterlockedExchange(&partner_desync_time, 0);
	if (pd) {
		char why[200] = "";
		const int r = saves_resync(pd, why, sizeof(why));
		if (r != 0)
			InterlockedExchange(&pending_secs, pd / 1000);
		if (r > 0)
			InterlockedExchange(&pending, NOTE_RESYNC);
		else if (r < 0) {
			log_printf("netwatch: the games cannot be brought together: %s", why);
			InterlockedExchange(&pending, NOTE_RESYNCFAIL);
			net_send_mod("mw2coop_nw fail");
		}
	}
	const LONG pi = InterlockedExchange(&partner_insync, 0);
	if (pi)
		InterlockedExchange(&pending, pi == 2 ? NOTE_RESYNCED : NOTE_INSYNC);
	// the client: until the host has answered, the report again every 15 s; when it cannot, the notice again every 15 s (the game's message stays about 5 s)
	static DWORD desync_next;
	const LONG ds = desync_since, st = desync_state;
	if (ds && st != 2 && (!desync_next || (int)(now - desync_next) >= 0)) {
		desync_next = now + 15000;
		InterlockedExchange(&pending_secs, ds / 1000);
		InterlockedExchange(&pending, st == 3 ? NOTE_RESYNCFAIL : NOTE_DESYNC);
		if (st == 1) {
			char text[64];
			snprintf(text, sizeof(text), "mw2coop_desync %ld", (long)ds);
			net_send_mod(text);
		}
	} else if (!ds)
		desync_next = 0;
	if (pending != NOTE_NONE && level_runs())
		game_cbuf_addtext("mw2coop_netnotice\n");
}

// --- the notice (console command, main thread) ---
static void notice_f(void)
{
	const LONG kind = InterlockedExchange(&pending, NOTE_NONE);
	if (kind == NOTE_NONE || !level_runs())
		return;
	const int secs = (int)pending_secs;
	const char *name = kind == NOTE_SILENT ? "MW2COOP_NET_SILENT_FMT" : kind == NOTE_BACK ? "MW2COOP_NET_BACK_FMT" : kind == NOTE_DESYNC ? "MW2COOP_NET_DESYNC_FMT" :
			   kind == NOTE_RESYNC ? "MW2COOP_NET_RESYNC_FMT" : kind == NOTE_RESYNCFAIL ? "MW2COOP_NET_RESYNCFAIL_FMT" : kind == NOTE_RESYNCED ? "MW2COOP_NET_RESYNCED" :
															       "MW2COOP_NET_INSYNC";
	const char *fmt = localize_text(name);
	char text[400];
	if (!fmt)
		fmt = name;
	if (kind == NOTE_INSYNC || kind == NOTE_RESYNCED)
		lstrcpynA(text, fmt, sizeof(text));
	else
		snprintf(text, sizeof(text), fmt, secs);
	((void (*)(int, const char *, int))(base + NW_GAME_MESSAGE))(0, text, 0); // (0: the whole line at once; 0x11, the game's "Game saved", types it out letter by letter)
	log_printf("netwatch: shown: %s", name);
}
// QA: mw2coop_netpause <ms> (this PC sends the partner nothing for that long: net.c asks netwatch_send_paused), mw2coop_netqa_desync (64 bits: player 1's
// health changes on this PC only, one of the values the state hash takes: the two games go apart on purpose)
static volatile LONG pause_until;
int netwatch_send_paused(void)
{
	const LONG u = pause_until;
	return u && (LONG)(GetTickCount() - (DWORD)u) < 0;
}
static void netpause_f(void)
{
	const int ms = atoi(game_cmd_argv(1));
	InterlockedExchange(&pause_until, (LONG)(GetTickCount() + (DWORD)(ms > 0 ? ms : 5000)));
	log_printf("netwatch: QA: nothing is sent to the partner for %d ms", ms > 0 ? ms : 5000);
}
static void netqa_desync_f(void)
{
#ifdef _WIN64
	int *health = (int *)(base + XR(0xd9e070) + 0x90); // gentity 0 (player 1), +0x90: the value G_RunFrameForEntity puts in the hash after the number and +0x7e
	log_printf("netwatch: QA: player 1's entity value at +0x90 %d -> %d on this PC only", *health, *health - 3);
	*health -= 3;
#else
	// 32 bits (the gentity layout differs): the state hash itself moves on this PC, which the other PC sees as a divergence from this frame on (the world stays the same)
	*(volatile int *)(base + NW_HASH) += 0x1234;
	log_printf("netwatch: QA: the state hash moved by 0x1234 on this PC only");
#endif
}
static void netqa_say_f(void) // QA: mw2coop_netqa_say <flags> <text name>: one of the notices at once, with those flags of CG_GameMessage
{
	const char *t = localize_text(game_cmd_argv(2));
	((void (*)(int, const char *, int))(base + NW_GAME_MESSAGE))(0, t ? t : game_cmd_argv(2), atoi(game_cmd_argv(1)));
}
static cmd_function_t commands[] = {
	{NULL, "mw2coop_netnotice", NULL, NULL, notice_f CMD_NODE_FLAG},
	{NULL, "mw2coop_netpause", NULL, NULL, netpause_f CMD_NODE_FLAG},
	{NULL, "mw2coop_netqa_desync", NULL, NULL, netqa_desync_f CMD_NODE_FLAG},
	{NULL, "mw2coop_netqa_say", NULL, NULL, netqa_say_f CMD_NODE_FLAG},
};

void netwatch_install(void)
{
	if (enabled >= 0)
		return;
	base = (uint8_t *)GetModuleHandleA(NULL);
	enabled = cfg("netwatch", 1) != 0;
	if (!enabled) {
		log_printf("netwatch: off ([native] netwatch=0)");
		return;
	}
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
	if (!hook_function(base + NW_EXEC, exec_bytes, sizeof(exec_bytes), (void *)exec_hook, (void **)&exec_original)) {
		log_printf("netwatch: the lockstep frame executor is not as expected: no divergence check (the silence check stays)");
		return;
	}
	log_printf("netwatch: the partner's silence (3 s) and the games' divergence (the host's state hash in each frame) are watched");
}
