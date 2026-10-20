// The mod's own localized strings (&"MW2COOP_..." in scripts). The game draws Cyrillic only from
// its localized strings, which this build keeps in Windows-1251 (COOP_WAITINGFORPLAYER reads
// ce e6 e8 e4 ...): a plain script string with Cyrillic shows as dots. So
// DB_FindXAssetHeader(ASSET_TYPE_LOCALIZE_ENTRY, "MW2COOP_...") is answered here, in Russian
// when the game is Russian (MENU_YES reads "Да"), else in English.
//
// DB_FindXAssetHeader (rva 0x0ef5d0) starts with three 5-byte moves that use no RIP-relative
// address (mov [rsp+8],rbx / mov [rsp+10h],rbp / mov [rsp+18h],rsi); those 15 bytes are run from
// a trampoline that then jumps back to +15.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "localize.h"
#include "log.h"

#define ASSET_TYPE_LOCALIZE_ENTRY 27

typedef struct {
	const char *value;
	const char *name;
} LocalizeEntry;

typedef void *(*db_find_header_t)(int type, const char *name);

static db_find_header_t original;
static int russian = -1;	// -1 unknown yet

typedef struct {
	const char *name;
	const char *ru;	// Windows-1251
	const char *en;
	LocalizeEntry entry;
} ModString;

// "Вы оглушены. Ожидайте второго игрока" in Windows-1251
static ModString strings[] = {
	{"MW2COOP_STUNNED",
	 "\xc2\xfb \xee\xe3\xeb\xf3\xf8\xe5\xed\xfb. \xce\xe6\xe8\xe4\xe0\xe9\xf2\xe5 \xe2\xf2\xee\xf0\xee\xe3\xee "
	 "\xe8\xe3\xf0\xee\xea\xe0",
	 "You are stunned. Wait for the other player", {NULL, NULL}},
};

static int game_is_russian(void)
{
	if (russian < 0) {
		LocalizeEntry *yes = original(ASSET_TYPE_LOCALIZE_ENTRY, "MENU_YES");
		russian = yes && yes->value && (unsigned char)yes->value[0] >= 0xc0;
		log_printf("localize: game language %s", russian ? "Russian" : "not Russian, mod strings in English");
	}
	return russian;
}

static void *find_header(int type, const char *name)
{
	if (type == ASSET_TYPE_LOCALIZE_ENTRY && name && strncmp(name, "MW2COOP_", 8) == 0) {
		for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
			if (strcmp(name, strings[i].name) != 0)
				continue;
			strings[i].entry.name = strings[i].name;
			strings[i].entry.value = game_is_russian() ? strings[i].ru : strings[i].en;
			return &strings[i].entry;
		}
	}
	return original(type, name);
}

static const uint8_t expected[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};

void localize_install(void)
{
	uint8_t *target = (uint8_t *)GetModuleHandleA(NULL) + RVA_DB_FIND_HEADER;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("localize: unexpected bytes at DB_FindXAssetHeader, not hooking");
		return;
	}
	// trampoline: the 15 original bytes, then jmp [rip+0] to target+15
	uint8_t *tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!tramp)
		return;
	memcpy(tramp, target, sizeof(expected));
	uint8_t back[14] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t to = (uint64_t)(uintptr_t)(target + sizeof(expected));
	memcpy(back + 6, &to, 8);
	memcpy(tramp + sizeof(expected), back, sizeof(back));
	FlushInstructionCache(GetCurrentProcess(), tramp, 64);
	original = (db_find_header_t)tramp;

	uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
	to = (uint64_t)(uintptr_t)find_header;
	memcpy(jmp + 6, &to, 8);
	DWORD old;
	VirtualProtect(target, sizeof(expected), PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, jmp, sizeof(jmp));
	target[14] = 0xcc;	// the 15th copied byte is never run: the trampoline resumes at +15
	VirtualProtect(target, sizeof(expected), old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, 15);
	log_printf("localize: DB_FindXAssetHeader hooked for MW2COOP_ strings");
}
