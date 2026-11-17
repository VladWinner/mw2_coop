#pragma once
#include <stdint.h>

// Progress file on the host and the mw2coop_continue console command.
void progress_install(void);

// Host: remember the mission and checkpoint to continue from.
// spot: where the checkpoint was reached ("x_y_z_yaw", m = minus), or "".
void progress_save(const char *map, const char *start, const char *spot);

// Host: the out-of-band packet that tells the client the start point; returns its length.
int progress_launch_packet(char *buf, int size);

// Hooks the difficulty popup's profile_difficultySave once it exists (control thread, polled).
void progress_poll(void);

// Client: handles a received packet if it is ours (returns 1: drop it, the game must not see it).
int progress_on_packet(const char *buf, int len);

// Host: where this session saves, called at Start once the partner is known. continuing: the
// lobby is on a save (the picked one, else the newest); add_member: the host said yes to adding a
// partner who is not a member. A new campaign (also the host's no to adding) starts a new save.
void progress_begin(int continuing, const char *id, uint64_t xuid, const char *name, int add_member);

// Host: 1 if the picked save (else the newest) has members and this partner is not one of them.
int progress_partner_is_new(const char *id, uint64_t xuid);

// Host: picks the newest save this partner is a member of; 0 if none.
int progress_find_with(const char *id, uint64_t xuid, char *map, int map_size, char *start, int start_size);

// Saves, the newest first: save i's mission and its members' names; 0 past the last.
int progress_save_info(int i, char *map, int map_size, char *names, int names_size);

// The picked save (else the newest): its mission and its members' names; 0 if none.
int progress_chosen_info(char *map, int map_size, char *names, int names_size);

// The host picks save i for the lobby (-1: the newest).
void progress_choose(int i);

// The picked save (else the newest): where its checkpoint was reached; 0 if not known.
int progress_chosen_spot(char *spot, int size);

// The picked save (else the newest): mission and start point; 0 if none.
int progress_chosen(char *map, int map_size, char *start, int start_size);

// the newest save: mission and start point; 0 if none
int progress_last(char *map, int map_size, char *start, int start_size);
// Snapshots (the game's own checkpoint of a playthrough, saves.c): each PC notes its copy's tag under the session's key (host: also in the playthrough's save)
void progress_snapshot_note(const char *key, const char *tag, const char *extra, int host); // extra: lines kept after the tag (saves.c: dvars)
// The lines after the tag (what progress_snapshot_note was given as extra); their length
int progress_snapshot_extra(const char *key, char *out, int size);
// This PC's tag of the snapshot under key; 0 if it has none
int progress_snapshot_tag(const char *key, char *tag, int size);
// The key and the tag of the snapshot of the save the host picked; 0 if it has none (a save from before snapshots)
int progress_chosen_snapshot(char *key, int key_size, char *tag, int tag_size);

// Split screen Start (menus.c, native.c): each player's difficulty for the level (0 Recruit .. 3 Veteran) into g_gameskill (player 1's), g_gameskill_player_1 / _2 and the
// party members' difficulty, current and latched.
void progress_split_skill(int p1, int p2);
// The difficulty the Special Ops lobby's popup picked: g_gameskill's latched value (its setdvar), else its current one; -1 if neither is 0..3.
int progress_skill_lobby(void);
