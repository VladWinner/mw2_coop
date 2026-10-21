// Who may join a campaign lobby, and how a player gets into a locked one.
//
// Host: the lobby's "Access" row (a copy of Invite Friend on the free line under it) cycles
//   open -> Steam friends only -> password (asked for in the text popup) -> open.
// - A Steam friend of the host always gets in: friends join through Steam invites. The XUID in a
//   coopConnect is the SteamID; ISteamFriends013::GetFriendRelationship (vtable slot 5) == 3.
// - open: anyone who finds the lobby on the network or knows the address.
// - password: others must send "mw2coop_auth <password>" from the address they connect from
//   first; the host keeps that address for 60 s.
// - Steam friends only: the lobby does not answer network queries and others are refused.
// A refused coopConnect never reaches the game; the host answers "coopReject <reason>".
//
// Client: a locked lobby from the list, or an address typed in, opens a text popup
// ("mw2coop_edit", a copy of the create-profile popup with its edit field bound to the dvar
// mw2coop_edit). The same popup sets the host's password.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "access.h"
#include "game.h"
#include "localize.h"
#include "log.h"
#include "settings.h"
#include "menus.h"
#include "menus_ui.h"
#include "net.h"

enum { ACCESS_OPEN, ACCESS_PASSWORD, ACCESS_STEAM };
enum { EDIT_NONE, EDIT_HOST_PASSWORD, EDIT_JOIN_PASSWORD, EDIT_ADDRESS };

static uint8_t *base;
static volatile LONG access_mode = ACCESS_OPEN;
static char password[33];
static volatile LONG hosting; // this PC holds a campaign lobby (set by the menu tick)

static int edit_mode;
static uint8_t edit_ip[4];
static uint16_t edit_port;

#define MAX_AUTH 8
typedef struct {
	uint8_t ip[4];
	uint16_t port;
	DWORD at;
} Auth;
static Auth auths[MAX_AUTH];
static CRITICAL_SECTION auth_lock;

// ---------------------------------------------------------------- Steam

static int steam_friend(uint64_t steam_id)
{
	static void *(*steam_friends)(void);
	if (!steam_friends) {
		HMODULE h = GetModuleHandleA("steam_api64.dll");
		steam_friends = h ? (void *(*)(void))GetProcAddress(h, "SteamFriends") : NULL;
		if (!steam_friends)
			return 0;
	}
	void *friends = steam_friends();
	if (!friends)
		return 0;
	typedef int (*relationship_t)(void *self, uint64_t steam_id);
	relationship_t relationship = (*(relationship_t **)friends)[5];
	return relationship(friends, steam_id) == 3; // k_EFriendRelationshipFriend
}

// ---------------------------------------------------------------- the text popup

static void open_edit(int mode, const char *title, const char *label)
{
	edit_mode = mode;
	localize_select("MW2COOP_EDIT_TITLE", title);
	localize_select("MW2COOP_EDIT_LABEL", label);
	game_cbuf_addtext("set mw2coop_edit \"\"\nopenmenu mw2coop_edit\n");
}

static void add_edit_menu(void)
{
	if (ui_find_menu("mw2coop_edit"))
		return;
	uint8_t *src = ui_find_menu("profile_create_popmenu");
	if (!src || ui_item_index(src, "createprofile") < 0 || ui_item_index(src, "ok") < 0)
		return;
	uint8_t *menu = ui_clone_menu(src, "mw2coop_edit");
	*(void **)(menu + MENU_ON_OPEN) = ui_script_set("\"setfocus\" \"createprofile\" ; ");
	*(void **)(menu + MENU_ON_CLOSE) = NULL;
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	static const char ok[] = "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_edit_ok\" ; ";
	for (int i = 0; i < count; i++) {
		uint8_t *it = items[i];
		const char *name = *(const char **)it;
		const char *text = *(const char **)(it + ITEM_TEXT);
		if (text && strcmp(text, "@MENU_CREATE_PLAYER_PROFILE") == 0)
			*(const char **)(it + ITEM_TEXT) = "@MW2COOP_EDIT_TITLE";
		if (!name)
			continue;
		if (strcmp(name, "createprofile") == 0) {
			// a plain edit field (the profile name field is a file-name field, 16, which drops
			// '.' and ':') holding up to 32 characters, 22 shown at a time (editFieldDef +0x10 maxChars,
			// +0x18 maxPaintChars: the field scrolls with the cursor)
			*(int *)(it + ITEM_TYPE) = ITEM_TYPE_EDITFIELD;
			*(int *)(it + ITEM_DATA_TYPE) = ITEM_TYPE_EDITFIELD;
			uint8_t *field = *(uint8_t **)(it + ITEM_TYPE_DATA);
			if (field) {
				uint8_t *copy = HeapAlloc(GetProcessHeap(), 0, 0x20);
				memcpy(copy, field, 0x20);
				*(int *)(copy + 0x10) = 32; // maxChars
				*(int *)(copy + 0x18) = 22; // maxPaintChars: what fits in the box; longer text scrolls
				*(uint8_t **)(it + ITEM_TYPE_DATA) = copy;
			}
			*(const char **)(it + ITEM_DVAR) = "mw2coop_edit";
			*(const char **)(it + ITEM_TEXT) = "@MW2COOP_EDIT_LABEL";
			*(void **)(it + ITEM_ACCEPT) = ui_script_set(ok); // Enter in the field
		} else if (strcmp(name, "ok") == 0) {
			*(void **)(it + ITEM_ACTION) = ui_script_set(ok);
		} else if (strcmp(name, "cancel") == 0) {
			*(void **)(it + ITEM_ACTION) = ui_script_set("\"play\" \"mouse_click\" ; \"close\" \"self\" ; ");
		}
	}
	if (ui_register_menu(menu))
		log_printf("access: text popup added");
}

static int parse_address(const char *text, uint8_t ip[4], uint16_t *port)
{
	unsigned a, b, c, d, p = settings_port();
	char tail;
	int n = sscanf(text, " %u.%u.%u.%u:%u %c", &a, &b, &c, &d, &p, &tail);
	if ((n != 4 && n != 5) || a > 255 || b > 255 || c > 255 || d > 255 || p == 0 || p > 65535)
		return 0;
	ip[0] = (uint8_t)a, ip[1] = (uint8_t)b, ip[2] = (uint8_t)c, ip[3] = (uint8_t)d;
	*port = (uint16_t)p;
	return 1;
}

static void join_now(const uint8_t ip[4], uint16_t port)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "mw2coop_join %u.%u.%u.%u %u\n", ip[0], ip[1], ip[2], ip[3], port);
	log_printf("access: joining %u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
	localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_CONNECTING");
	game_cbuf_addtext(cmd);
}

// mw2coop_edit_ok: the popup's OK (or Enter)
static void edit_ok_f(void)
{
	char text[64] = "";
	game_dvar_raw("mw2coop_edit", text, sizeof(text));
	int mode = edit_mode;
	edit_mode = EDIT_NONE;
	game_cbuf_addtext("set mw2coop_edit \"\"\n"); // a password does not stay in a dvar
	if (mode == EDIT_HOST_PASSWORD) {
		if (!text[0])
			return; // no password given: the access stays as it was
		lstrcpynA(password, text, sizeof(password));
		InterlockedExchange(&access_mode, ACCESS_PASSWORD);
		log_printf("access: lobby locked with a password");
	} else if (mode == EDIT_JOIN_PASSWORD) {
		char auth[64];
		snprintf(auth, sizeof(auth), "mw2coop_auth %s", text);
		net_send_mod_to(edit_ip, edit_port, auth); // before the join's coopConnect (next frame)
		join_now(edit_ip, edit_port);
	} else if (mode == EDIT_ADDRESS) {
		uint8_t ip[4];
		uint16_t port;
		if (!parse_address(text, ip, &port)) {
			log_printf("access: '%s' is not an address", text);
			return;
		}
		menus_find_at(ip, port); // learns whether it is locked, then access_join
	}
}

// mw2coop_join_address: the list's last row
static void join_address_f(void)
{
	open_edit(EDIT_ADDRESS, "MW2COOP_EDIT_ADDRESS", "MW2COOP_LABEL_ADDRESS");
}

void access_join(const uint8_t ip[4], uint16_t port, int locked)
{
	if (!locked) {
		join_now(ip, port);
		return;
	}
	memcpy(edit_ip, ip, 4);
	edit_port = port;
	open_edit(EDIT_JOIN_PASSWORD, "MW2COOP_EDIT_PASSWORD", "MW2COOP_LABEL_PASSWORD");
}

// ---------------------------------------------------------------- the host's lobby

// mw2coop_access: the lobby's Access row
static void access_f(void)
{
	if (!base[RVA_PARTY_IS_HOST])
		return;
	switch (access_mode) {
	case ACCESS_OPEN:
		InterlockedExchange(&access_mode, ACCESS_STEAM);
		log_printf("access: lobby for Steam friends only");
		break;
	case ACCESS_STEAM:
		open_edit(EDIT_HOST_PASSWORD, "MW2COOP_EDIT_PASSWORD", "MW2COOP_LABEL_PASSWORD");
		break;
	default:
		InterlockedExchange(&access_mode, ACCESS_OPEN);
		password[0] = 0;
		log_printf("access: lobby open");
		break;
	}
}

// The Access row: inserted into coop_lobby's items while it holds a campaign mission.
static uint8_t *lobby_menu, **native_items, **campaign_items;
static int native_count;

static void patch_lobby_row(int campaign)
{
	uint8_t *menu = ui_find_menu("coop_lobby");
	if (!menu)
		return;
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	if (menu != lobby_menu || (items != native_items && items != campaign_items)) {
		// first look, or the UI zone was reloaded
		int at = ui_item_index(menu, "invitefriend");
		if (at < 0)
			return;
		lobby_menu = menu;
		native_items = items;
		native_count = *(int *)(menu + MENU_ITEM_COUNT);
		uint8_t *row = ui_clone_item(items[at], menu);
		float y = *(float *)(row + ITEM_RECT_Y) + BUTTON_HEIGHT;
		*(const char **)row = "mw2coop_access";
		*(const char **)(row + ITEM_TEXT) = "@MW2COOP_ACCESS";
		*(void **)(row + ITEM_ACTION) = ui_script_set("\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_access\" ; ");
		*(void **)(row + ITEM_ON_FOCUS) =
			ui_script_set("\"play\" \"mouse_over\" ; \"setItemColor\" \"self\" \"backcolor\" 0 0 0 1 ; ");
		*(void **)(row + ITEM_LEAVE_FOCUS) = ui_script_set("\"setItemColor\" \"self\" \"backcolor\" 0 0 0 \"0.0\" ; ");
		*(float *)(row + ITEM_RECT_Y) = y;
		*(float *)(row + ITEM_RECT_CLIENT_Y) = y;
		campaign_items = HeapAlloc(GetProcessHeap(), 0, (native_count + 1) * sizeof(*campaign_items));
		memcpy(campaign_items, native_items, (at + 1) * sizeof(*campaign_items));
		campaign_items[at + 1] = row;
		memcpy(campaign_items + at + 2, native_items + at + 1, (native_count - at - 1) * sizeof(*campaign_items));
	}
	uint8_t **want = campaign ? campaign_items : native_items;
	if (items != want) {
		// shrink the count before the array goes back, grow it after the new one is in
		if (!campaign)
			*(int *)(menu + MENU_ITEM_COUNT) = native_count;
		*(uint8_t ***)(menu + MENU_ITEMS) = want;
		if (campaign)
			*(int *)(menu + MENU_ITEM_COUNT) = native_count + 1;
	}
}

void access_tick(int campaign_lobby, int host)
{
	if (!base)
		return;
	add_edit_menu();
	patch_lobby_row(campaign_lobby);
	InterlockedExchange(&hosting, campaign_lobby && host);
	if (!campaign_lobby) {
		// a new lobby starts open; the password is not kept
		if (access_mode != ACCESS_OPEN && !host) {
			InterlockedExchange(&access_mode, ACCESS_OPEN);
			password[0] = 0;
		}
		return;
	}
	if (!host) {
		localize_set_text("MW2COOP_ACCESS", ""); // the host's setting is not ours to show
		return;
	}
	static const char *const texts[] = {"MW2COOP_ACCESS_OPEN", "MW2COOP_ACCESS_PASSWORD", "MW2COOP_ACCESS_STEAM"};
	localize_select("MW2COOP_ACCESS", texts[access_mode]);
}

char access_lobby_flag(void)
{
	if (access_mode == ACCESS_STEAM)
		return 0;
	return access_mode == ACCESS_PASSWORD ? 'p' : '-';
}

// ---------------------------------------------------------------- packets (game's network thread)

static int authorized(const uint8_t ip[4], uint16_t port)
{
	int ok = 0;
	EnterCriticalSection(&auth_lock);
	for (int i = 0; i < MAX_AUTH; i++)
		if (auths[i].port && GetTickCount() - auths[i].at < 60000 && memcmp(auths[i].ip, ip, 4) == 0 &&
		    auths[i].port == port)
			ok = 1;
	LeaveCriticalSection(&auth_lock);
	return ok;
}

static void authorize(const uint8_t ip[4], uint16_t port)
{
	EnterCriticalSection(&auth_lock);
	// a free slot, else the oldest
	int slot = 0;
	for (int i = 0; i < MAX_AUTH; i++) {
		if (!auths[i].port) {
			slot = i;
			break;
		}
		if (GetTickCount() - auths[i].at > GetTickCount() - auths[slot].at)
			slot = i;
	}
	memcpy(auths[slot].ip, ip, 4);
	auths[slot].port = port;
	auths[slot].at = GetTickCount();
	LeaveCriticalSection(&auth_lock);
}

int access_on_packet(const char *buf, int len, const uint8_t ip[4], uint16_t port, char *reply, int reply_size,
		     int *reply_len)
{
	static const char auth[] = "\xff\xff\xff\xffmw2coop_auth ";
	int al = (int)sizeof(auth) - 1;
	if (len >= al && memcmp(buf, auth, al) == 0) {
		if (hosting && access_mode == ACCESS_PASSWORD) {
			int n = len - al;
			int ok = n == (int)strlen(password) && memcmp(buf + al, password, n) == 0;
			if (ok)
				authorize(ip, port);
			log_printf("access: %s password from %u.%u.%u.%u:%u", ok ? "right" : "wrong", ip[0], ip[1], ip[2],
				   ip[3], port);
		}
		return 1;
	}
	if (!hosting || access_mode == ACCESS_OPEN)
		return 0;
	// "\xff\xff\xff\xff" "R" <2 bytes> "coopConnect\0" <protocol byte> <xuid, 8 bytes> ...
	static const char connect[] = "coopConnect";
	const char *at = NULL;
	for (int i = 4; i + (int)sizeof(connect) + 9 <= len && i < 16; i++)
		if (memcmp(buf + i, connect, sizeof(connect)) == 0)
			at = buf + i + sizeof(connect);
	if (!at)
		return 0;
	uint64_t xuid;
	memcpy(&xuid, at + 1, 8);
	if (steam_friend(xuid) || (access_mode == ACCESS_PASSWORD && authorized(ip, port)))
		return 0;
	*reply_len = snprintf(reply, reply_size, "\xff\xff\xff\xff" "coopReject %s",
			      access_mode == ACCESS_STEAM ? "MW2COOP_FRIENDS_ONLY" : "MW2COOP_BAD_PASSWORD");
	static DWORD last_log;
	if (GetTickCount() - last_log > 2000) {
		last_log = GetTickCount();
		log_printf("access: refused %u.%u.%u.%u:%u (xuid %016llx, %s)", ip[0], ip[1], ip[2], ip[3], port,
			   (unsigned long long)xuid, access_mode == ACCESS_STEAM ? "not a Steam friend" : "no password");
	}
	return 1;
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_edit_ok", NULL, NULL, edit_ok_f},
	{NULL, "mw2coop_join_address", NULL, NULL, join_address_f},
	{NULL, "mw2coop_access", NULL, NULL, access_f},
};

void access_install(void)
{
	InitializeCriticalSection(&auth_lock);
	base = (uint8_t *)GetModuleHandleA(NULL);
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		commands[i].next = *head;
		*head = &commands[i];
	}
	game_cbuf_addtext("set mw2coop_edit \"\"\n");
}
