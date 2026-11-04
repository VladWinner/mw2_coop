// Guards against crashes seen in the field (64-bit game): the game's own code trusts a pointer the mod cannot always make true.
//
// Weapon definition of a predicted entity (rva 0x9ea20, the cgame's per local client entity switch; its case for entity type 2 at 0x9eaff): the weapon index
// (word at +0x164, modulo 200) goes through the table of weapon definition pointers (rva 0x3ee320, the function at 0x85850) and the entity is drawn from
// def->slots[(index / 200) & 0xff] (+0x2e0). v0.6.0 report (player 2 picks a weapon up, the change of weapon goes on): the table held a pointer that was not a
// definition (an unaligned heap address), the read of def+0x2e0 faulted at rva 0x9eb2b. The lookup at the call site 0x9eb1c is wrapped: a result whose slot
// array cannot be read is replaced by an empty definition (its slots are all null: the entity is skipped, as the game does for a weapon without a model), and the
// index is logged, which is what is still missing to find out how the table got that value.
#include <windows.h>
#include <stdint.h>

#include "game.h"
#include "guards.h"
#include "hook.h"
#include "settings.h"
#include "log.h"

#ifdef _WIN64
static uint8_t *base;
static void *(*wdef_original)(uint64_t);
static struct {
	uint8_t pad[0x2e0];
	void *slots;
} wdef_empty;
static void *wdef_empty_slots[256];

static int readable(const void *p, size_t n)
{
	const uint8_t *at = p;
	while (n) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(at, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			return 0;
		size_t room = (size_t)((const uint8_t *)mbi.BaseAddress + mbi.RegionSize - at);
		if (room >= n)
			return 1;
		at += room;
		n -= room;
	}
	return 1;
}

static void *wdef_hook(uint64_t index)
{
	void *def = wdef_original(index);
	if (def && readable((const uint8_t *)def + 0x2e0, 8)) { // (zone memory is 4-byte aligned: no alignment test)
		void *slots = *(void **)((const uint8_t *)def + 0x2e0);
		if (slots && readable(slots, sizeof(wdef_empty_slots)))
			return def;
	}
	static LONG logged;
	if (InterlockedIncrement(&logged) <= 8)
		log_printf("guards: the weapon definition of index %llu is %p, not a definition: the entity is skipped", (unsigned long long)index, def);
	wdef_empty.slots = wdef_empty_slots;
	return &wdef_empty;
}
#endif


// Dvars the game does not keep in its config but should (v0.6.0 report): the ARCHIVE flag (bit 0) makes the config writer save them. com_maxfps is registered early; the lookup
// is repeated for a while in case it is not there yet when the mod starts.
static DWORD WINAPI saved_dvars_thread(LPVOID unused)
{
	(void)unused;
	if (!settings_hwgamma()) { // [display] hwgamma=0: the hardware gamma ramp of exclusive full screen is ignored (r_ignorehwgamma), as the windowed modes have none
		for (int tries = 0; tries < 40; tries++) {
			dvar_t *g = game_find_dvar("r_ignorehwgamma");
			if (g) {
				g->current.enabled = 1;
				g->modified = 1;
				log_printf("guards: r_ignorehwgamma set (hwgamma=0)");
				break;
			}
			Sleep(500);
		}
	}
	static const char *const names[] = {"com_maxfps"};
	for (int tries = 0; tries < 40; tries++) {
		int missing = 0;
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			dvar_t *d = game_find_dvar(names[i]);
			if (!d) {
				missing++;
				continue;
			}
			if (!(d->flags & 1)) {
				d->flags |= 1;
				log_printf("guards: dvar %s is saved now (archive flag)", names[i]);
			}
		}
		if (!missing)
			return 0;
		Sleep(500);
	}
	return 0;
}

#ifdef _WIN64
// rva 0x2a70: (index & 0xffffffff) * 0x3c + *(*(base + 0x4115920) + 0x168), the table of 0x3c-byte entries that the entity code (0x9ed44: the case of the per-client entity switch that
// adds a weapon / model effect) takes one from. The global is zero once the renderer's device data is gone (the level shut down while the client frame was still adding entities:
// v0.6.0 crash at iw4sp.exe+0x2a7d, read of 0x168). A zero entry makes the caller (0x2a90: "if (entry->count == 0) return") do nothing.
static uint8_t empty_entry[0x40];
static intptr_t table_entry_guarded(uint64_t index)
{
	uint8_t *g = *(uint8_t **)(base + 0x4115920);
	uint8_t *table = g && !IsBadReadPtr(g + 0x168, 8) ? *(uint8_t **)(g + 0x168) : NULL;
	if (!table)
		return (intptr_t)empty_entry;
	return (intptr_t)(table + (index & 0xffffffffu) * 0x3c);
}
static const uint8_t table_entry_bytes[21] = {0x8b, 0xc1, 0x48, 0x8b, 0x0d, 0xa7, 0x2e, 0x11, 0x04, 0x48, 0x6b, 0xc0, 0x3c, 0x48, 0x03, 0x81, 0x68, 0x01, 0x00, 0x00, 0xc3};
#endif

void guards_install(void)
{
	CreateThread(NULL, 0, saved_dvars_thread, NULL, 0, NULL);
#ifdef _WIN64
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!memcmp(base + 0x2a70, table_entry_bytes, sizeof(table_entry_bytes))) {
		hook_jump(base + 0x2a70, sizeof(table_entry_bytes), (const void *)table_entry_guarded, 0xcc);
		log_printf("guards: the entity table lookup at 0x2a70 is guarded");
	} else
		log_printf("guards: the entity table lookup at 0x2a70 is not as expected, not guarded");
	uint8_t *site = base + 0x9eb1c; // call 0x140085850 (the table lookup)
	if (site[0] != 0xe8 || site + 5 + *(int32_t *)(site + 1) != base + 0x85850) {
		log_printf("guards: weapon definition lookup not guarded (bytes differ)");
		return;
	}
	log_printf("guards: weapon definition lookup %s", hook_call(site, (void *)wdef_hook, (void **)&wdef_original) ? "guarded" : "NOT guarded (no hook page)");
#endif
}
