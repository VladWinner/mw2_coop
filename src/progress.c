// Campaign progress that outlives the game: the host keeps each playthrough's mission and
// checkpoint (level start point) in a save of its own with an index,
// <game dir>\mw2coop\progress\save_<n>.txt:
//   map trainer
//   start timed_ads
//   spot 371_m29140_186_90   (where the checkpoint was reached: x_y_z_yaw, m = minus; may be absent)
//   member 0123456789abcdef0123456789abcdef 0110000100000001 Name
// A member is a partner who played it with the host: his settings.ini id, his Steam ID (0 without
// one; a partner who reinstalled and got a new id is still found by it) and his name. The host
// picks a save in the menu ("Continue" is the newest, "Pick a save" lists them with their
// members). With "Continue" the lobby moves to the newest save of the partner who joins. A partner
// who is not a member of the picked save makes Start ask twice (menus.c): continue with him at all,
// then add him to this playthrough's members (yes: the same save), else a new game from the very
// beginning (a new save; the old one stays). A save from before members (mw2coop\progress.txt) continues into a
// new indexed save and is then kept as progress.txt.old.
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
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "progress.h"
#include "saves.h"
#include "menus.h"
#include "settings.h"

static char dir[MAX_PATH]; // the mod folder with its trailing slash

#define MAX_MEMBERS 8
#define MAX_FILES 64

typedef struct {
	char id[33];
	uint64_t xuid;
	char name[33];
} member_t;

typedef struct {
	char map[64], start[64], spot[64];
	char snap[64]; // the game's own checkpoint of this playthrough on the host (saves.c): its tag, <map>_<level time>_<net|split>; "" before snapshots
	int members;
	member_t member[MAX_MEMBERS];
} save_t;

static char chosen_file[MAX_PATH]; // the save the host picked in the menu; "" for the newest
static char target_file[MAX_PATH]; // where this session saves (progress_begin)
static save_t target;		   // its members
static int from_legacy;		   // continuing mw2coop\progress.txt: set aside after the first save

// Map and start point names are plain identifiers; anything else in the file is ignored.
static int is_name(const char *s)
{
	for (; *s; s++)
		if (!(*s == '_' || (*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')))
			return 0;
	return 1;
}

static int read_save(const char *file, save_t *v)
{
	memset(v, 0, sizeof(*v));
	FILE *f = fopen(file, "r");
	if (!f)
		return 0;
	char line[160];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (strncmp(line, "map ", 4) == 0)
			lstrcpynA(v->map, line + 4, sizeof(v->map));
		else if (strncmp(line, "start ", 6) == 0)
			lstrcpynA(v->start, line + 6, sizeof(v->start));
		else if (strncmp(line, "spot ", 5) == 0)
			lstrcpynA(v->spot, line + 5, sizeof(v->spot));
		else if (strncmp(line, "snapshot ", 9) == 0)
			lstrcpynA(v->snap, line + 9, sizeof(v->snap));
		else if (strncmp(line, "member ", 7) == 0 && v->members < MAX_MEMBERS) {
			// member <id> <Steam ID, 16 hex> <name>
			member_t *m = &v->member[v->members];
			unsigned long long xuid = 0;
			int name_at = 0;
			if (sscanf(line + 7, "%32s %llx %n", m->id, &xuid, &name_at) >= 2) {
				m->xuid = xuid;
				if (name_at)
					lstrcpynA(m->name, line + 7 + name_at, sizeof(m->name));
				v->members++;
			}
		}
	}
	fclose(f);
	return v->map[0] && is_name(v->map) && is_name(v->start);
}

static int write_save(const char *file, const save_t *v)
{
	char sub[MAX_PATH];
	snprintf(sub, sizeof(sub), "%sprogress", dir);
	CreateDirectoryA(sub, NULL);
	FILE *f = fopen(file, "w");
	if (!f) {
		log_printf("progress: cannot write %s", file);
		return 0;
	}
	fprintf(f, "map %s\nstart %s\n", v->map, v->start);
	if (v->spot[0] && is_name(v->spot))
		fprintf(f, "spot %s\n", v->spot);
	if (v->snap[0] && is_name(v->snap))
		fprintf(f, "snapshot %s\n", v->snap);
	for (int i = 0; i < v->members; i++)
		fprintf(f, "member %s %016llx %s\n", v->member[i].id[0] ? v->member[i].id : "-",
			(unsigned long long)v->member[i].xuid, v->member[i].name);
	fclose(f);
	return 1;
}

// the member index of this partner (his id, else his Steam ID); -1 if he is none
static int member_index(const save_t *v, const char *id, uint64_t xuid)
{
	for (int i = 0; i < v->members; i++)
		if (id && id[0] && strcmp(v->member[i].id, id) == 0)
			return i;
	for (int i = 0; i < v->members; i++)
		if (xuid && v->member[i].xuid == xuid)
			return i;
	return -1;
}

static int is_member(const save_t *v, const char *id, uint64_t xuid)
{
	return member_index(v, id, xuid) >= 0;
}

static void legacy_file(char *out)
{
	snprintf(out, MAX_PATH, "%sprogress.txt", dir);
}

static int is_legacy(const char *file)
{
	char legacy[MAX_PATH];
	legacy_file(legacy);
	return lstrcmpiA(file, legacy) == 0;
}

// Every save (the indexed ones and the one from before saves had members), the newest first.
typedef struct {
	char file[MAX_PATH];
	FILETIME time;
} save_file_t;

static int list_saves(save_file_t *out)
{
	int n = 0;
	WIN32_FILE_ATTRIBUTE_DATA a;
	legacy_file(out[0].file);
	if (GetFileAttributesExA(out[0].file, GetFileExInfoStandard, &a))
		out[n++].time = a.ftLastWriteTime;
	char pattern[MAX_PATH];
	snprintf(pattern, sizeof(pattern), "%sprogress\\save_*.txt", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		// the newest MAX_FILES of them, whatever their number (the first MAX_FILES the folder listed were kept before, and a newer save past them was never
		// offered: QA lobby 2026-11-17, a test copy with 413 saves)
		do {
			int at = n;
			if (n == MAX_FILES) { // full: the oldest kept one makes room, if this one is newer
				at = 0;
				for (int i = 1; i < n; i++)
					if (CompareFileTime(&out[i].time, &out[at].time) < 0)
						at = i;
				if (CompareFileTime(&fd.ftLastWriteTime, &out[at].time) <= 0)
					continue;
			} else
				n++;
			snprintf(out[at].file, MAX_PATH, "%sprogress\\%s", dir, fd.cFileName);
			out[at].time = fd.ftLastWriteTime;
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	for (int i = 1; i < n; i++) // newest first
		for (int j = i; j > 0 && CompareFileTime(&out[j].time, &out[j - 1].time) > 0; j--) {
			save_file_t t = out[j];
			out[j] = out[j - 1];
			out[j - 1] = t;
		}
	return n;
}

// The i-th readable save, the newest first (i = 0: the newest); its file in file_out.
static int nth_save(int i, save_t *v, char *file_out)
{
	save_file_t files[MAX_FILES];
	int n = list_saves(files), k = 0;
	for (int f = 0; f < n; f++)
		if (read_save(files[f].file, v) && k++ == i) {
			if (file_out)
				lstrcpynA(file_out, files[f].file, MAX_PATH);
			return 1;
		}
	return 0;
}

// a free index: save_<1 + the highest in use>.txt
static void new_save_file(char *out)
{
	int top = 0;
	char pattern[MAX_PATH];
	snprintf(pattern, sizeof(pattern), "%sprogress\\save_*.txt", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			int k = atoi(fd.cFileName + 5);
			if (k > top)
				top = k;
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	snprintf(out, MAX_PATH, "%sprogress\\save_%d.txt", dir, top + 1);
}

static int chosen_save(save_t *v, char *file_out)
{
	if (chosen_file[0]) {
		if (file_out)
			lstrcpynA(file_out, chosen_file, MAX_PATH);
		return read_save(chosen_file, v);
	}
	return nth_save(0, v, file_out);
}

int progress_last(char *map, int map_size, char *start, int start_size)
{
	save_t v;
	if (!nth_save(0, &v, NULL))
		return 0;
	lstrcpynA(map, v.map, map_size);
	lstrcpynA(start, v.start, start_size);
	return 1;
}

int progress_save_info(int i, char *map, int map_size, char *names, int names_size)
{
	save_t v;
	if (!nth_save(i, &v, NULL))
		return 0;
	lstrcpynA(map, v.map, map_size);
	names[0] = 0;
	for (int m = 0; m < v.members; m++) {
		int n = (int)strlen(names);
		snprintf(names + n, names_size - n, "%s%s", m ? ", " : "", v.member[m].name[0] ? v.member[m].name : "?");
	}
	return 1;
}

void progress_choose(int i)
{
	save_t v;
	chosen_file[0] = 0;
	if (i >= 0 && nth_save(i, &v, chosen_file))
		log_printf("progress: the host picked the save %s (map '%s')", chosen_file + strlen(dir), v.map);
}

int progress_chosen_spot(char *spot, int size)
{
	save_t v;
	if (!chosen_save(&v, NULL) || !is_name(v.spot))
		return 0;
	lstrcpynA(spot, v.spot, size);
	return spot[0] != 0;
}

int progress_chosen(char *map, int map_size, char *start, int start_size)
{
	save_t v;
	if (!chosen_save(&v, NULL))
		return 0;
	lstrcpynA(map, v.map, map_size);
	lstrcpynA(start, v.start, start_size);
	return 1;
}

int progress_find_with(const char *id, uint64_t xuid, char *map, int map_size, char *start, int start_size)
{
	save_t v;
	char file[MAX_PATH];
	for (int i = 0; nth_save(i, &v, file); i++)
		if (is_member(&v, id, xuid)) {
			lstrcpynA(chosen_file, file, MAX_PATH);
			lstrcpynA(map, v.map, map_size);
			lstrcpynA(start, v.start, start_size);
			log_printf("progress: the newest save with this partner is %s", file + strlen(dir));
			return 1;
		}
	return 0;
}

int progress_chosen_info(char *map, int map_size, char *names, int names_size)
{
	save_t v;
	if (!chosen_save(&v, NULL))
		return 0;
	lstrcpynA(map, v.map, map_size);
	names[0] = 0;
	for (int m = 0; m < v.members; m++) {
		int n = (int)strlen(names);
		snprintf(names + n, names_size - n, "%s%s", m ? ", " : "", v.member[m].name[0] ? v.member[m].name : "?");
	}
	return 1;
}

int progress_partner_is_new(const char *id, uint64_t xuid)
{
	save_t v;
	if (!chosen_save(&v, NULL) || !v.members)
		return 0; // no save, or one from before saves had members: nobody to compare with
	return !is_member(&v, id, xuid);
}

static void snapshot_key_of(const char *file, char *key, int size);
void progress_begin(int continuing, const char *id, uint64_t xuid, const char *name, int add_member)
{
	save_t v;
	char file[MAX_PATH] = "";
	int have = continuing && chosen_save(&v, file);
	if (!have)
		memset(&v, 0, sizeof(v));
	int member = is_member(&v, id, xuid);
	if (have && !is_legacy(file) && (member || add_member || !v.members)) {
		lstrcpynA(target_file, file, MAX_PATH); // the same playthrough
	} else {
		new_save_file(target_file); // a playthrough of its own (a new campaign, or "no" to adding)
		if (!add_member)
			v.members = 0;
	}
	target = v;
	from_legacy = have && is_legacy(file);
	// a member who got a new id (player.ini lost) but is known by his Steam ID keeps his place
	int at = member_index(&target, id, xuid);
	if (at >= 0 && id && id[0] && strcmp(target.member[at].id, id) != 0) {
		log_printf("progress: member %s has a new id %s (same Steam ID)", target.member[at].id, id);
		lstrcpynA(target.member[at].id, id, sizeof(target.member[at].id));
	}
	if (!member && (id && id[0]) && target.members < MAX_MEMBERS) {
		member_t *m = &target.member[target.members++];
		lstrcpynA(m->id, id, sizeof(m->id));
		m->xuid = xuid;
		lstrcpynA(m->name, name ? name : "", sizeof(m->name));
		m->name[strcspn(m->name, "\r\n")] = 0;
	}
	log_printf("progress: this session saves to %s (%d member%s)", target_file + strlen(dir), target.members,
		   target.members == 1 ? "" : "s");
	char key[48], cmd[96];
	snapshot_key_of(target_file, key, sizeof(key));
	snprintf(cmd, sizeof(cmd), "set mw2coop_snapkey %s\n", key); // (queued before the caller's coopLaunch: the start packet carries it)
	game_cbuf_addtext(cmd);
}

// ---- snapshots: the game's own checkpoint of a playthrough (saves.c). Each PC of the session keeps its own copy, players\save\mw2coop_<key>.svg, and its tag in
// mw2coop\progress\snap_<key>.txt; the host also notes the tag in the playthrough's save ("snapshot <tag>"). The key names the playthrough on both PCs: the
// host's player id (8 characters) and the number of its save file, sent to the client as the session dvar mw2coop_snapkey. "Continue" loads the snapshot when the
// partner's copy has the same tag (menus.c), on both PCs; with another partner the save does not start (owner, 2026-11-17).
static void snapshot_key_of(const char *file, char *key, int size)
{
	const char *name = strrchr(file, '\\');
	name = name ? name + 1 : file;
	char id[9];
	lstrcpynA(id, settings_player_id(), sizeof(id));
	for (char *c = id; *c; c++)
		if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')))
			*c = 'x';
	snprintf(key, size, "%s_%d", id[0] ? id : "host", atoi(name + 5)); // save_<n>.txt
}

static void snap_file(const char *key, char *out)
{
	snprintf(out, MAX_PATH, "%sprogress\\snap_%s.txt", dir, key);
}

void progress_snapshot_note(const char *key, const char *tag, const char *extra, int host)
{
	if (!key[0] || !is_name(key) || !is_name(tag))
		return;
	char file[MAX_PATH], sub[MAX_PATH];
	snprintf(sub, sizeof(sub), "%sprogress", dir);
	CreateDirectoryA(sub, NULL);
	snap_file(key, file);
	FILE *f = fopen(file, "w");
	if (f) {
		fprintf(f, "%s\n%s", tag, extra ? extra : "");
		fclose(f);
	}
	if (host && target_file[0]) {
		lstrcpynA(target.snap, tag, sizeof(target.snap));
		// a checkpoint can come before the mod's first one of the session (progress_save): a save without its mission is no save (read_save), and Continue
		// passed over it (QA lobby 2026-11-17: save_413.txt had "map " empty and the lobby continued an older one)
		if (!target.map[0]) {
			char map[64] = "";
			game_dvar_raw("mapname", map, sizeof(map));
			if (map[0] && is_name(map))
				lstrcpynA(target.map, map, sizeof(target.map));
		}
		write_save(target_file, &target);
	}
}

int progress_snapshot_tag(const char *key, char *tag, int size)
{
	char file[MAX_PATH], line[96] = "";
	tag[0] = 0;
	if (!key[0] || !is_name(key))
		return 0;
	snap_file(key, file);
	FILE *f = fopen(file, "r");
	if (!f)
		return 0;
	if (fgets(line, sizeof(line), f))
		line[strcspn(line, "\r\n")] = 0;
	fclose(f);
	if (!line[0] || !is_name(line))
		return 0;
	lstrcpynA(tag, line, size);
	return 1;
}

int progress_snapshot_extra(const char *key, char *out, int size)
{
	char file[MAX_PATH], line[256];
	int n = 0;
	out[0] = 0;
	if (!key[0] || !is_name(key))
		return 0;
	snap_file(key, file);
	FILE *f = fopen(file, "r");
	if (!f)
		return 0;
	for (int i = 0; fgets(line, sizeof(line), f); i++)
		if (i > 0 && n + (int)strlen(line) < size - 1) { // (the tag is the first line)
			lstrcpynA(out + n, line, size - n);
			n += (int)strlen(line);
		}
	fclose(f);
	return n;
}

int progress_chosen_snapshot(char *key, int key_size, char *tag, int tag_size)
{
	save_t v;
	char file[MAX_PATH] = "";
	key[0] = tag[0] = 0;
	if (!chosen_save(&v, file) || !v.snap[0] || is_legacy(file))
		return 0;
	snapshot_key_of(file, key, key_size);
	lstrcpynA(tag, v.snap, tag_size);
	return 1;
}

void progress_save(const char *map, const char *start, const char *spot)
{
	if (!target_file[0]) // no lobby this run (mw2coop_continue from the console): a save of its own
		new_save_file(target_file);
	lstrcpynA(target.map, map, sizeof(target.map));
	lstrcpynA(target.start, start, sizeof(target.start));
	lstrcpynA(target.spot, spot ? spot : "", sizeof(target.spot));
	if (!write_save(target_file, &target))
		return;
	log_printf("progress: saved map '%s' start '%s' to %s", map, start, target_file + strlen(dir));
	if (from_legacy) {
		char legacy[MAX_PATH], old[MAX_PATH];
		legacy_file(legacy);
		snprintf(old, sizeof(old), "%s.old", legacy);
		if (MoveFileExA(legacy, old, MOVEFILE_REPLACE_EXISTING))
			log_printf("progress: the save from before members is now %s", old + strlen(dir));
		from_legacy = 0;
	}
}

static void continue_f(void)
{
	char map[64], start[64], cmd[192];
	if (!progress_last(map, sizeof(map), start, sizeof(start))) {
		log_printf("progress: nothing to continue (%s)", dir);
		return;
	}
	snprintf(cmd, sizeof(cmd), "set coop_mapName %s\nset start \"%s\"\n", map, start);
	game_cbuf_addtext(cmd);
	log_printf("progress: continue at map '%s' start '%s'", map, start);
}

// Host dvars the client must match before the level loads (level scripts read them).
static const char *const session_dvars[] = {"start", "mw2coop_god", "mw2coop_skill", "mw2coop_ff", "mw2coop_cp_start", "mw2coop_cp_pos",
							  "mw2coop_fall_damage", "mw2coop_jump_height", "mw2coop_gravity", "mw2coop_saves", "mw2coop_snapkey",
							  "mw2coop_snapload"};
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
		// the game's checkpoints (saves.c) only when both builds load them: a machine that cannot would restart the level while the other loads the save
		if (strcmp(session_dvars[i], "mw2coop_saves") == 0 && strcmp(value, "0") != 0 && (!saves_supported() || !menus_partner_saves())) {
			static int told;
			if (!told++)
				log_printf("progress: the game's checkpoints off for this session (%s build does not load them)", saves_supported() ? "the partner's" : "this");
			snprintf(value, sizeof(value), "0");
			game_cbuf_addtext("set mw2coop_saves 0\n");
		}
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
	menus_campaign_loading_begin();
	char text[512];
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
	{NULL, "mw2coop_continue", NULL, NULL, continue_f CMD_NODE_FLAG},
};

void progress_install(void)
{
	DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != '\\')
		n--;
	dir[n] = 0;
	lstrcatA(dir, "mw2coop\\");

	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		game_add_command(&commands[i]);
		log_printf("progress: added console command %s", commands[i].name);
	}
}
