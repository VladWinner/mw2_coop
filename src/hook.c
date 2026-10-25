// Code patches of the game's functions: a jump over the start of a function (14 bytes absolute in the 64-bit
// build, 5 bytes relative in the 32-bit one, where every address is within reach), and a trampoline that runs
// the function's own first bytes and then goes on with the rest of it.
#include <windows.h>
#include <string.h>

#include "hook.h"

#ifdef _WIN64
#define JUMP_SIZE 14
#else
#define JUMP_SIZE 5
#endif
#define MAX_LEN 32

#ifdef _WIN64
void hook_jump_bytes(uint8_t out[JUMP_SIZE], const void *to)
{
	static const uint8_t jmp_rip[6] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t address = (uint64_t)(uintptr_t)to;
	memcpy(out, jmp_rip, sizeof(jmp_rip));
	memcpy(out + 6, &address, 8);
}
#endif

// The jump instruction as it will stand at `at` (the 32-bit one is relative to that place).
static void jump_bytes(uint8_t out[JUMP_SIZE], const uint8_t *at, const void *to)
{
#ifdef _WIN64
	(void)at;
	hook_jump_bytes(out, to);
#else
	int32_t rel = (int32_t)((uintptr_t)to - ((uintptr_t)at + 5));
	out[0] = 0xe9;
	memcpy(out + 1, &rel, 4);
#endif
}

uint8_t *hook_code(const uint8_t *code, size_t size)
{
	uint8_t *page = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!page)
		return NULL;
	memcpy(page, code, size);
	FlushInstructionCache(GetCurrentProcess(), page, size);
	DWORD old;
	if (!VirtualProtect(page, size, PAGE_EXECUTE_READ, &old)) {
		VirtualFree(page, 0, MEM_RELEASE);
		return NULL;
	}
	return page;
}

void hook_jump(uint8_t *target, size_t len, const void *replacement, uint8_t filler)
{
	if (len < JUMP_SIZE || len > MAX_LEN)
		return;
	uint8_t patch[MAX_LEN];
	memset(patch, filler, len);
	jump_bytes(patch, target, replacement);
	DWORD old;
	VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, patch, len);
	VirtualProtect(target, len, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, len);
}

// The trampoline of hook_function: the first `len` bytes of `target`, then a jump to target + len. It is
// built in place because the 32-bit jump is relative to where it stands.
static uint8_t *make_trampoline(const uint8_t *target, size_t len)
{
	size_t size = len + JUMP_SIZE;
	uint8_t *page = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!page)
		return NULL;
	memcpy(page, target, len);
	jump_bytes(page + len, page + len, target + len);
	FlushInstructionCache(GetCurrentProcess(), page, size);
	DWORD old;
	if (!VirtualProtect(page, size, PAGE_EXECUTE_READ, &old)) {
		VirtualFree(page, 0, MEM_RELEASE);
		return NULL;
	}
	return page;
}

int hook_function(uint8_t *target, const uint8_t *expected, size_t len, void *replacement, void **original)
{
	if (len < JUMP_SIZE || len > MAX_LEN || memcmp(target, expected, len) != 0)
		return 0;
	uint8_t *trampoline = make_trampoline(target, len);
	if (!trampoline)
		return 0;
	*original = trampoline; // before the patch, not after it
	hook_jump(target, len, replacement, 0xcc); // never run: the jump leaves, the trampoline resumes after them
	return 1;
}
