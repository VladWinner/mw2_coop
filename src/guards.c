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

static volatile LONG cache_generation;

#ifdef _WIN64
static uint8_t *base;
static void *(*wdef_original)(uint64_t);
static struct {
	uint8_t pad[0x2e0];
	void *slots;
} wdef_empty;
static void *wdef_empty_slots[256];

// The two checks below run for every entity of every frame (a big fight: thousands of calls a frame), and a VirtualQuery each cost a third of the frame time there (Invasion's diner:
// 53 and 102 fps with the guards, 144 without). The answer of a query is the whole region (base, size), so the regions found are kept for a millisecond per thread: a pointer inside one
// needs no query. A region that goes away is noticed within that millisecond (the level's teardown is the case these guards are for, and it takes longer).
#define READABLE_SPANS 4
static _Thread_local struct {
	uintptr_t lo[READABLE_SPANS], hi[READABLE_SPANS];
	int n, next;
	LONGLONG stamp;
} spans;
static LONGLONG qpc_ms;

static LONGLONG readable_now(void)
{
	if (!qpc_ms) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		qpc_ms = f.QuadPart / 1000 ? f.QuadPart / 1000 : 1;
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return now.QuadPart;
}

static int readable_at(const void *p, size_t n, LONGLONG now)
{
	if (now - spans.stamp > qpc_ms) {
		spans.n = 0;
		spans.stamp = now;
	}
	uintptr_t from = (uintptr_t)p, to = from + n;
	for (int i = 0; i < spans.n; i++)
		if (from >= spans.lo[i] && to <= spans.hi[i])
			return 1;
	const uint8_t *at = p;
	size_t left = n;
	while (left) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(at, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			return 0;
		size_t room = (size_t)((const uint8_t *)mbi.BaseAddress + mbi.RegionSize - at);
		if (room >= left) {
			if (at == (const uint8_t *)p) { // the whole range in one region: kept
				int slot = spans.n < READABLE_SPANS ? spans.n++ : (spans.next++ % READABLE_SPANS);
				spans.lo[slot] = (uintptr_t)mbi.BaseAddress;
				spans.hi[slot] = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
			}
			return 1;
		}
		at += room;
		left -= room;
	}
	return 1;
}

static int readable(const void *p, size_t n)
{
	return readable_at(p, n, readable_now());
}

// Weapon definitions and their slot arrays are zone assets. Their memory-region mapping stays valid for
// the lifetime of the loaded zones; zones.c advances cache_generation before a level-zone rebuild or
// vid_restart. Unlike the renderer owner check below, this per-entity path therefore needs no clock read
// or 1 ms expiry on every weapon lookup. A cache miss still goes through VirtualQuery before any read.
#define ZONE_READABLE_SPANS 8
static _Thread_local struct {
	uintptr_t lo[ZONE_READABLE_SPANS], hi[ZONE_READABLE_SPANS];
	int n, next;
	uintptr_t last_lo, last_hi;
	LONG generation;
} zone_spans;
static void zone_spans_refresh(void)
{
	LONG generation = __atomic_load_n(&cache_generation, __ATOMIC_RELAXED);
	if (zone_spans.generation != generation) {
		zone_spans.generation = generation;
		zone_spans.n = 0;
		zone_spans.next = 0;
		zone_spans.last_lo = zone_spans.last_hi = 0;
	}
}
static int zone_readable(const void *p, size_t n)
{
	uintptr_t from = (uintptr_t)p, to = from + n;
	if (from >= zone_spans.last_lo && to <= zone_spans.last_hi)
		return 1;
	for (int i = 0; i < zone_spans.n; i++)
		if (from >= zone_spans.lo[i] && to <= zone_spans.hi[i]) {
			zone_spans.last_lo = zone_spans.lo[i];
			zone_spans.last_hi = zone_spans.hi[i];
			return 1;
		}
	const uint8_t *at = p;
	size_t left = n;
	while (left) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(at, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
			return 0;
		size_t room = (size_t)((const uint8_t *)mbi.BaseAddress + mbi.RegionSize - at);
		if (room >= left) {
			if (at == (const uint8_t *)p) {
				int slot = zone_spans.n < ZONE_READABLE_SPANS ? zone_spans.n++ : (zone_spans.next++ % ZONE_READABLE_SPANS);
				zone_spans.lo[slot] = (uintptr_t)mbi.BaseAddress;
				zone_spans.hi[slot] = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
				zone_spans.last_lo = zone_spans.lo[slot];
				zone_spans.last_hi = zone_spans.hi[slot];
			}
			return 1;
		}
		at += room;
		left -= room;
	}
	return 1;
}

static void *wdef_hook(uint64_t index)
{
	void *def = wdef_original(index);
	if (def) {
		zone_spans_refresh();
		// This lookup runs on entity draws. The zone-generation cache avoids a QPC sample and two
		// 1 ms expiry checks per entity; level-zone reload / vid_restart invalidates it explicitly.
		if (zone_readable((const uint8_t *)def + 0x2e0, 8)) { // (zone memory is 4-byte aligned: no alignment test)
			void *slots = *(void **)((const uint8_t *)def + 0x2e0);
			if (slots && zone_readable(slots, sizeof(wdef_empty_slots)))
				return def;
		}
	}
	static LONG logged;
	if (InterlockedIncrement(&logged) <= 8)
		log_printf("guards: the weapon definition of index %llu is %p, not a definition: the entity is skipped", (unsigned long long)index, def);
	wdef_empty.slots = wdef_empty_slots;
	return &wdef_empty;
}
#endif

// Invalidate the renderer-table fast path before level-zone teardown / rebuild. The counter is
// shared across builds; only the official x64 client has the entity-table cache that reads it.
void guards_invalidate_caches(void)
{
	InterlockedIncrement(&cache_generation);
}

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
// adds a weapon / model effect) takes one from. The renderer owner (`g`) and its table pointer are stable between level-zone loads; the global becomes zero when renderer device data
// is gone (the level shut down while the client frame was still adding entities: v0.6.0 crash at iw4sp.exe+0x2a7d, read of 0x168). Cache the table for that lifetime instead of calling
// QueryPerformanceCounter and scanning the readable spans for every entity. Invalidate before a new zone load and vid_restart; a null owner still immediately returns the empty entry.
// A zero entry makes the caller (0x2a90: "if (entry->count == 0) return") do nothing.
static uint8_t empty_entry[0x40];
static _Thread_local LONG table_cache_generation;
static _Thread_local uint8_t *table_cache_owner;
static _Thread_local uint8_t *table_cache;
static intptr_t table_entry_guarded(uint64_t index)
{
	LONG generation = __atomic_load_n(&cache_generation, __ATOMIC_RELAXED);
	if (table_cache_generation != generation) {
		table_cache_generation = generation;
		table_cache_owner = NULL;
		table_cache = NULL;
	}
	uint8_t *g = *(uint8_t **)(base + 0x4115920);
	if (!g) {
		table_cache_owner = NULL;
		table_cache = NULL;
		return (intptr_t)empty_entry;
	}
	if (g != table_cache_owner || !table_cache) {
		table_cache_owner = g;
		table_cache = readable(g + 0x168, 8) ? *(uint8_t **)(g + 0x168) : NULL;
	}
	if (!table_cache)
		return (intptr_t)empty_entry;
	return (intptr_t)(table_cache + (index & 0xffffffffu) * 0x3c);
}
static const uint8_t table_entry_bytes[21] = {0x8b, 0xc1, 0x48, 0x8b, 0x0d, 0xa7, 0x2e, 0x11, 0x04, 0x48, 0x6b, 0xc0, 0x3c, 0x48, 0x03, 0x81, 0x68, 0x01, 0x00, 0x00, 0xc3};
#endif

#ifdef _WIN64
// The same list unlink in the 64-bit game (rva 0x1cb9f0; the node is in rcx, the pointer to the previous link at +0x10, the faulting write at 0x1cba87 `mov [rax], rcx`): a Favela
// test with several failures in a row crashed there three seconds into a restart (thread other than the script's, GSC position maps/_gameskill::playerhealthregen). See the
// iw4x-sp note below; [native] unlinkguard=0 switches it off.
static void (*unlink64_original)(void *);
static void unlink64_guarded(void *node)
{
	if (node && !*(void **)((uint8_t *)node + 0x10)) {
		static LONG seen;
		if (InterlockedIncrement(&seen) < 6)
			log_printf("guards: a list node %p that is not in a list was left alone", node);
		return;
	}
	unlink64_original(node);
}
#endif
#if !defined(_WIN64) && defined(MW2COOP_IW4X)
// The iw4x-sp client (1.0.159 image, rva 0xb2700): the game's unlink of a node from a doubly linked list (next at +4, the pointer to the previous link at +8; called by the function
// at 0xc6e10 that moves an object between two lists under critical section 0x17). A node whose back pointer is null has been unlinked already: the write through it
// (rva 0xb276f, `mov [edx], eax`) faulted, three seconds after "mission failed" when the level was torn down for the restart (a test with several failures in a row, the thread
// that runs the animation scripts, `animscripts/run::updatemoveanimweights`). Such a node is left alone. [native] unlinkguard=0 switches it off.
static void (*unlink_original)(void *);
static void unlink_guarded(void *node)
{
	if (node && !*(void **)((uint8_t *)node + 8)) {
		static LONG seen;
		if (InterlockedIncrement(&seen) < 6)
			log_printf("guards: a list node %p that is not in a list was left alone", node);
		return;
	}
	unlink_original(node);
}
#endif

// The text of the objective pointers ("Follow", "Protect" over a squadmate or a place) and the names over other players are big next to the rest of the HUD (owner, 2026-11-04):
// their font sizes (dvars objectiveFontSize for the labels: [display] hudtextscale percent, default 50; friendlyNameFontSize and its split screen twin for the names: nametagscale,
// default 70) are brought to that part of the game's own, whenever the dvar holds the game's default: a size the player or his config set (different from the default the first time it is seen) is left alone. The game registers these
// dvars again when a level's cgame starts, which puts the default back (a first version that scaled once at the start showed the full-size text in the level: the "2.0m" over the
// weapon table of the Trainer), so the dvars are looked at twice a second for as long as the game runs.
static DWORD WINAPI hud_text_thread(LPVOID unused)
{
	(void)unused;
	static const char *const names[] = {"objectiveFontSize", "friendlyNameFontSize", "friendlyNameFontSizeSplitscreen"};
	const int pcts[3] = {settings_hudtextscale(), settings_nametagscale(), settings_nametagscale()};
	if (pcts[0] == 100 && pcts[1] == 100)
		return 0; // nothing to scale: no thread that looks at the game's dvars for the whole session
	int own[3] = {0, 0, 0}; // the first value seen was not the default: the player's own, left alone
	int seen[3] = {0, 0, 0};
	int logged = 0;
	for (;;) {
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			int pct = pcts[i];
			if (own[i] || pct == 100)
				continue;
			dvar_t *d = game_find_dvar(names[i]);
			if (!d || d->type != DVAR_TYPE_FLOAT || d->reset.value <= 0.0f)
				continue;
			if (!seen[i]) {
				seen[i] = 1;
				if (d->current.value != d->reset.value) {
					own[i] = 1;
					log_printf("guards: %s is %.3f, not the game's %.3f: left alone", names[i], (double)d->current.value, (double)d->reset.value);
					continue;
				}
			}
			if (d->current.value != d->reset.value)
				continue;
			d->current.value = d->reset.value * (float)pct / 100.0f;
			d->modified = 1;
			if (logged++ < 12)
				log_printf("guards: %s %.3f -> %.3f (%d percent)", names[i], (double)d->reset.value, (double)d->current.value, pct);
		}
		Sleep(500);
	}
	return 0;
}

void guards_install(void)
{
	CreateThread(NULL, 0, hud_text_thread, NULL, 0, NULL);
#ifdef _WIN64
	CreateThread(NULL, 0, saved_dvars_thread, NULL, 0, NULL);
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!settings_hotguards()) {
		log_printf("guards: [native] hotguards=0: the entity table and weapon definition lookups are not guarded");
	} else if (!memcmp(base + 0x2a70, table_entry_bytes, sizeof(table_entry_bytes))) {
		hook_jump(base + 0x2a70, sizeof(table_entry_bytes), (const void *)table_entry_guarded, 0xcc);
		log_printf("guards: the entity table lookup at 0x2a70 is guarded");
	} else
		log_printf("guards: the entity table lookup at 0x2a70 is not as expected, not guarded");
	if (settings_unlinkguard()) {
		static const uint8_t unlink_bytes[15] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20}; // mov [rsp+8], rbx; mov [rsp+0x10], rsi; push rdi; sub rsp, 0x20
		log_printf("guards: the list unlink at rva 0x1cb9f0 %s", hook_function(base + 0x1cb9f0, unlink_bytes, sizeof(unlink_bytes), (void *)unlink64_guarded, (void **)&unlink64_original) ? "is guarded" : "is not as expected, not guarded");
	}
	if (!settings_hotguards())
		return;
	uint8_t *site = base + 0x9eb1c; // call 0x140085850 (the table lookup)
	if (site[0] != 0xe8 || site + 5 + *(int32_t *)(site + 1) != base + 0x85850) {
		log_printf("guards: weapon definition lookup not guarded (bytes differ)");
		return;
	}
	log_printf("guards: weapon definition lookup %s", hook_call(site, (void *)wdef_hook, (void **)&wdef_original) ? "guarded" : "NOT guarded (no hook page)");
#endif
#if !defined(_WIN64) && defined(MW2COOP_IW4X)
	if (settings_unlinkguard()) {
		static const uint8_t unlink_bytes[7] = {0x51, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x10}; // push ecx; push ebx; push ebp; mov ebp, [esp+0x10]
		uint8_t *b32 = (uint8_t *)GetModuleHandleA(NULL);
		log_printf("guards: the list unlink at rva 0xb2700 %s", hook_function(b32 + 0xb2700, unlink_bytes, sizeof(unlink_bytes), (void *)unlink_guarded, (void **)&unlink_original) ? "is guarded" : "is not as expected, not guarded");
	}
#endif
}
