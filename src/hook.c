// Code patches of the game's functions: a 14-byte absolute jump over the start of a function, and a
// trampoline that runs the function's own first bytes and then goes on with the rest of it.
#include <windows.h>
#include <string.h>

#include "hook.h"

#define JUMP_SIZE 14
#define MAX_LEN 32

void hook_jump_bytes(uint8_t out[JUMP_SIZE], const void *to)
{
	static const uint8_t jmp_rip[6] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t address = (uint64_t)(uintptr_t)to;
	memcpy(out, jmp_rip, sizeof(jmp_rip));
	memcpy(out + 6, &address, 8);
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
	hook_jump_bytes(patch, replacement);
	DWORD old;
	VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, patch, len);
	VirtualProtect(target, len, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, len);
}

void *hook_function(uint8_t *target, const uint8_t *expected, size_t len, void *replacement)
{
	if (len < JUMP_SIZE || len > MAX_LEN || memcmp(target, expected, len) != 0)
		return NULL;
	uint8_t code[MAX_LEN + JUMP_SIZE];
	memcpy(code, target, len);
	hook_jump_bytes(code + len, target + len);
	uint8_t *trampoline = hook_code(code, len + JUMP_SIZE);
	if (!trampoline)
		return NULL;
	hook_jump(target, len, replacement, 0xcc); // never run: the jump leaves, the trampoline resumes after them
	return trampoline;
}
