// The player's settings for the mod: <game dir>\mw2coop\settings.ini, read once at start.
//   [network]
//   port=28960    the game's network port: the host listens on it, and joining (an address typed
//                 in without a port, the search on the network) uses it. Both players need the
//                 same port; most never change it.
//   steam_relay=0 1: a Steam invite's game traffic goes through Steam P2P (steamnet.c)
// The player's id is in <game dir>\mw2coop\player.ini (not in the release archive, so updating the
// mod keeps it):
//   [player]
//   id=<32 hex digits>  this player, made up once (random) the first time the lobby needs it; the
//                 host's saves know their members by it (progress.c), so it works without a
//                 Steam ID (other stores, two copies on one account)
// The port is the default of the dvar net_port, registered with an immediate 28960 (rva 0x293380
// in NET_Init, 0x292f02 in a second registration); DllMain runs before the game's own start, so
// the immediates are patched before the dvar exists.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "settings.h"

#define DEFAULT_PORT 28960
static uint16_t port = DEFAULT_PORT;
static int steam_relay;
static char player_id[33];
static char player_ini[MAX_PATH];

static const uint32_t net_port_immediates[] = {0x293381, 0x292f03}; // "mov edx, 28960": ba 20 71 00 00

static void patch_port(uint16_t value)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	for (size_t i = 0; i < sizeof(net_port_immediates) / sizeof(net_port_immediates[0]); i++) {
		uint8_t *at = base + net_port_immediates[i];
		if (at[-1] != 0xba || *(uint32_t *)at != DEFAULT_PORT) {
			log_printf("settings: unexpected code at rva 0x%x, net_port stays %d", net_port_immediates[i], DEFAULT_PORT);
			return;
		}
	}
	for (size_t i = 0; i < sizeof(net_port_immediates) / sizeof(net_port_immediates[0]); i++) {
		uint8_t *at = base + net_port_immediates[i];
		DWORD old;
		VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old);
		*(uint32_t *)at = value;
		VirtualProtect(at, 4, old, &old);
	}
	FlushInstructionCache(GetCurrentProcess(), base, 0);
	log_printf("settings: net_port default %u", value);
}

static int is_id(const char *s)
{
	int n = 0;
	for (; s[n]; n++)
		if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f')))
			return 0;
	return n == 32;
}

static void load_player_id(const char *path)
{
	GetPrivateProfileStringA("player", "id", "", player_id, sizeof(player_id), path);
	if (is_id(player_id))
		return;
	// RtlGenRandom (advapi32 SystemFunction036), loaded here: no new import for the DLL
	typedef BOOLEAN(WINAPI * gen_random_t)(PVOID, ULONG);
	HMODULE advapi = LoadLibraryA("advapi32.dll");
	gen_random_t gen = advapi ? (gen_random_t)(void *)GetProcAddress(advapi, "SystemFunction036") : NULL;
	uint8_t bytes[16] = {0};
	if (!gen || !gen(bytes, sizeof(bytes))) {
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		uint64_t x = (uint64_t)t.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ GetTickCount();
		for (int i = 0; i < 16; i++) {
			x ^= x << 13, x ^= x >> 7, x ^= x << 17;
			bytes[i] = (uint8_t)x;
		}
	}
	for (int i = 0; i < 16; i++)
		snprintf(player_id + 2 * i, 3, "%02x", bytes[i]);
	if (WritePrivateProfileStringA("player", "id", player_id, path))
		log_printf("settings: new player id written to player.ini");
	else
		log_printf("settings: cannot write the player id to player.ini");
}

void settings_load(void)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\settings.ini");
	UINT p = GetPrivateProfileIntA("network", "port", DEFAULT_PORT, path);
	if (p > 0 && p < 65536)
		port = (uint16_t)p;
	else
		log_printf("settings: bad port %u in settings.ini", p);
	steam_relay = GetPrivateProfileIntA("network", "steam_relay", 0, path) != 0;
	if (port != DEFAULT_PORT)
		patch_port(port);
	lstrcpynA(player_ini, path, MAX_PATH);
	char *slash = strrchr(player_ini, '\\');
	if (slash)
		lstrcpynA(slash + 1, "player.ini", (int)(MAX_PATH - (slash + 1 - player_ini)));
}

// Not in DllMain: making an id loads advapi32, which must not happen under the loader lock.
const char *settings_player_id(void)
{
	if (!player_id[0])
		load_player_id(player_ini);
	return player_id;
}

int settings_steam_relay(void)
{
	return steam_relay;
}

uint16_t settings_port(void)
{
	return port;
}
