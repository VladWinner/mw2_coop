// Level scripts equip level.player only. Weapons reach player 2 through the scripts' weapon sync
// (maps\_mw2coop::sync_weapons); the rest of a loadout is set with player methods that scripts
// cannot read back, so it is mirrored here: when a script calls one of these methods on player 1
// in a co-op campaign session, the same call (same arguments, still on the script stack) runs
// on player 2 right after. Owner (2026-10-21): "look at what every mission gives the player and
// give the partner the same". maps\_loadout and the levels use them for:
//   setactionslot             night vision, claymore, C4, Predator and Cobra on the D-pad
//   setoffhandsecondaryclass  flash or smoke as the second grenade
//   givemaxammo, givestartammo  ammo refills (they skip a weapon the player does not have)
// Mirrored one way, player 1 to player 2, as the weapon sync. Both lockstep machines run the
// same scripts, so both mirror the same calls.
//
// A method builtin is {const char *name; void (*func)(scr_entref_t); int developer} (0x18
// bytes, table in .rdata). scr_entref_t comes in ecx: entity number in the low 16 bits, class
// in the high 16 (0 = entity). g_entities: rva 0xd9e070, 0x2a0 bytes each, gclient_t * at +0x108.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "mirror.h"

#define RVA_G_ENTITIES 0xd9e070
#define GENTITY_SIZE 0x2a0
#define GENTITY_CLIENT 0x108

typedef void (*method_t)(uint32_t entref);

static uint8_t *base;

static const struct {
	uint32_t rva; // the table entry
	const char *name;
} mirrored[] = {
	{0x339668, "setactionslot"},
	{0x339308, "setoffhandsecondaryclass"},
	{0x339110, "givemaxammo"},
	{0x3390f8, "givestartammo"},
};
#define MIRRORED (sizeof(mirrored) / sizeof(mirrored[0]))

static method_t originals[MIRRORED];

static int is_client(int entnum)
{
	return *(void **)(base + RVA_G_ENTITIES + (size_t)entnum * GENTITY_SIZE + GENTITY_CLIENT) != NULL;
}

static int campaign_coop(void)
{
	char coop[8], map[64];
	if (!game_dvar_raw("coop", coop, sizeof(coop)) || strcmp(coop, "1") != 0)
		return 0;
	return game_dvar_raw("mapname", map, sizeof(map)) && _strnicmp(map, "so_", 3) != 0;
}

static void call(int i, uint32_t entref)
{
	originals[i](entref);
	if (entref == 0 && is_client(1) && campaign_coop()) {
		originals[i](1);
		log_printf("mirror: %s also on player 2", mirrored[i].name);
	}
}

#define WRAP(i) \
	static void wrap##i(uint32_t entref) { call(i, entref); }
WRAP(0)
WRAP(1)
WRAP(2)
WRAP(3)
static const method_t wrappers[MIRRORED] = {wrap0, wrap1, wrap2, wrap3};

void mirror_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	int done = 0;
	for (size_t i = 0; i < MIRRORED; i++) {
		uint8_t *entry = base + mirrored[i].rva;
		const char *name = *(const char **)entry;
		if (!name || strcmp(name, mirrored[i].name) != 0) {
			log_printf("mirror: no %s at rva 0x%x, player 2 does not get it", mirrored[i].name, mirrored[i].rva);
			continue;
		}
		method_t *func = (method_t *)(entry + 8);
		originals[i] = *func;
		DWORD old;
		VirtualProtect(func, sizeof(*func), PAGE_READWRITE, &old);
		*func = wrappers[i];
		VirtualProtect(func, sizeof(*func), old, &old);
		done++;
	}
	log_printf("mirror: %d player methods also equip player 2 in co-op", done);
}
