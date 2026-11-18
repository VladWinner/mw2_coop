// The game's own saves in a co-op campaign: its checkpoints are taken as in single player, and a failed mission loads the last one on both machines.
//
// How single player saves (64-bit build, Ghidra 2026-11-17; the 32-bit build has the same functions):
// - Script SaveGame / SaveGameNoCommit (rva 0x193310 / 0x193460) queue a pending save (SV_AddPendingSave 0x232b00: up to 3, 400 bytes each from 0x19c5f40).
//   At the end of the server frame SV_ProcessPendingSaves (0x233930, from the frame 0x233190) writes each: 0x180930 -> 0x181a00 -> G_SaveGame 0x180cf0 into a
//   memory file of 1 MB (two of them, 0xf56e00 and 0x1056e00: "current" 0x1156e08 and "last commit" 0x1156e00). CommitSave (0x1935f0) swaps them (0x1aed80)
//   and writes the committed one to disk (0x1af150), which it skips in a co-op session (0x233d90). vid_restart and snd_restart save and load the game the same way
//   ("internal/vid_restart", 0x2341a0).
// - What G_SaveGame writes, in segments (each compressed): the level's globals and time, the script VM (variables, every waiting thread), the 2048 entities
//   (0x2a0 each, by field descriptors), up to two clients (gclient 0xafe0: player state, weapons, ammo, health), 32 actors (0x1560), 50 sentients (0x88),
//   vehicles, path nodes, badplaces, the configstrings 0x10..0x8f (objectives and more), physics bodies, effects, sounds, and per local client its cgame
//   state (0xc95f0) and its reliable server commands (0x233aa0). About 2 MB before compression on Estate.
// - loadgame_continue (0x22c6c0) loads the last commit: the same map restart as fast_restart (0x22c970) with G_LoadGame (0x17f8e0, from 0x17f3b0) in place of
//   the level's start. The engine has the co-op paths (DAT 0x52fb5c: the session's two slots at 0x540bb0, stride 0x2650), but over the network the partner is
//   only sent coopRestart and runs a plain fast_restart (0x22f990).
//
// In a co-op campaign (maps\_mw2coop.gsc: saves_on) both machines run the level's autosaves in lockstep and keep the same save in memory (QA nativesave,
// 2026-11-17: host 1978145 and client 1977163 bytes, the difference being each machine's own local client block). A failed mission sets the dvar mw2coop_retry
// "load" on both; the host runs fast_restart, the client runs it on the coopRestart the map restart sends, and here both load their own copy.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "native.h"
#include "net.h"
#include "progress.h"
#include "saves.h"

#ifdef _WIN64
#define RVA_SV_CMD_FAST_RESTART 0x1904d80 // the server command node of fast_restart ({next, name, dir, ext, function})
#define RVA_FAST_RESTART_FN     0x22c680
#define RVA_LOADGAME_MF_FN      0x22c7e0 // loadgame_continue_missionfailed: 0x22c6c0 (0, 1), the last commit when it is this map's, else a fast restart
#define RVA_SAVE_COMMITTED      0x1156e00 // the last committed save in memory (its map checksum at +4, its name at +0x3e0)
#define RVA_MAP_CHECKSUM        0x19c3864
#define RVA_CLC_RING            0x54784c // local client 0's reliable command ring (0x1400ca250: 0x54784c + client * 0x3218)
#define RVA_CGS_SEQUENCE        0x40cab0 // the cgame's last server command sequence (set by 0xa9230 when a save is loaded)
#define RVA_SITE_RING_CALL      0x233b25 // the call of the ring getter in the block writer 0x233aa0 (RVA_: tools/addrsig.py collects it)
#define SITE_RING_CALL          RVA_SITE_RING_CALL
#define RVA_SITE_SEQ_CALL       0x2337b0 // the call of the sequence setter in the block reader 0x2336e0
#define SITE_SEQ_CALL           RVA_SITE_SEQ_CALL
#define RVA_COOP_FLAG           0x52fb5c // the co-op session byte
#define RVA_SV_CMD_MAP          0x1904f10 // the server command node of "map" (handler 0x22cb70)
#define RVA_MAP_FN              0x22cb70
#define RVA_LOAD_NAME           0xf4e140 // the saved game G_InitGame loads (0x40 characters; 0x1819e0 sets it, 0x1807f0 reads it)
#define RVA_SITE_LOAD_CALL      0x17f3f3 // the call of G_LoadGame in 0x17f3b0
#define SITE_LOAD_CALL          RVA_SITE_LOAD_CALL
#define RVA_COMMIT_SITE_1       0x1809b8 // the calls of the disk write 0x1af150
#define RVA_COMMIT_SITE_2       0x180a04
#define RVA_COMMIT_SITE_3       0x17f337
#define RVA_COMMIT_SITE_4       0x22cd75
#define RVA_LEVEL_TIME          0xd96ec8
#else
// 32 bits (VA = rva + 0x400000): Cmd_AddServerCommand 0x45f330 in 0x493340, loadgame_continue 0x659c50, the commit check 0x4af8d0, G_SaveGame 0x5c6530,
// G_LoadGame 0x5c6d20, the block writer 0x49cff0 (ring getter 0x40f9a0) and reader 0x4a9730 (sequence setter 0x41daf0)
// (REF_: tools/gen_addr159.py maps these for the iw4x-sp 1.0.159 exe)
#define REF_SV_CMD_FAST_RESTART 0x166acb8
#define REF_FAST_RESTART_FN     0xfb250
#define REF_LOADGAME_MF_FN      0x259d40
#define REF_SAVE_COMMITTED      0xecb480
#define REF_MAP_CHECKSUM        0x172130c
#define REF_CLC_RING            0x48e33c
#define REF_CGS_SEQUENCE        0x35f2b0
#define REF_RING_CALL           0x9d061
#define REF_SEQ_CALL            0xa97f8
#define REF_COOP_FLAG           0x48897c
#define REF_SV_CMD_MAP          0x166abc8 // the server command node of "map" (0x493340 registers it, handler 0x659780)
#define REF_MAP_FN              0x259780
#define REF_LOAD_NAME           0xcb87f0  // the saved game G_InitGame loads (0x424d00 reads it)
#define REF_LEVEL_TIME          0xca3c44
#define REF_LOAD_CALL           0x885d    // the call of G_LoadGame 0x5c6d20 in 0x408820
#define REF_COMMIT_BODY         0xfac9c   // the disk write 0x4fac90 (cdecl, the slot; its state at +0x4a4) past its first 12 bytes (call 0x490020, test, jz, ret)
#define RVA_COOP_FLAG           REF_COOP_FLAG
#define RVA_SV_CMD_MAP          REF_SV_CMD_MAP
#define RVA_MAP_FN              REF_MAP_FN
#define RVA_LOAD_NAME           REF_LOAD_NAME
#define RVA_LEVEL_TIME          REF_LEVEL_TIME
#define SITE_LOAD_CALL          REF_LOAD_CALL
#define RVA_SV_CMD_FAST_RESTART REF_SV_CMD_FAST_RESTART
#define RVA_FAST_RESTART_FN     REF_FAST_RESTART_FN
#define RVA_LOADGAME_MF_FN      REF_LOADGAME_MF_FN
#define RVA_SAVE_COMMITTED      REF_SAVE_COMMITTED
#define RVA_MAP_CHECKSUM        REF_MAP_CHECKSUM
#define RVA_CLC_RING            REF_CLC_RING
#define RVA_CGS_SEQUENCE        REF_CGS_SEQUENCE
#define SITE_RING_CALL          REF_RING_CALL
#define SITE_SEQ_CALL           REF_SEQ_CALL
#endif

static uint8_t *base;

// ---- the split screen's client blocks
// A split screen session (native.c) runs both players' client side as local client 0, each context with its own copy of client 0's statics; the game's local
// client 1 statics are not used. The save's per-client block of reliable server commands takes a local client's command ring from the client side when the
// server's copy is all acknowledged, and for client 1 read garbage there (QA nativesave 2026-11-17: a size of about 1 GB; the load then read past the end of
// the save, "Trying to read corrupted file"). Each client's ring is its context's copy of client 0's, and so is the sequence the load sets.
static void *(*clc_ring_orig)(int);
static void (*cgs_seq_orig)(int, int);
static void *clc_ring_hook(int lc)
{
	void *p = native_ctx_addr(lc, RVA_CLC_RING);
	return p ? p : clc_ring_orig(lc);
}
static void cgs_seq_hook(int lc, int seq)
{
	int *p = (int *)native_ctx_addr(lc, RVA_CGS_SEQUENCE);
	if (p)
		*p = seq;
	else
		cgs_seq_orig(lc, seq);
}

#ifndef _WIN64
// The official 32-bit exe hashes its own code and some data (docs/32bit-plan.md: VA 0x400ffc-0x408000, 0x417ffc-0x428000, 0x42fffc-0x440000, 0x6a9ffc-0x6aa378)
// and quits with exit code 0x8000dead when they differ. QA (env MW2COOP_HASHDIFF): before a load, the bytes in those ranges that differ from the exe on disk.
static void hash_ranges_diff(void)
{
	static const uint32_t r[][2] = {{0x400ffc, 0x408000}, {0x417ffc, 0x428000}, {0x42fffc, 0x440000}, {0x6a9ffc, 0x6aa378}};
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, sizeof(path));
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
	int shown = 0;
	for (int k = 0; k < 4; k++)
		for (uint32_t va = r[k][0]; va < r[k][1]; va++) {
			uint32_t rva = va - 0x400000, off = 0;
			for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
				if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + sec[i].SizeOfRawData)
					off = rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
			if (!off)
				continue;
			uint8_t b = 0;
			DWORD got = 0;
			SetFilePointer(f, (LONG)off, NULL, FILE_BEGIN);
			if (ReadFile(f, &b, 1, &got, NULL) && got == 1 && b != base[rva] && shown < 40) {
				log_printf("saves: hashed byte differs at VA 0x%x: file %02x, memory %02x", va, b, base[rva]);
				shown++;
			}
		}
	CloseHandle(f);
	log_printf("saves: hashed ranges checked, %d byte(s) differ%s", shown, shown >= 40 ? " (first 40)" : "");
}
#endif

// ---- both PCs hold the same checkpoint
// Each checkpoint's tag (<map>_<level time>_<net|split>, taken when it is committed: the same frame on both lockstep PCs) is kept; a network client sends
// it to the host ("mw2coop_cp <tag>"). Before a failed mission loads the checkpoint, the host compares the two: when they differ (the client's save failed,
// or it has none) both restart the level at its start point instead, and the host tells the client which it is ("mw2coop_retry load|restart", sent before its
// fast_restart, whose coopRestart starts the client's). Owner, 2026-11-17: "what if the host loads its save and the client does not have it?"
static char own_tag[64], partner_tag[64], host_decision[16];
// A checkpoint that leads straight back to the failure (2026-11-18, Just Like Old Times: the owner shot the control room's explosives during the breach, a checkpoint was
// taken a moment after the hit and before the failure; every load of it exploded again, and the mod loaded it again 3 s later: twelve times until the run ended). Such a
// replay fails at the same game time each time; a player who dies again in a hard fight after a load does not. The second load of the same checkpoint whose failure came at
// the same game time restarts the level at its start point instead (as with no checkpoint).
static int restart_req_time, last_load_req_time;
static char last_load_tag[64];
void saves_note_restart_request(int game_time)
{
	restart_req_time = game_time;
}
static volatile DWORD host_decision_at;

void saves_note_checkpoint(const char *tag)
{
	lstrcpynA(own_tag, tag, sizeof(own_tag));
	if (!native_split_active() && *(base + XR(RVA_COOP_FLAG)) && !game_peek(RVA_PARTY_IS_HOST, 1)) {
		char text[96];
		snprintf(text, sizeof(text), "mw2coop_cp %s", GetEnvironmentVariableA("MW2COOP_QA_FAKETAG", NULL, 0) ? "qa_fake_net" : tag); // (QA: a client whose checkpoint differs)
		net_send_mod(text);
	}
}

int saves_on_packet(const char *buf, int len)
{
	static const char cp[] = "\xff\xff\xff\xffmw2coop_cp ", retry[] = "\xff\xff\xff\xffmw2coop_retry ";
	char text[64];
	if (len > (int)sizeof(cp) - 1 && memcmp(buf, cp, sizeof(cp) - 1) == 0) {
		int n = len - ((int)sizeof(cp) - 1) < (int)sizeof(text) - 1 ? len - ((int)sizeof(cp) - 1) : (int)sizeof(text) - 1;
		memcpy(text, buf + sizeof(cp) - 1, n);
		text[n] = 0;
		lstrcpynA(partner_tag, text, sizeof(partner_tag));
		log_printf("saves: the partner's checkpoint %s (this PC's %s)", partner_tag, own_tag[0] ? own_tag : "none");
		return 1;
	}
	if (len > (int)sizeof(retry) - 1 && memcmp(buf, retry, sizeof(retry) - 1) == 0) {
		int n = len - ((int)sizeof(retry) - 1) < (int)sizeof(host_decision) - 1 ? len - ((int)sizeof(retry) - 1) : (int)sizeof(host_decision) - 1;
		memcpy(host_decision, buf + sizeof(retry) - 1, n);
		host_decision[n] = 0;
		host_decision_at = GetTickCount();
		log_printf("saves: the host's failed mission: %s", host_decision);
		return 1;
	}
	return 0;
}

// ---- a failed mission goes back to the last checkpoint
// maps\_mw2coop::mission_failed() sets mw2coop_retry ("load", or "restart" with mw2coop_saves 0) on both machines, then the host runs fast_restart; the map
// restart sends the client coopRestart, on which the client runs fast_restart too (0x22f990). The server command's handler is replaced: with "load" it runs
// loadgame_continue_missionfailed, so both machines load their own copy of the same save (or both restart the level when it has none).
static void (*fast_restart_orig)(void);
static void fast_restart_cmd(void)
{
	char v[16];
	if (!game_dvar_raw("mw2coop_retry", v, sizeof(v)) || strcmp(v, "load") != 0) {
		fast_restart_orig();
		return;
	}
	const int network = *(base + XR(RVA_COOP_FLAG)) && !native_split_active();
	const int doomed = restart_req_time && last_load_req_time && abs(restart_req_time - last_load_req_time) < 300 && !strcmp(last_load_tag, own_tag) &&
			   (!network || game_peek(RVA_PARTY_IS_HOST, 1));
	if (doomed) {
		log_printf("saves: the mission failed again at the same moment (game time %d) after loading checkpoint %s: the checkpoint leads to the failure, the level restarts "
			   "at its start point", restart_req_time, own_tag[0] ? own_tag : "(no tag)");
		last_load_req_time = 0;
		restart_req_time = 0;
		if (network)
			net_send_mod("mw2coop_retry restart");
		game_cbuf_addtext("set mw2coop_retry \"\"\n");
		fast_restart_orig();
		return;
	}
	last_load_req_time = restart_req_time;
	lstrcpynA(last_load_tag, own_tag, sizeof(last_load_tag));
	restart_req_time = 0;
	if (network && game_peek(RVA_PARTY_IS_HOST, 1)) {
		const int same = !partner_tag[0] || strcmp(partner_tag, own_tag) == 0;
		net_send_mod(same ? "mw2coop_retry load" : "mw2coop_retry restart");
		if (!same) {
			log_printf("saves: the checkpoints differ (host %s, client %s): both restart the level at its start point", own_tag, partner_tag);
			game_cbuf_addtext("set mw2coop_retry \"\"\n");
			fast_restart_orig();
			return;
		}
		if (!partner_tag[0])
			log_printf("saves: the client's checkpoint is not known (no packet): loading this PC's %s", own_tag[0] ? own_tag : "none");
	} else if (network && host_decision_at && GetTickCount() - host_decision_at < 20000 && strcmp(host_decision, "restart") == 0) {
		log_printf("saves: the host restarts the level (the checkpoints differ: this PC's %s)", own_tag);
		host_decision_at = 0;
		game_cbuf_addtext("set mw2coop_retry \"\"\n");
		fast_restart_orig();
		return;
	}
	uint8_t *slot = *(uint8_t **)(base + XR(RVA_SAVE_COMMITTED));
	int same_map = slot && *(int *)(slot + 4) == *(int *)(base + XR(RVA_MAP_CHECKSUM)) && slot[0x3e0];
	log_printf("saves: fast_restart after a failed mission: %s", same_map ? "the last checkpoint is loaded" : "no checkpoint of this map in memory, the level restarts");
	game_cbuf_addtext("set mw2coop_retry \"\"\n");
#ifndef _WIN64
	if (GetEnvironmentVariableA("MW2COOP_HASHDIFF", NULL, 0))
		hash_ranges_diff();
#endif
	((void (*)(void))(base + XR(RVA_LOADGAME_MF_FN)))();
}

static void map_cmd(void);
static void (*map_orig)(void);

static DWORD WINAPI fast_restart_wait(LPVOID unused)
{
	(void)unused;
	cmd_function_t *node = (cmd_function_t *)(base + XR(RVA_SV_CMD_FAST_RESTART));
	for (int i = 0; i < 600; i++) { // the server registers its commands once, early
		if (node->function == (void (*)(void))(base + XR(RVA_FAST_RESTART_FN))) {
			fast_restart_orig = node->function;
			node->function = fast_restart_cmd;
			log_printf("saves: fast_restart goes back to the last checkpoint after a failed mission");
			cmd_function_t *map = (cmd_function_t *)(base + XR(RVA_SV_CMD_MAP)); // (registered by the same function, 0x22bef0)
			if (map->function == (void (*)(void))(base + XR(RVA_MAP_FN))) {
				map_orig = map->function;
				map->function = map_cmd;
				log_printf("saves: the map command starts a continued session from its checkpoint");
			} else
				log_printf("saves: the server's map command was not found (%p): Continue starts at the start point", (void *)map->function);
			return 0;
		}
		if (node->function == fast_restart_cmd)
			return 0;
		Sleep(100);
	}
	log_printf("saves: the server's fast_restart command was not found (%p): a failed mission restarts the level", (void *)node->function);
	return 0;
}

// ---- a co-op checkpoint stays in memory
// A committed save also goes to disk (0x1af150 -> 0x1adce0: players\save\<map>.svg, save\autosave\<map>-<name>.svg), unless 0x233d90 says not to: arcade mode,
// Spec Ops, or a running server with sv_saveOnStartMap 0, which is how the co-op session had it while the mod turned the game's saves off. With them on, a
// co-op campaign wrote its checkpoints over the player's single player saves of that level (the 32-bit split screen's console log 2026-11-17: "Writing
// savegame ... to file save\estate.svg"). In a co-op session the write is left out; the checkpoint is loaded from memory. The game's own temporary save of a
// vid_restart / snd_restart (internal/...) is still written: the restart loads it back from the disk.
#ifdef _WIN64
static const uint32_t commit_write_sites[] = {RVA_COMMIT_SITE_1, RVA_COMMIT_SITE_2, RVA_COMMIT_SITE_3, RVA_COMMIT_SITE_4};
#endif
// (32 bits: the function itself, from its 13th byte: the iw4x-sp exe reached it from another place than the two calls of the official one, and the level's autosaves
// went to players\save\autosave there; QA 2026-11-17)
static uintptr_t (*commit_write_orig)(void *); // (32 bits: cdecl, the slot pushed)

// ---- snapshots: the co-op checkpoint on disk, for "Continue" in a later session
// At each co-op checkpoint both PCs write their own copy to players\save\mw2coop_<key>.svg (the key: the session dvar mw2coop_snapkey, the host's playthrough,
// progress.c) in the game's file layout (0x1adce0: the slot's first 0x480 bytes, then its data, *(slot + 0x44c) bytes from *(slot + 0x480)), and its tag
// <map>_<level time>_<net|split> next to the playthrough (lockstep: the same on both PCs). Not with the game's writer: it ends with 0x233cc0, which makes the
// file the profile's last save, and single player's "Resume game" would load the co-op checkpoint. The map command (map_cmd) loads it when a session starts.
#define SLOT_HEADER    0x480

// The dvars the mod's scripts set for this DLL are no part of a save: after a load they kept what they were before it (a load in the same session), or what
// the level's main() set in its first frames, which the load then threw away (a continued session): the split screen never showed both halves (main() sets
// mw2coop_split_view 0 and leaves the 1 to a thread of the intro; QA snapsave in split screen, 2026-11-17). Their values at each checkpoint are kept (with
// the snapshot too) and set again once G_LoadGame is done (g_load_hook). Lockstep: the same values on both PCs.
static const char *const kept_names[] = {"mw2coop_split_view", "mw2coop_hide_names", "mw2coop_ride", "mw2coop_fov_pilot"};
static char kept[400]; // "set <name> \"<value>\"\n" for each, ready for the command buffer
static void keep_dvars(void)
{
	int n = 0;
	kept[0] = 0;
	for (size_t i = 0; i < sizeof(kept_names) / sizeof(kept_names[0]); i++) {
		char v[64] = "";
		game_dvar_raw(kept_names[i], v, sizeof(v));
		v[strcspn(v, "\"\r\n;")] = 0;
		n += snprintf(kept + n, sizeof(kept) - (size_t)n, "set %s \"%s\"\n", kept_names[i], v);
		if (n >= (int)sizeof(kept)) {
			kept[0] = 0;
			return;
		}
	}
}

static void snapshot_write(const uint8_t *slot)
{
	char map[64] = "", key[48] = "", tag[64];
	game_dvar_raw("mapname", map, sizeof(map));
	snprintf(tag, sizeof(tag), "%s_%d_%s", map, *(const int *)(base + XR(RVA_LEVEL_TIME)), native_split_active() ? "split" : "net");
	saves_note_checkpoint(tag);
	keep_dvars();
	if (!game_dvar_raw("mw2coop_snapkey", key, sizeof(key)) || !key[0] || strpbrk(key, "\\/:. "))
		return;
	const int size = *(const int *)(slot + 0x44c);
	const uint8_t *data = *(const uint8_t *const *)(slot + SLOT_HEADER);
	if (!data || size <= 0 || size > 0x100000)
		return;
	static uint8_t header[SLOT_HEADER];
	memcpy(header, slot, sizeof(header));
	snprintf((char *)header + 0x3e0, 0x40, "save\\mw2coop_%s.svg", key);
	char dir[MAX_PATH], tmp[MAX_PATH], file[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, dir, sizeof(dir));
	while (n && dir[n - 1] != '\\')
		n--;
	dir[n] = 0;
	snprintf(tmp, sizeof(tmp), "%splayers\\save\\mw2coop_%s.tmp", dir, key);
	snprintf(file, sizeof(file), "%splayers\\save\\mw2coop_%s.svg", dir, key);
	FILE *f = fopen(tmp, "wb");
	int ok = f && fwrite(header, 1, sizeof(header), f) == sizeof(header) && fwrite(data, 1, (size_t)size, f) == (size_t)size;
	if (f)
		ok = (fclose(f) == 0) && ok;
	if (!ok || !MoveFileExA(tmp, file, MOVEFILE_REPLACE_EXISTING)) {
		log_printf("saves: the checkpoint %s could not be written to %s", tag, file);
		DeleteFileA(tmp);
		return;
	}
	const int host = native_split_active() || (int)game_peek(RVA_PARTY_IS_HOST, 1);
	progress_snapshot_note(key, tag, kept, host);
	log_printf("saves: checkpoint %s kept for Continue (players\\save\\mw2coop_%s.svg, %d bytes)", tag, key, size + SLOT_HEADER);
}

static uintptr_t commit_write_hook(void *slot)
{
	const char *name = (const char *)slot + 0x3e0;
	if ((native_split_active() || *(base + XR(RVA_COOP_FLAG))) && !strstr(name, "internal")) {
		static int told;
		if (!told++)
			log_printf("saves: co-op checkpoints stay in memory, not written over the single player saves ('%.60s')", name);
		snapshot_write((const uint8_t *)slot);
		return 0;
	}
	return commit_write_orig(slot);
}

// A session that continues a playthrough starts by loading its snapshot on both PCs: the lobby (menus.c) sets mw2coop_snapload 1 only when the partner's copy
// has the host's tag, and the start packet takes it to the client. Both PCs start the level with the map command (the party's 0x22e1a0: "map <coop_mapName>");
// with mw2coop_snapload the map command is given the snapshot instead (its handler 0x22cb70 takes a .svg: it reads the map from the file's header and starts
// the server with a saved game), and the name G_InitGame loads (0xf4e140, set by 0x1819e0) is the file.
static void (*map_orig)(void);
static void map_cmd(void)
{
	char v[8] = "", key[48] = "";
	if (game_dvar_raw("mw2coop_snapload", v, sizeof(v)) && strcmp(v, "1") == 0 && game_dvar_raw("mw2coop_snapkey", key, sizeof(key)) && key[0]) {
		char tag[64] = "", want[80];
		const char *map = game_sv_cmd_argv(1);
		const size_t ml = strlen(map);
		game_cbuf_addtext("set mw2coop_snapload 0\n");
		snprintf(want, sizeof(want), "_%s", native_split_active() ? "split" : "net");
		const size_t tl = progress_snapshot_tag(key, tag, sizeof(tag)) ? strlen(tag) : 0;
		if (ml && tl > ml + strlen(want) && strncmp(tag, map, ml) == 0 && tag[ml] == '_' && strcmp(tag + tl - strlen(want), want) == 0) {
			static char file[96];
			snprintf(file, sizeof(file), "save/mw2coop_%s.svg", key);
			lstrcpynA((char *)(base + XR(RVA_LOAD_NAME)), file, 0x40);
			const char *old = game_sv_cmd_set_argv(1, file);
			progress_snapshot_extra(key, kept, sizeof(kept)); // (set again when the load is done: g_load_hook)
			log_printf("saves: the session goes on from the checkpoint %s (%s)", tag, file);
			map_orig();
			if (old)
				game_sv_cmd_set_argv(1, old);
			return;
		}
		log_printf("saves: no checkpoint of '%s' under %s on this PC (tag '%s'): the level starts at its start point", map, key, tag);
	}
	map_orig();
}

// ---- after a load the mod's dvars of the checkpoint are set again (keep_dvars); on 64 bits the trace counts the bytes too
#ifdef _WIN64
#define LOAD_CC
#else
#define LOAD_CC __attribute__((fastcall)) // (32 bits: G_LoadGame 0x5c6d20 takes its save in ECX)
#endif
static void(LOAD_CC *g_load_orig)(void *);
#ifdef _WIN64
static int trace_on(void);
static void trace_begin(int op, const char *what);
static void trace_end_load(void);
#endif
static void LOAD_CC g_load_hook(void *f)
{
#ifdef _WIN64
	int on = trace_on();
	if (on)
		trace_begin('r', "G_LoadGame");
#endif
	g_load_orig(f);
#ifdef _WIN64
	if (on)
		trace_end_load();
#endif
	if (kept[0] && (native_split_active() || *(base + XR(RVA_COOP_FLAG)))) { // the scripts' dvars for this DLL as they were at the checkpoint (keep_dvars)
		game_cbuf_addtext(kept);
		log_printf("saves: the mod's dvars of the checkpoint set again after the load");
	}
}

#ifdef _WIN64
// ---- trace (dvar mw2coop_saves 1, 64 bits): the bytes G_SaveGame writes and G_LoadGame reads, by the place in either function that asked for them. For each
// place, the first time it is reached the bytes so far are logged; a save and the load of it must list the same numbers in the same order (the place in the
// per local client part 0xc95f0 is logged by itself). Only the main thread saves.
#define RVA_MEM_WRITE   0x270c90 // void (memfile *, int size, const void *src)
#define RVA_MEM_READ    0x26feb8 // void (memfile *, int size, void *dst), past its test edx / jz (the first 8 bytes)
#define RVA_G_SAVEGAME  0x180cf0
#define RVA_G_SAVE_END  0x1819e0
#define RVA_G_LOADGAME  0x17f8e0
#define RVA_G_LOAD_END  0x180930
#define RVA_SITE_SAVE_CALL 0x181b45 // the call of G_SaveGame in 0x181a00
#define SITE_SAVE_CALL  RVA_SITE_SAVE_CALL

static void (*mem_write_orig)(void *, int, const void *);
static void (*mem_read_orig)(void *, int, void *);
static void (*g_save_orig)(int, void *);

#define MAX_SITES 400
static struct {
	int op; // 0 none, 'w' save, 'r' load
	unsigned total;
	unsigned n;
	uint32_t site[MAX_SITES];
} tr;

static int trace_on(void)
{
	char v[8];
	return game_dvar_raw("mw2coop_saves", v, sizeof(v)) && v[0] == '1';
}

// the place inside [lo, hi) the stack passes through, as an rva; 0 when none
static uint32_t site_of(uint32_t lo, uint32_t hi)
{
	void *frames[24];
	USHORT n = RtlCaptureStackBackTrace(2, 24, frames, NULL);
	for (USHORT i = 0; i < n; i++) {
		uintptr_t a = (uintptr_t)frames[i];
		if (a >= (uintptr_t)base + lo && a < (uintptr_t)base + hi)
			return (uint32_t)(a - (uintptr_t)base);
	}
	return 0;
}

static void note(int op, int size)
{
	if (tr.op != op || size <= 0)
		return;
	uint32_t site = site_of(XR(0xc95f0), XR(0xc99c0));
	if (!site)
		site = op == 'w' ? site_of(XR(RVA_G_SAVEGAME), XR(RVA_G_SAVE_END)) : site_of(XR(RVA_G_LOADGAME), XR(RVA_G_LOAD_END));
	unsigned i;
	for (i = 0; i < tr.n && tr.site[i] != site; i++)
		;
	if (i == tr.n && tr.n < MAX_SITES) {
		tr.site[tr.n++] = site;
		log_printf("saves: %c #%u site 0x%x at %u bytes", op, tr.n, site, tr.total);
	}
	tr.total += (unsigned)size;
}

static void mem_write_hook(void *f, int size, const void *src)
{
	note('w', size);
	mem_write_orig(f, size, src);
}

static void mem_read_hook(void *f, int size, void *dst)
{
	note('r', size);
	mem_read_orig(f, size, dst);
}

static void trace_begin(int op, const char *what)
{
	memset(&tr, 0, sizeof(tr));
	tr.op = op;
	log_printf("saves: %s begins (slots %d/%d, %d/%d)", what, *(int *)(base + XR(0x540bb0)), *(uint16_t *)(base + XR(0x540bb8)),
		   *(int *)(base + XR(0x540bb0) + 0x2650), *(uint16_t *)(base + XR(0x540bb8) + 0x2650));
}

static void g_save_hook(int a, void *f)
{
	int on = trace_on();
	if (on)
		trace_begin('w', "G_SaveGame");
	g_save_orig(a, f);
	if (on) {
		log_printf("saves: G_SaveGame wrote %u bytes from %u places", tr.total, tr.n);
		tr.op = 0;
	}
}

static void trace_end_load(void)
{
	log_printf("saves: G_LoadGame read %u bytes from %u places", tr.total, tr.n);
	tr.op = 0;
}

static int trace_install(void)
{
	static const uint8_t write_bytes[14] = {0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x40, 0x80, 0x79, 0x21, 0x00};
	static const uint8_t read_bytes[15] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20};
	int ok = hook_function(base + XR(RVA_MEM_WRITE), write_bytes, sizeof(write_bytes), (void *)mem_write_hook, (void **)&mem_write_orig);
	ok += hook_function(base + XR(RVA_MEM_READ), read_bytes, sizeof(read_bytes), (void *)mem_read_hook, (void **)&mem_read_orig);
	ok += hook_call(base + XR(SITE_SAVE_CALL), (void *)g_save_hook, (void **)&g_save_orig);
	return ok;
}
#endif

static int supported; // the game's checkpoints can be loaded in a co-op session on this build

int saves_supported(void)
{
	return supported;
}

void saves_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
#ifndef _WIN64
	// The official 32-bit exe quits with 0x8000dead when it loads a save, in single player too and with no co-op code of ours in the hashed ranges (QA 2026-11-17:
	// nativesave in split screen and network co-op, and loadgame_continue in plain single player on mw2-32-b, all quit right after G_LoadGame restored the saved
	// dvars; MW2COOP_HASHDIFF found no differing byte in the four known hashed ranges). Until that is understood the 32-bit builds keep the game's saves off and
	// restart a failed mission at its start point; the hooks below stay for when it is (env MW2COOP_SAVES32 turns them on for a test).
	// The iw4x-sp client's exe has no such check: there the game's saves work (QA nativesave on mw2-iw4x-b, split screen, 2026-11-17).
	if (!game_is_iw4x() && !GetEnvironmentVariableA("MW2COOP_SAVES32", NULL, 0))
		game_cbuf_addtext("set mw2coop_saves 0\n");
	else
		supported = 1;
#else
	supported = 1;
#endif
	if (!addr_resolved(RVA_SV_CMD_FAST_RESTART) || !addr_resolved(RVA_FAST_RESTART_FN) || !addr_resolved(RVA_LOADGAME_MF_FN) ||
	    !addr_resolved(RVA_SAVE_COMMITTED) || !addr_resolved(RVA_MAP_CHECKSUM)) {
		log_printf("saves: this build's save functions are not known: the game's checkpoints stay off, a failed mission restarts the level");
		game_cbuf_addtext("set mw2coop_saves 0\n"); // (a session dvar: the host sends it to the client, so both machines leave them off)
		supported = 0;
		return;
	}
	CloseHandle(CreateThread(NULL, 0, fast_restart_wait, NULL, 0, NULL));
	int fixes = 0;
	if (!GetEnvironmentVariableA("MW2COOP_NOSAVEFIX", NULL, 0)) { // (QA bisect)
		fixes = hook_call(base + XR(SITE_RING_CALL), (void *)clc_ring_hook, (void **)&clc_ring_orig);
		fixes += hook_call(base + XR(SITE_SEQ_CALL), (void *)cgs_seq_hook, (void **)&cgs_seq_orig);
	}
	log_printf("saves: split screen client blocks of a save, %d of 2 calls", fixes);
	if (supported) { // (the official 32-bit exe, whose saves stay off, keeps these code bytes as they are)
		int writes = 0, nsites = 1;
#ifdef _WIN64
		nsites = (int)(sizeof(commit_write_sites) / sizeof(commit_write_sites[0]));
		for (int i = 0; i < nsites; i++) {
			void *orig = NULL;
			if (hook_call(base + XR(commit_write_sites[i]), (void *)commit_write_hook, &orig)) {
				commit_write_orig = (uintptr_t (*)(void *))orig; // (the same callee at every site)
				writes++;
			}
		}
#else
		static const uint8_t body[11] = {0x56, 0x8b, 0x74, 0x24, 0x08, 0x8b, 0x86, 0xa4, 0x04, 0x00, 0x00}; // push esi; mov esi, [esp+8]; mov eax, [esi+0x4a4]
		writes = hook_function(base + XR(REF_COMMIT_BODY), body, sizeof(body), (void *)commit_write_hook, (void **)&commit_write_orig);
#endif
		log_printf("saves: co-op checkpoints kept off the disk at %d of %d places", writes, nsites);
		log_printf("saves: the checkpoint's dvars after a load: %s",
			   hook_call(base + XR(SITE_LOAD_CALL), (void *)g_load_hook, (void **)&g_load_orig) ? "hooked" : "NOT hooked");
	}
#ifdef _WIN64
	log_printf("saves: save trace hooks %d of 3 (on with dvar mw2coop_saves 1)", trace_install());
#endif
}
