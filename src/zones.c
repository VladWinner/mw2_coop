// The Spec Ops add-on zone of a campaign level, loaded with the level in a co-op session. Players
// have a third-person body only in Spec Ops: the coop_body_* / coop_head_* models, the only ones
// the player animations fit (an AI soldier body set on a player stays in its bind pose), live in
// the so_* zones. Each so_* zone sits on top of a campaign level's zone (so_killspree_trainer on
// trainer) and holds little else (its map entities, its scripts, the Spec Ops table).
//
// The level zone loader (rva 0xf09b0, (map name, flag)) loads a Spec Ops map's own zone and then
// its base level, which it reads from the name (so_<op>_<base>). In a co-op campaign session the
// loader is given the Spec Ops name of the level instead, so both zones load exactly as for that
// Spec Ops map. A level without its own Spec Ops zone gets another level's zone with the same
// uniform after its own (DB_LoadXAssets, rva 0xf0ae0); its few references to that other level's
// assets fall back to defaults. The game's own files only: nothing is shipped.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "hook.h"
#include "log.h"
#include "zones.h"
#ifdef _WIN64
#include "game.h"
#include "transplant.h"

#define RVA_LOAD_LEVEL_ZONES 0x0f09b0
#define RVA_DB_LOAD_XASSETS  0x0f0ae0
#else
// The 32-bit official build (docs/32bit-plan.md): the level zone loader (map, flag) at 0x4d0450 and DB_LoadXAssets at
// 0x49edb0, both cdecl; the model transplant and the pool extension are not ported yet.
#include "game.h"
#include "transplant.h"

#define RVA_LOAD_LEVEL_ZONES 0x0d0450
#define RVA_DB_LOAD_XASSETS  0x09edb0
#endif

typedef struct {
	const char *name;
	uint32_t alloc_flags;
	uint32_t free_flags;
} XZoneInfo;

typedef void (*load_level_zones_t)(const char *map, int flag);
typedef void (*db_load_xassets_t)(XZoneInfo *zones, uint32_t count, int sync);

static load_level_zones_t original;

static const struct {
	const char *level, *spec_ops; // own: the Spec Ops map built on the level
	int own;
} zones[] = {
	{"trainer", "so_killspree_trainer", 1},
	{"invasion", "so_killspree_invasion", 1},
	{"arcadia", "so_download_arcadia", 1},
	{"airport", "so_escape_airport", 1},
	{"favela", "so_killspree_favela", 1},
	{"favela_escape", "so_defuse_favela_escape", 1},
	{"cliffhanger", "so_sabotage_cliffhanger", 1},
	{"contingency", "so_rooftop_contingency", 1},
	{"oilrig", "so_assault_oilrig", 1},
	{"gulag", "so_showers_gulag", 1},
	{"estate", "so_takeover_estate", 1},
	{"boneyard", "so_intel_boneyard", 1},
	// no Spec Ops map of their own: a zone with the same uniform
	{"roadkill", "so_killspree_invasion", 0},
	{"dcburning", "so_killspree_invasion", 0},
	{"dcemp", "so_killspree_invasion", 0},
	{"dc_whitehouse", "so_killspree_invasion", 0},
	{"af_caves", "so_killspree_favela", 0}, // needs more loaded_sound room (pool_extend)
	{"af_chase", "so_killspree_favela", 0},
	{"ending", "so_killspree_favela", 0},
};

// Asset pools. An extra zone on top of a level also needs room in the pools: Just Like Old Times
// (af_caves) with any Spec Ops zone stopped at load with "Exceeded limit of 1350 'loaded_sound'
// assets" (QA 2026-10-21; so_killspree_favela, so_defuse_favela_escape, so_intel_boneyard alike).
// Each pool is { free list head; entries[size] }, a free entry's first 8 bytes point to the next
// free one (the pool alloc at rva 0xeee20: mov rax,[rcx]; mov rdx,[rax]; mov [rcx],rdx), and
// DB_Init (rva 0xeff10) builds the lists from the size table. The asset alloc (rva 0xeee30) fails
// with that message when the list is empty. More entries are linked in front of the list; the
// ones in use stay where they are. The size table is only read for that message and DB_Init.
#ifdef _WIN64
#define RVA_POOL_SIZES 0x3bf930 // int[43]
#define RVA_POOLS      0x3bfe00 // void *[43]
#define ASSET_TYPE_LOADED_SOUND 13

static void pool_extend(int type, int extra, size_t entry_size)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	void **head = ((void ***)(base + RVA_POOLS))[type];
	int *sizes = (int *)(base + RVA_POOL_SIZES);
	uint8_t *more = VirtualAlloc(NULL, (size_t)extra * entry_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!head || !more) {
		log_printf("zones: pool %d not extended", type);
		return;
	}
	for (int i = 0; i < extra; i++)
		*(void **)(more + (size_t)i * entry_size) = i + 1 < extra ? more + (size_t)(i + 1) * entry_size : *head;
	*head = more;
	log_printf("zones: pool %d: %d entries more (%d -> %d)", type, extra, sizes[type], sizes[type] + extra);
	sizes[type] += extra;
}

#endif

static void load_level_zones(const char *map, int flag)
{
	transplant_new_level();
	char coop[8] = "";
	game_dvar_string("coop", coop, sizeof(coop)); // a bool: "0" / "1"
	log_printf("zones: loading level zones for '%s' (coop '%s', flag %d)", map ? map : "", coop, flag);
	if (coop[0] == '1' && map) {
		// once, before the first co-op level's zones: the database thread is idle here
#ifdef _WIN64
		static int extended;
		if (!extended++)
			pool_extend(ASSET_TYPE_LOADED_SOUND, 650, 64); // 64: the pool's span / 1350
#endif

		for (size_t i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
			if (strcmp(map, zones[i].level) != 0)
				continue;
			if (zones[i].own) {
				log_printf("zones: %s with its Spec Ops zone %s (co-op player bodies)", map, zones[i].spec_ops);
				original(zones[i].spec_ops, flag);
				return;
			}
			original(map, flag);
			XZoneInfo extra = {zones[i].spec_ops, 4, 0};
			((db_load_xassets_t)((uint8_t *)GetModuleHandleA(NULL) + RVA_DB_LOAD_XASSETS))(&extra, 1, 0);
			log_printf("zones: %s with %s (co-op player bodies)", map, zones[i].spec_ops);
			return;
		}
	}
	original(map, flag);
}

#ifdef _WIN64
// mov rax,rsp; mov [rax+8],rbx; mov [rax+10h],rbp; mov [rax+18h],rsi; push rdi
static const uint8_t expected[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48,
				   0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57};
#else
// sub esp,10h; push ebx; push ebp; push esi; mov esi,[esp+20h]
static const uint8_t expected[] = {0x83, 0xec, 0x10, 0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x20};
#endif

void zones_install(void)
{
	uint8_t *target = (uint8_t *)GetModuleHandleA(NULL) + RVA_LOAD_LEVEL_ZONES;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("zones: unexpected bytes at the level zone loader, not hooking");
		return;
	}
	if (!hook_function(target, expected, sizeof(expected), load_level_zones, (void **)&original))
		return;
	log_printf("zones: level zone loader hooked");
}
