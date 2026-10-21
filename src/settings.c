// The player's settings for the mod: <game dir>\mw2coop\settings.ini, read once at start.
//   [network]
//   port=28960    the game's network port: the host listens on it, and joining (an address typed
//                 in without a port, the search on the network) uses it. Both players need the
//                 same port; most never change it.
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
	if (port != DEFAULT_PORT)
		patch_port(port);
}

uint16_t settings_port(void)
{
	return port;
}
