// Co-op in the game's own menus (no .bat files). This build has no menu text parser, so the
// zone's menus (ASSET_TYPE_MENU 26, as the UI has loaded them) are changed in memory:
//
// - Campaign main menu (main_text): a "TWO PLAYERS ONLINE" button under Mission Select, cloned
//   from Mission Select. It opens the game's own Spec Ops co-op lobby (coop_lobby: Steam friend
//   invites, ready state, Start) with a campaign mission: where the players stopped last
//   (mw2coop\progress.txt), else The Pit (trainer).
// - In that lobby "Change Op" opens the campaign mission list (levels) instead of the Spec Ops
//   list. The mission buttons end in the difficulty popup's "devmap <map>"; in the campaign lobby
//   the devmap command is caught and sets the lobby's mission instead.
//
// Layout of this build's structures (64-bit, found with memdump.c, see NOTES.md):
//   menuDef_t:  name +0, itemCount int +0xbc, onOpen +0xe0, onClose +0xf0, onESC +0xf8,
//               itemDef_t **items +0x168
//   itemDef_t (0x1e8 bytes): name +0, rect {x,y,w,h} +8, rectClient +0x1c, group +0x30,
//               dynamicFlags +0x50 (2 = has focus, 4 = visible), text +0xf0, parent menu +0x100,
//               action +0x128, onFocus +0x138, leaveFocus +0x140, visibleExp +0x198
//   MenuEventHandlerSet {int count; MenuEventHandler **handlers}
//   MenuEventHandler {void *data; char type}  type 0: data is a menu script string
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "access.h"
#include "localize.h"
#include "log.h"
#include "settings.h"
#include "menus.h"
#include "menus_ui.h"
#include "net.h"
#include "progress.h"

#define RVA_UI_CONTEXT 0x1a3ab00 // uiContext_t (Menus_OpenByName's first argument, "openmenu" @0x24ca10)
#define UI_MENUS 0x38		 // menuDef_t *menus[]
#define UI_MENU_COUNT 0x1438	 // int
#define UI_MAX_MENUS 640	 // (0x1438 - 0x38) / 8

typedef struct {
	void *data;
	char type;
	char pad[7];
} MenuEventHandler;

typedef struct {
	int count;
	int pad;
	MenuEventHandler **handlers;
} MenuEventHandlerSet;

static uint8_t *base;
#define FN_SLOT_USED(lobby, i) ((char (*)(void *, int))(base + RVA_LOBBY_SLOT_USED))(lobby, i)

// The menus the UI has loaded (the list Menus_FindByName, rva 0x259d30, searches): no asset
// database call, which blocks while a zone loads (a control-thread lookup during a level load
// hung both games after the briefing).
uint8_t *ui_find_menu(const char *name)
{
	uint8_t *ui = base + RVA_UI_CONTEXT;
	int count = *(int *)(ui + UI_MENU_COUNT);
	uint8_t **menus = (uint8_t **)(ui + UI_MENUS);
	for (int i = 0; i < count && i < UI_MAX_MENUS; i++) {
		const char *n = menus[i] ? *(const char **)menus[i] : NULL;
		if (n && lstrcmpiA(n, name) == 0)
			return menus[i];
	}
	return NULL;
}

int ui_item_index(uint8_t *menu, const char *name)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		const char *n = *(const char **)items[i];
		if (n && strcmp(n, name) == 0)
			return i;
	}
	return -1;
}

// A handler set running one menu script, e.g. "\"open\" \"levels\" ;" (the parsed form the
// zone stores: quoted tokens, commands ended by ';').
void *ui_script_set(const char *script)
{
	MenuEventHandlerSet *set = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*set));
	MenuEventHandler **list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
	MenuEventHandler *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
	h->data = (void *)script;
	h->type = 0;
	list[0] = h;
	set->count = 1;
	set->handlers = list;
	return set;
}

uint8_t *ui_clone_item(uint8_t *from, uint8_t *parent_menu)
{
	uint8_t *it = HeapAlloc(GetProcessHeap(), 0, ITEM_SIZE);
	memcpy(it, from, ITEM_SIZE);
	*(uint8_t **)(it + ITEM_PARENT) = parent_menu;
	*(uint32_t *)(it + ITEM_DYNAMIC_FLAGS) &= ~WINDOW_HASFOCUS;
	return it;
}

uint8_t *ui_clone_menu(uint8_t *from, const char *name)
{
	uint8_t *menu = HeapAlloc(GetProcessHeap(), 0, MENU_SIZE);
	memcpy(menu, from, MENU_SIZE);
	*(const char **)menu = name;
	int count = *(int *)(from + MENU_ITEM_COUNT);
	uint8_t **src = *(uint8_t ***)(from + MENU_ITEMS);
	uint8_t **items = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*items));
	for (int i = 0; i < count; i++)
		items[i] = ui_clone_item(src[i], menu);
	*(uint8_t ***)(menu + MENU_ITEMS) = items;
	return menu;
}

int ui_register_menu(uint8_t *menu)
{
	uint8_t *ui = base + RVA_UI_CONTEXT;
	int count = *(int *)(ui + UI_MENU_COUNT);
	if (count >= UI_MAX_MENUS)
		return 0;
	((uint8_t **)(ui + UI_MENUS))[count] = menu;
	*(int *)(ui + UI_MENU_COUNT) = count + 1;
	return 1;
}

// ---------------------------------------------------------------- campaign main menu button

static uint8_t **main_items; // our items array for main_text, NULL until patched

static const char online_action[] =
	"\"play\" \"mouse_click\" ; "
	"\"setdvar\" \"arcademode\" 0 ; \"setdvar\" \"arcademode_full\" 0 ; \"setdvar\" \"solo_play\" 0 ; "
	"\"setdvar\" \"ui_skip_level_select\" 1 ; "
	"\"setdvar\" \"so_ophud_player0\" 0 ; \"setdvar\" \"so_ophud_player1\" 0 ; "
	"\"exec\" \"xrequirelivesignin\" ; "
	"\"exec\" \"stopListening; mw2coop_online; listen\" ; ";

static const char join_action[] =
	"\"play\" \"mouse_click\" ; "
	"\"exec\" \"xrequirelivesignin\" ; "
	"\"exec\" \"mw2coop_find\" ; ";

// Buttons added under Mission Select, in this order
static const struct {
	const char *name, *text, *action;
} main_buttons[] = {
	{"mw2coop_online", "@MENU_SP_TWO_PLAYER_ONLINE_CAPS", online_action},
	{"mw2coop_find", "@MW2COOP_JOIN", join_action},
};
#define MAIN_BUTTONS (int)(sizeof(main_buttons) / sizeof(main_buttons[0]))

static void patch_main_menu(void)
{
	uint8_t *menu = ui_find_menu("main_text");
	if (!menu)
		return;
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	if (items == main_items)
		return;
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	int at = ui_item_index(menu, "levelselect");
	if (at < 0 || ui_item_index(menu, main_buttons[0].name) >= 0) {
		log_printf("menus: main_text has no levelselect button, no co-op buttons added");
		main_items = items;
		return;
	}
	uint8_t *src = items[at];
	float y0 = *(float *)(src + ITEM_RECT_Y) + BUTTON_HEIGHT;

	uint8_t **list = HeapAlloc(GetProcessHeap(), 0, (count + MAIN_BUTTONS) * sizeof(*list));
	memcpy(list, items, (at + 1) * sizeof(*list));
	for (int b = 0; b < MAIN_BUTTONS; b++) {
		uint8_t *item = HeapAlloc(GetProcessHeap(), 0, ITEM_SIZE);
		memcpy(item, src, ITEM_SIZE);
		float y = y0 + b * BUTTON_HEIGHT;
		*(const char **)item = main_buttons[b].name;
		*(const char **)(item + ITEM_TEXT) = main_buttons[b].text;
		*(MenuEventHandlerSet **)(item + ITEM_ACTION) = ui_script_set(main_buttons[b].action);
		*(uint32_t *)(item + ITEM_DYNAMIC_FLAGS) &= ~WINDOW_HASFOCUS;
		*(float *)(item + ITEM_RECT_Y) = y;
		*(float *)(item + ITEM_RECT_CLIENT_Y) = y;
		list[at + 1 + b] = item;
	}
	memcpy(list + at + 1 + MAIN_BUTTONS, items + at + 1, (count - at - 1) * sizeof(*list));
	// everything below the new buttons moves down
	for (int i = at + 1 + MAIN_BUTTONS; i < count + MAIN_BUTTONS; i++) {
		if (*(float *)(list[i] + ITEM_RECT_Y) >= y0 - 0.5f) {
			*(float *)(list[i] + ITEM_RECT_Y) += MAIN_BUTTONS * BUTTON_HEIGHT;
			*(float *)(list[i] + ITEM_RECT_CLIENT_Y) += MAIN_BUTTONS * BUTTON_HEIGHT;
		}
	}
	// the old items stay valid, so the UI may read the new array before the new count
	main_items = list;
	*(uint8_t ***)(menu + MENU_ITEMS) = list;
	*(int *)(menu + MENU_ITEM_COUNT) = count + MAIN_BUTTONS;
	log_printf("menus: main_text: co-op buttons added under Mission Select (%d items)", count + MAIN_BUTTONS);
}

// ---------------------------------------------------------------- joining a game on the network

// mw2coop_find: the campaign menu's join button. Asks every network the PC is on (LAN, Radmin
// VPN and the like: each adapter's broadcast address, net.c) for campaign lobbies for 2 s, then
// lists the hosts that answered in a popup ("mw2coop_lobbies", built from the difficulty popup);
// the player picks one (mw2coop_join_found <n>; a locked lobby asks for its password, access.c)
// or enters an address. A host answers while it holds a campaign lobby with a free slot that
// is open to the network: "mw2coop_here <host xuid> <p|-> <map> <host name>" (p: password; a
// host on several networks answers on each, the XUID keeps it to one row).
#define FIND_TAG "mw2coop_find"
#define HERE_TAG "mw2coop_here "
#define FIND_MS 2000
#define LIST_ROWS 4
#define MAX_LOBBIES (LIST_ROWS - 1) // the last row enters an address

typedef struct {
	uint8_t ip[4];
	uint16_t port;
	uint64_t xuid;
	int locked;
	char map[32];
	char name[48];
} Lobby;

static CRITICAL_SECTION lobby_lock; // the packet thread adds, the main thread reads
static Lobby lobbies[MAX_LOBBIES];
static int lobby_count;
static volatile LONG finding;
static volatile DWORD find_started, find_sent, join_text_until;
static int find_target; // asking one address (typed in) instead of every network
static uint8_t target_ip[4];
static uint16_t target_port;

static void find_f(void)
{
	if (finding)
		return;
	EnterCriticalSection(&lobby_lock);
	lobby_count = 0;
	LeaveCriticalSection(&lobby_lock);
	find_started = GetTickCount();
	find_sent = 0;
	find_target = 0;
	InterlockedExchange(&finding, 1);
	localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_SEARCHING");
	log_printf("menus: looking for campaign lobbies on the network");
}

// "Name - Mission" in the game's language (Windows-1251)
static void lobby_row(const Lobby *l, char *out, int size)
{
	char key[64] = "MENU_SP_SP_";
	int n = (int)strlen(key);
	for (const char *m = l->map; *m && n < (int)sizeof(key) - 1; m++)
		key[n++] = (char)((*m >= 'a' && *m <= 'z') ? *m - 32 : *m);
	key[n] = 0;
	const char *mission = localize_text(key);
	const char *lock = l->locked ? localize_text("MW2COOP_LOCKED") : NULL;
	snprintf(out, size, "%s - %s%s%s", l->name[0] ? l->name : "?", mission ? mission : l->map, lock ? " " : "",
		 lock ? lock : "");
}

void menus_find_at(const uint8_t ip[4], uint16_t port)
{
	find_f();
	memcpy(target_ip, ip, 4);
	target_port = port;
	find_target = 1;
}

static void find_tick(void)
{
	DWORD now = GetTickCount();
	if (finding) {
		if (now - find_started < FIND_MS) {
			if (!find_sent || now - find_sent >= 700) {
				find_sent = now;
				if (find_target)
					net_send_mod_to(target_ip, target_port, FIND_TAG);
				else
					net_broadcast_mod(FIND_TAG, settings_port());
			}
			return;
		}
		InterlockedExchange(&finding, 0);
		if (find_target) {
			// the address typed in: join it whether or not it answered (an answer tells if it is locked)
			EnterCriticalSection(&lobby_lock);
			int locked = lobby_count ? lobbies[0].locked : 0;
			LeaveCriticalSection(&lobby_lock);
			localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_IDLE");
			access_join(target_ip, target_port, locked);
			return;
		}
		EnterCriticalSection(&lobby_lock);
		int count = lobby_count;
		for (int i = 0; i < MAX_LOBBIES; i++) {
			char alias[32], row[128] = "";
			snprintf(alias, sizeof(alias), "MW2COOP_LOBBY_%d", i + 1);
			if (i < count)
				lobby_row(&lobbies[i], row, sizeof(row));
			localize_set_text(alias, row);
		}
		LeaveCriticalSection(&lobby_lock);
		log_printf("menus: %d campaign lobb%s answered", count, count == 1 ? "y" : "ies");
		// rows without a lobby are hidden (WINDOW_VISIBLE in dynamicFlags), so the focus starts on
		// the first lobby, or on the address row
		uint8_t *list = ui_find_menu("mw2coop_lobbies");
		for (int i = 0; list && i < MAX_LOBBIES; i++) {
			char row[24];
			snprintf(row, sizeof(row), "mw2coop_lobby_%d", i + 1);
			int at = ui_item_index(list, row);
			if (at < 0)
				continue;
			uint32_t *flags = (uint32_t *)((*(uint8_t ***)(list + MENU_ITEMS))[at] + ITEM_DYNAMIC_FLAGS);
			if (i < count)
				*flags |= WINDOW_VISIBLE;
			else
				*flags &= ~(WINDOW_VISIBLE | WINDOW_HASFOCUS);
		}
		// the list opens even when nothing answered: its last row enters an address
		localize_select("MW2COOP_JOIN", count ? "MW2COOP_JOIN_IDLE" : "MW2COOP_JOIN_NONE");
		if (!count)
			join_text_until = now + 4000;
		game_cbuf_addtext("openmenu mw2coop_lobbies\n");
		return;
	}
	if (join_text_until && (int)(now - join_text_until) > 0) {
		join_text_until = 0;
		localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_IDLE");
	}
}

// mw2coop_join_found <n>: a row of the list (1-based)
static void join_found_f(void)
{
	int n = atoi(game_cmd_argv(1)) - 1;
	Lobby l;
	EnterCriticalSection(&lobby_lock);
	int ok = n >= 0 && n < lobby_count;
	if (ok)
		l = lobbies[n];
	LeaveCriticalSection(&lobby_lock);
	if (!ok)
		return;
	if (!l.locked) {
		localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_CONNECTING");
		join_text_until = GetTickCount() + 6000;
	}
	access_join(l.ip, l.port, l.locked);
}

// The list popup: a copy of the difficulty popup (title, four buttons, the description lines)
// with our texts and actions. It is added to the UI's menu list, which a UI zone reload rebuilds.

static void add_lobbies_menu(void)
{
	if (ui_find_menu("mw2coop_lobbies"))
		return;
	uint8_t *src = ui_find_menu("popmenu_difficulty");
	static const char *const buttons[LIST_ROWS] = {"newgame_easy", "newgame_regular", "newgame_hardened",
						       "newgame_veteran"};
	int first = src ? ui_item_index(src, buttons[0]) : -1;
	if (first < 0)
		return;
	uint8_t *menu = ui_clone_menu(src, "mw2coop_lobbies");
	*(void **)(menu + MENU_ON_OPEN) = ui_script_set("\"focusfirst\" ; ");
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		uint8_t *it = items[i];
		const char *text = *(const char **)(it + ITEM_TEXT);
		if (text && strcmp(text, "@MENU_SELECT_DIFFICULTY") == 0)
			*(const char **)(it + ITEM_TEXT) = "@MW2COOP_LOBBIES";
		else if (i > first - 6 && i < first) {
			// the difficulty descriptions and the checkpoint warning
			*(const char **)(it + ITEM_TEXT) = "";
			*(void **)(it + ITEM_TEXT_EXP) = NULL;
		}
	}
	// rows 1..3: the lobbies found; row 4: enter an address
	for (int b = 0; b < LIST_ROWS; b++) {
		int i = ui_item_index(src, buttons[b]);
		if (i < 0)
			continue;
		uint8_t *it = items[i];
		static char names[LIST_ROWS][24], texts[LIST_ROWS][24], actions[LIST_ROWS][128];
		snprintf(names[b], sizeof(names[b]), "mw2coop_lobby_%d", b + 1);
		if (b < MAX_LOBBIES) {
			snprintf(texts[b], sizeof(texts[b]), "@MW2COOP_LOBBY_%d", b + 1);
			snprintf(actions[b], sizeof(actions[b]),
				 "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_found %d\" ; ", b + 1);
		} else {
			snprintf(texts[b], sizeof(texts[b]), "@MW2COOP_BY_ADDRESS");
			snprintf(actions[b], sizeof(actions[b]),
				 "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_address\" ; ");
		}
		*(const char **)it = names[b];
		*(const char **)(it + ITEM_TEXT) = texts[b];
		*(void **)(it + ITEM_ACTION) = ui_script_set(actions[b]);
		*(void **)(it + ITEM_ON_FOCUS) =
			ui_script_set("\"play\" \"mouse_over\" ; \"setItemColor\" \"self\" \"backcolor\" 0 0 0 1 ; ");
		*(void **)(it + ITEM_LEAVE_FOCUS) =
			ui_script_set("\"setItemColor\" \"self\" \"backcolor\" 0 0 0 \"0.0\" ; ");
	}
	if (ui_register_menu(menu))
		log_printf("menus: list of games on the network added (%d items)", count);
}

// ---------------------------------------------------------------- the lobby with a campaign mission

// Both PCs: the party is up, no level runs, and the lobby's mission is not a Spec Ops one
// (the client learns coop_mapName from the host's coopAccept).
static int campaign_lobby(void)
{
	if (!base[RVA_PARTY_ACTIVE])
		return 0;
	char v[64] = "";
	game_dvar_raw("sv_running", v, sizeof(v));
	if (v[0] == '1')
		return 0;
	game_dvar_raw("coop_mapName", v, sizeof(v));
	return v[0] && strncmp(v, "so_", 3) != 0;
}

// Items changed while the lobby holds a campaign mission; the native values are kept to switch
// back for Spec Ops.
typedef struct {
	const char *item;   // name, "name+1" for the unnamed item right after it, or "@TEXT" for the item showing it
	int offset;
	const char *script; // replacement action ...
	const void *value;  // ... or replacement pointer (NULL clears a statement)
	uint8_t *seen;	    // the item the native value was read from (zone reloads bring new ones)
	void *native;
	void *campaign;
} LobbySwap;

static LobbySwap lobby_swaps[] = {
	// the title: "SPECIAL OPS : Two players online" -> "CAMPAIGN : ..."
	{"@MENU_SPECIAL_OPS", ITEM_TEXT, NULL, "@MW2COOP_CAMPAIGN", NULL, NULL, NULL},
	// Change Op: the campaign missions
	{"selectmap", ITEM_ACTION, "\"play\" \"mouse_click\" ; \"open\" \"levels\" ; ", NULL, NULL, NULL, NULL},
	// Start: the native button needs coopready(), which counts the Steam lobby's members; a
	// partner who joined by address has none. mw2coop_start checks the party and the ready state.
	{"startmatch", ITEM_ACTION, "\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_start\" ; ", NULL, NULL, NULL, NULL},
	{"startmatch", 0x198, NULL, NULL, NULL, NULL, NULL}, // disabled when ( !gamehost() || !coopready() )
	{"startmatch", 0x1a8, NULL, NULL, NULL, NULL, NULL},
	{"startmatch", ITEM_TEXT, NULL, "@MW2COOP_START", NULL, NULL, NULL},
	// Ready: the Spec Ops difficulty line, shown (greyed) for missions without Spec Ops difficulty
	{"dont_selectdiff", ITEM_ACTION, "\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_ready\" ; ", NULL, NULL, NULL, NULL},
	{"dont_selectdiff", 0x198, NULL, NULL, NULL, NULL, NULL}, // disabled when the op has no difficulty
	{"dont_selectdiff", ITEM_TEXT, NULL, "@MW2COOP_READY", NULL, NULL, NULL},
	// its focus hint read "not available for this op"
	{"dont_selectdiff", ITEM_ON_FOCUS, "\"play\" \"mouse_over\" ; \"setItemColor\" \"self\" \"backcolor\" 0 0 0 1 ; ", NULL, NULL, NULL, NULL},
	// the greyed "Select difficulty" text drawn on the same line
	{"dont_selectdiff+1", ITEM_TEXT, NULL, "", NULL, NULL, NULL},
};

static int lobby_campaign_set;

// the item showing <text> natively (or our text, once swapped: the swap remembers its item)
static int item_by_text(uint8_t *menu, const char *text, const LobbySwap *w)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		const char *t = *(const char **)(items[i] + ITEM_TEXT);
		if (items[i] == w->seen || (t && strcmp(t, text) == 0))
			return i;
	}
	return -1;
}

static void patch_lobby(int campaign)
{
	uint8_t *menu = ui_find_menu("coop_lobby");
	if (!menu)
		return;
	int changed = 0;
	for (size_t k = 0; k < sizeof(lobby_swaps) / sizeof(lobby_swaps[0]); k++) {
		LobbySwap *w = &lobby_swaps[k];
		char name[64];
		lstrcpynA(name, w->item, sizeof(name));
		char *plus = strchr(name, '+');
		if (plus)
			*plus = 0;
		int i = name[0] == '@' ? item_by_text(menu, name, w) : ui_item_index(menu, name);
		if (i < 0)
			continue;
		if (plus && ++i >= *(int *)(menu + MENU_ITEM_COUNT))
			continue;
		uint8_t *item = (*(uint8_t ***)(menu + MENU_ITEMS))[i];
		void **field = (void **)(item + w->offset);
		if (item != w->seen) {
			w->seen = item;
			w->native = *field;
		}
		if (!w->campaign)
			w->campaign = w->script ? ui_script_set(w->script) : (void *)w->value;
		void *want = campaign ? w->campaign : w->native;
		if (*field != want) {
			*field = want;
			changed = 1;
		}
	}
	if (changed || campaign != lobby_campaign_set)
		log_printf("menus: coop_lobby set for %s", campaign ? "a campaign mission" : "Spec Ops");
	lobby_campaign_set = campaign;
}

// Ready state: each PC sends its own to the partner every second while the lobby is open
// ("\xff\xff\xff\xffmw2coop_ready 0|1", net.c); a partner not heard from for 3 s is not there.
#define READY_TAG "mw2coop_ready "
static volatile LONG my_ready, partner_ready;
static volatile DWORD partner_heard, ready_sent;

static int partner_present(void)
{
	return partner_heard && GetTickCount() - partner_heard < 3000;
}

static int party_full(void)
{
	uint8_t *lobby = *(uint8_t **)(base + RVA_PARTY_LOBBY);
	if (!lobby)
		return 0;
	int members = 0;
	for (int i = 0; i < 2; i++)
		members += FN_SLOT_USED(lobby, i) != 0;
	return members == 2;
}

static void send_ready(void)
{
	char text[32];
	snprintf(text, sizeof(text), READY_TAG "%ld", my_ready);
	net_send_mod(text);
	ready_sent = GetTickCount();
}

// net.c: a lobby query from the network, or a host's answer to ours. Returns 1 if it was one.
int menus_on_lobby_packet(const char *buf, int len, const uint8_t *from_ip, uint16_t from_port, char *reply,
			  int reply_size, int *reply_len)
{
	*reply_len = 0;
	static const char find[] = "\xff\xff\xff\xff" FIND_TAG;
	static const char here[] = "\xff\xff\xff\xff" HERE_TAG;
	if (len == (int)sizeof(find) - 1 && memcmp(buf, find, len) == 0) {
		// answer only while hosting a campaign lobby that still has room
		char lock = access_lobby_flag();
		if (base[RVA_PARTY_IS_HOST] && lobby_campaign_set && !party_full() && lock) {
			char map[64] = "", name[33] = "";
			game_dvar_raw("coop_mapName", map, sizeof(map));
			// the host's name as its party has it (slot 0, rva 0x5dd960, set by the party join 0xd7220)
			lstrcpynA(name, (const char *)base + RVA_PARTY_MEMBER_NAMES, sizeof(name));
			uint64_t xuid = *(uint64_t *)(base + RVA_XUID_TABLE);
			*reply_len = snprintf(reply, reply_size, "%s%016llx %c %s %s", here, (unsigned long long)xuid, lock, map, name);
			log_printf("menus: answered a lobby query from %u.%u.%u.%u:%u", from_ip[0], from_ip[1], from_ip[2],
				   from_ip[3], from_port);
		}
		return 1;
	}
	if (len >= (int)sizeof(here) - 1 && memcmp(buf, here, sizeof(here) - 1) == 0) {
		if (!finding)
			return 1;
		// "<xuid> <p|-> <map> <host name>"
		char text[128];
		int n = len - ((int)sizeof(here) - 1);
		n = n < (int)sizeof(text) - 1 ? n : (int)sizeof(text) - 1;
		memcpy(text, buf + sizeof(here) - 1, n);
		text[n] = 0;
		char *flag = strchr(text, ' ');
		if (!flag || flag[1] == 0 || flag[2] != ' ')
			return 1;
		*flag++ = 0;
		char *map = flag + 2;
		char *name = strchr(map, ' ');
		if (name)
			*name++ = 0;
		uint64_t xuid = strtoull(text, NULL, 16);
		EnterCriticalSection(&lobby_lock);
		int known = 0;
		for (int i = 0; i < lobby_count; i++)
			known |= lobbies[i].xuid == xuid;
		if (!known && lobby_count < MAX_LOBBIES) {
			Lobby *l = &lobbies[lobby_count++];
			memcpy(l->ip, from_ip, 4);
			l->port = from_port;
			l->xuid = xuid;
			l->locked = *flag == 'p';
			lstrcpynA(l->map, map, sizeof(l->map));
			lstrcpynA(l->name, name ? name : "", sizeof(l->name));
			log_printf("menus: lobby at %u.%u.%u.%u:%u: %s, host '%s'%s", from_ip[0], from_ip[1], from_ip[2],
				   from_ip[3], from_port, l->map, l->name, l->locked ? ", password" : "");
		}
		LeaveCriticalSection(&lobby_lock);
		return 1;
	}
	return 0;
}

int menus_on_packet(const char *buf, int len)
{
	static const char tag[] = "\xff\xff\xff\xff" READY_TAG;
	int tl = (int)sizeof(tag) - 1;
	if (len < tl + 1 || memcmp(buf, tag, tl) != 0)
		return 0;
	LONG ready = buf[tl] == '1';
	if (InterlockedExchange(&partner_ready, ready) != ready || !partner_present())
		log_printf("menus: partner %s", ready ? "ready" : "not ready");
	partner_heard = GetTickCount();
	return 1;
}

static void update_texts(void)
{
	int host = base[RVA_PARTY_IS_HOST];
	localize_select("MW2COOP_READY", my_ready ? "MW2COOP_READY_UNSET" : "MW2COOP_READY_SET");
	const char *start;
	if (!partner_present() || (host && !party_full()))
		start = "MW2COOP_START_NEED_PARTNER";
	else if (my_ready && partner_ready)
		start = host ? "MW2COOP_START_GO" : "MW2COOP_START_HOST";
	else if (my_ready)
		start = "MW2COOP_START_WAIT_PARTNER";
	else if (partner_ready)
		start = "MW2COOP_START_WAIT_YOU";
	else
		start = "MW2COOP_START_WAIT_BOTH";
	localize_select("MW2COOP_START", start);
}

// mw2coop_ready: the lobby's ready button (both PCs)
static void ready_f(void)
{
	InterlockedExchange(&my_ready, !my_ready);
	log_printf("menus: %s", my_ready ? "ready" : "not ready");
	send_ready();
	update_texts();
}

// mw2coop_start: the lobby's Start in campaign mode. The party frame launches once both slots
// are taken and the bandwidth test is done.
static void start_f(void)
{
	if (!base[RVA_PARTY_IS_HOST]) {
		log_printf("menus: Start: only the host starts");
		return;
	}
	if (!party_full() || !partner_present()) {
		log_printf("menus: Start: waiting for the second player");
		return;
	}
	if (!my_ready || !partner_ready) {
		log_printf("menus: Start: not everyone is ready (host %ld, partner %ld)", my_ready, partner_ready);
		return;
	}
	log_printf("menus: Start: coopLaunch");
	game_cbuf_addtext("coopLaunch\n");
}

// mw2coop_online: run by the campaign menu's co-op button before "listen" opens the lobby
static void online_f(void)
{
	char map[64] = "trainer", start[64] = "";
	if (progress_last(map, sizeof(map), start, sizeof(start)))
		log_printf("menus: campaign lobby, continuing at '%s' start '%s'", map, start);
	else
		log_printf("menus: campaign lobby, no progress yet: '%s'", map);
	char cmd[192];
	snprintf(cmd, sizeof(cmd), "set coop_mapName %s\nset start \"%s\"\n", map, start);
	game_cbuf_addtext(cmd);
}

// devmap in the host's lobby: run by the campaign mission list (The Pit directly, the other
// missions from the difficulty popup); it sets the lobby's mission instead of starting solo.
static void (*devmap_native)(void);

static void devmap_f(void)
{
	char map[64];
	lstrcpynA(map, game_sv_cmd_argv(1), sizeof(map));
	if (!campaign_lobby() || !base[RVA_PARTY_IS_HOST] || !map[0] || strpbrk(map, " \t;\"\n")) {
		devmap_native();
		return;
	}
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		 "set coop_mapName %s\nset start \"\"\n"
		 "closemenu popmenu_act1\nclosemenu popmenu_act2\nclosemenu popmenu_act3\nclosemenu levels\n",
		 map);
	game_cbuf_addtext(cmd);
	log_printf("menus: campaign lobby mission -> '%s'", map);
}

static void tick_f(void);

static cmd_function_t commands[] = {
	{NULL, "mw2coop_online", NULL, NULL, online_f},
	{NULL, "mw2coop_start", NULL, NULL, start_f},
	{NULL, "mw2coop_ready", NULL, NULL, ready_f},
	{NULL, "mw2coop_menus_tick", NULL, NULL, tick_f},
	{NULL, "mw2coop_find", NULL, NULL, find_f},
	{NULL, "mw2coop_join_found", NULL, NULL, join_found_f},
};

// The menu work runs on the game's main thread, between frames: the control thread only queues
// "mw2coop_menus_tick" (every 250 ms, never while a level runs or loads). The UI zone may be
// reloaded (back from a level), which brings fresh menus without our changes.
static volatile LONG tick_queued;
static volatile DWORD tick_queued_at;

void menus_poll(void)
{
	if (!base)
		return;
	char v[8] = "";
	game_dvar_raw("sv_running", v, sizeof(v));
	if (v[0] == '1')
		return;
	// the queue is cleared by a map load; a tick queued before it never runs
	if (tick_queued && GetTickCount() - tick_queued_at < 3000)
		return;
	tick_queued_at = GetTickCount();
	InterlockedExchange(&tick_queued, 1);
	game_cbuf_addtext("mw2coop_menus_tick\n");
}

static void tick_f(void)
{
	InterlockedExchange(&tick_queued, 0);
	if (!base)
		return;
	patch_main_menu();
	add_lobbies_menu();
	find_tick();
	int campaign = campaign_lobby();
	patch_lobby(campaign);
	access_tick(campaign, base[RVA_PARTY_IS_HOST]);
	if (!campaign) {
		InterlockedExchange(&my_ready, 0);
		InterlockedExchange(&partner_ready, 0);
		partner_heard = 0;
		return;
	}
	if (GetTickCount() - ready_sent >= 1000)
		send_ready();
	update_texts();
}

void menus_install(void)
{
	InitializeCriticalSection(&lobby_lock);
	base = (uint8_t *)GetModuleHandleA(NULL);
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		commands[i].next = *head;
		*head = &commands[i];
	}
	// devmap is a server command: the console node's function is empty and the command goes on
	// to this node (Cmd_AddServerCommand in SV_AddOperatorCommands, rva 0x22bef0)
	cmd_function_t *sv_devmap = (cmd_function_t *)(base + RVA_SV_CMD_DEVMAP);
	if (sv_devmap->name && strcmp(sv_devmap->name, "devmap") == 0 && sv_devmap->function) {
		devmap_native = sv_devmap->function;
		sv_devmap->function = devmap_f;
	}
	log_printf("menus: installed (devmap %s)", devmap_native ? "hooked" : "NOT FOUND");
	if (!devmap_native)
		base = NULL;
}
