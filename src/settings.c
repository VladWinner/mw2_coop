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
#include "voicefix.h"

#define DEFAULT_PORT 28960
static uint16_t port = DEFAULT_PORT;
static int steam_relay;
static int fonts_on = 1;
static int fontmip_on = 1;
static int darktitle_on = 1;
static int hwgamma_on = 1;
static int unlinkguard_on = 1;
static int hotguards_on = 1;
static int hudtextscale = 50;
static int nametagscale = 70;
static int keepads_on = 1;
static int d3d9ex_on = 0;
static int borderless_on = 1;
static int netguard_on = 1;
static int d3d9on12_on = 0;
static int d3d9on12_adapter = -1;
static int fsr3_on;
static char fsr3_dir_buf[MAX_PATH];
static int upscale_on, upscale_w, upscale_h, upscale_sharp = 25, fsr3_sharp = 60;
static int gp_fall_damage = -1, gp_jump_height, gp_gravity;
static char discord_id[32];
static char player_id[33];
static char player_ini[MAX_PATH];

#ifdef _WIN64
static const uint32_t net_port_immediates[] = {0x293381, 0x292f03}; // "mov edx, 28960": ba 20 71 00 00
#define PORT_OPCODE 0xba
#else
// the 32-bit build registers net_port with "push 28960" (68 20 71 00 00) before "push 'net_port'" (0x48c5f6, 0x63908e)
#ifdef MW2COOP_IW4X
static const uint32_t net_port_immediates[] = {0x94e27, 0x23b87f}; // the 1.0.159 exe of iw4x-sp
#else
static const uint32_t net_port_immediates[] = {0x8c5f7, 0x23908f};
#endif
#define PORT_OPCODE 0x68
#endif

static void patch_port(uint16_t value)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	for (size_t i = 0; i < sizeof(net_port_immediates) / sizeof(net_port_immediates[0]); i++) {
		uint8_t *at = base + net_port_immediates[i];
		if (at[-1] != PORT_OPCODE || *(uint32_t *)at != DEFAULT_PORT) {
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
	netguard_on = GetPrivateProfileIntA("network", "guard", 1, path) != 0;
	d3d9on12_on = GetPrivateProfileIntA("display", "d3d9on12", 0, path) != 0;
	d3d9on12_adapter = (int)GetPrivateProfileIntA("display", "d3d9on12_adapter", -1, path);
	fsr3_on = GetPrivateProfileIntA("display", "fsr3", 0, path) != 0;
	GetPrivateProfileStringA("display", "fsr3_dir", "", fsr3_dir_buf, sizeof(fsr3_dir_buf), path);
	fsr3_sharp = (int)GetPrivateProfileIntA("display", "fsr3_sharpness", 60, path);
	if (fsr3_sharp < 0 || fsr3_sharp > 100)
		fsr3_sharp = 60;
	if (fsr3_on)
		d3d9on12_on = 1; // FSR 3 runs on the Direct3D 12 layer
	upscale_on = GetPrivateProfileIntA("display", "upscale", 0, path) != 0;
	upscale_w = (int)GetPrivateProfileIntA("display", "upscale_width", 0, path);
	upscale_h = (int)GetPrivateProfileIntA("display", "upscale_height", 0, path);
	upscale_sharp = (int)GetPrivateProfileIntA("display", "upscale_sharpness", 25, path);
	if (upscale_sharp < 0 || upscale_sharp > 300)
		upscale_sharp = 25;
	fonts_on = GetPrivateProfileIntA("display", "fonts", 1, path) != 0;
	fontmip_on = GetPrivateProfileIntA("display", "fontmip", 1, path) != 0;
	darktitle_on = GetPrivateProfileIntA("display", "darktitle", 1, path) != 0;
	hwgamma_on = GetPrivateProfileIntA("display", "hwgamma", 1, path) != 0;
	unlinkguard_on = GetPrivateProfileIntA("native", "unlinkguard", 1, path) != 0;
	hotguards_on = GetPrivateProfileIntA("native", "hotguards", 1, path) != 0;
	keepads_on = GetPrivateProfileIntA("native", "keepads", 1, path) != 0;
	d3d9ex_on = GetPrivateProfileIntA("display", "d3d9ex", 0, path) != 0;
	borderless_on = GetPrivateProfileIntA("display", "borderless", 1, path) != 0;
	hudtextscale = (int)GetPrivateProfileIntA("display", "hudtextscale", 50, path);
	if (hudtextscale < 20 || hudtextscale > 200)
		hudtextscale = 50;
	nametagscale = (int)GetPrivateProfileIntA("display", "nametagscale", 70, path);
	if (nametagscale < 20 || nametagscale > 200)
		nametagscale = 70;
	// [gameplay]: options of the session, the same dvars the console sets (mw2coop_fall_damage, mw2coop_jump_height, mw2coop_gravity); the host's values go to the client with the launch
	gp_fall_damage = (int)GetPrivateProfileIntA("gameplay", "fall_damage", -1, path);
	if (gp_fall_damage != 0 && gp_fall_damage != 1)
		gp_fall_damage = -1;
	gp_jump_height = (int)GetPrivateProfileIntA("gameplay", "jump_height", 0, path);
	if (gp_jump_height < 0 || gp_jump_height > 1000)
		gp_jump_height = 0;
	gp_gravity = (int)GetPrivateProfileIntA("gameplay", "gravity", 0, path);
	if (gp_gravity < 0 || gp_gravity > 5000)
		gp_gravity = 0;
	GetPrivateProfileStringA("discord", "client_id", "", discord_id, sizeof(discord_id), path);
	for (char *c = discord_id; *c; c++)
		if (*c < '0' || *c > '9') {
			discord_id[0] = 0;
			break;
		}
	log_printf("settings: %s port %u, steam_relay %d, fonts %d, fontmip %d", path, (unsigned)port, steam_relay, fonts_on, fontmip_on);
	voicefix_apply(path);
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

int settings_fonts(void)
{
	return fonts_on;
}

int settings_fontmip(void)
{
	return fontmip_on;
}

int settings_darktitle(void)
{
	return darktitle_on;
}

int settings_hwgamma(void)
{
	return hwgamma_on;
}

int settings_hotguards(void)
{
	return hotguards_on;
}

int settings_unlinkguard(void)
{
	return unlinkguard_on;
}

int settings_keepads(void)
{
	return keepads_on;
}

int settings_d3d9ex(void)
{
	return d3d9ex_on;
}

int settings_d3d9on12_adapter(void)
{
	return d3d9on12_adapter;
}

int settings_upscale(void)
{
	return upscale_on;
}

int settings_upscale_width(void)
{
	return upscale_w;
}

int settings_upscale_height(void)
{
	return upscale_h;
}

int settings_upscale_sharpness(void)
{
	return upscale_sharp;
}

int settings_fsr3_sharpness(void)
{
	return fsr3_sharp;
}

int settings_fsr3(void)
{
	return fsr3_on;
}

const char *settings_fsr3_dir(void)
{
	return fsr3_dir_buf;
}

int settings_d3d9on12(void)
{
	return d3d9on12_on;
}

int settings_netguard(void)
{
	return netguard_on;
}

int settings_borderless(void)
{
	return borderless_on;
}

int settings_gameplay_fall_damage(void)
{
	return gp_fall_damage;
}

int settings_gameplay_jump_height(void)
{
	return gp_jump_height;
}

int settings_gameplay_gravity(void)
{
	return gp_gravity;
}

const char *settings_discord_id(void)
{
	return discord_id;
}

int settings_hudtextscale(void)
{
	return hudtextscale;
}

int settings_nametagscale(void)
{
	return nametagscale;
}

uint16_t settings_port(void)
{
	return port;
}
