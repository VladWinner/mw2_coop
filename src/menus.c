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
#include <string.h>

#include "game.h"
#include "localize.h"
#include "log.h"
#include "menus.h"
#include "net.h"
#include "progress.h"

#define ITEM_SIZE 0x1e8
#define MENU_ITEM_COUNT 0xbc
#define MENU_ITEMS 0x168
#define ITEM_RECT_Y 0x0c
#define ITEM_RECT_CLIENT_Y 0x20
#define ITEM_DYNAMIC_FLAGS 0x50
#define ITEM_TEXT 0xf0
#define ITEM_ACTION 0x128
#define ITEM_ON_FOCUS 0x138
#define WINDOW_HASFOCUS 2
#define BUTTON_HEIGHT 20.0f
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
static uint8_t *find_menu(const char *name)
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

static int item_index(uint8_t *menu, const char *name)
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
static MenuEventHandlerSet *script_set(const char *script)
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

// ---------------------------------------------------------------- campaign main menu button

static uint8_t **main_items; // our items array for main_text, NULL until patched

static const char online_action[] =
	"\"play\" \"mouse_click\" ; "
	"\"setdvar\" \"arcademode\" 0 ; \"setdvar\" \"arcademode_full\" 0 ; \"setdvar\" \"solo_play\" 0 ; "
	"\"setdvar\" \"ui_skip_level_select\" 1 ; "
	"\"setdvar\" \"so_ophud_player0\" 0 ; \"setdvar\" \"so_ophud_player1\" 0 ; "
	"\"exec\" \"xrequirelivesignin\" ; "
	"\"exec\" \"stopListening; mw2coop_online; listen\" ; ";

static void patch_main_menu(void)
{
	uint8_t *menu = find_menu("main_text");
	if (!menu)
		return;
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	if (items == main_items)
		return;
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	int at = item_index(menu, "levelselect");
	if (at < 0 || item_index(menu, "mw2coop_online") >= 0) {
		log_printf("menus: main_text has no levelselect button, no co-op button added");
		main_items = items;
		return;
	}
	uint8_t *src = items[at];
	float y = *(float *)(src + ITEM_RECT_Y) + BUTTON_HEIGHT;

	uint8_t *item = HeapAlloc(GetProcessHeap(), 0, ITEM_SIZE);
	memcpy(item, src, ITEM_SIZE);
	*(const char **)item = "mw2coop_online";
	*(const char **)(item + ITEM_TEXT) = "@MENU_SP_TWO_PLAYER_ONLINE_CAPS";
	*(MenuEventHandlerSet **)(item + ITEM_ACTION) = script_set(online_action);
	*(uint32_t *)(item + ITEM_DYNAMIC_FLAGS) &= ~WINDOW_HASFOCUS;
	*(float *)(item + ITEM_RECT_Y) = y;
	*(float *)(item + ITEM_RECT_CLIENT_Y) = y;

	uint8_t **list = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(*list));
	memcpy(list, items, (at + 1) * sizeof(*list));
	list[at + 1] = item;
	memcpy(list + at + 2, items + at + 1, (count - at - 1) * sizeof(*list));
	// everything below the new button moves down one row
	for (int i = at + 2; i <= count; i++) {
		if (*(float *)(list[i] + ITEM_RECT_Y) >= y - 0.5f) {
			*(float *)(list[i] + ITEM_RECT_Y) += BUTTON_HEIGHT;
			*(float *)(list[i] + ITEM_RECT_CLIENT_Y) += BUTTON_HEIGHT;
		}
	}
	// the old items stay valid, so the UI may read the new array before the new count
	main_items = list;
	*(uint8_t ***)(menu + MENU_ITEMS) = list;
	*(int *)(menu + MENU_ITEM_COUNT) = count + 1;
	log_printf("menus: main_text: co-op button added under Mission Select (%d items)", count + 1);
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
	const char *item;   // name, or "name+1" for the unnamed item right after it
	int offset;
	const char *script; // replacement action ...
	const void *value;  // ... or replacement pointer (NULL clears a statement)
	uint8_t *seen;	    // the item the native value was read from (zone reloads bring new ones)
	void *native;
	void *campaign;
} LobbySwap;

static LobbySwap lobby_swaps[] = {
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

static void patch_lobby(int campaign)
{
	uint8_t *menu = find_menu("coop_lobby");
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
		int i = item_index(menu, name);
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
			w->campaign = w->script ? script_set(w->script) : (void *)w->value;
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
	int campaign = campaign_lobby();
	patch_lobby(campaign);
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
