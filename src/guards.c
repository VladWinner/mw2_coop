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

#include "guards.h"
#include "hook.h"
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

void guards_install(void)
{
#ifdef _WIN64
	base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *site = base + 0x9eb1c; // call 0x140085850 (the table lookup)
	if (site[0] != 0xe8 || site + 5 + *(int32_t *)(site + 1) != base + 0x85850) {
		log_printf("guards: weapon definition lookup not guarded (bytes differ)");
		return;
	}
	log_printf("guards: weapon definition lookup %s", hook_call(site, (void *)wdef_hook, (void **)&wdef_original) ? "guarded" : "NOT guarded (no hook page)");
#endif
}
