// Script runtime errors. The retail Scr_Error (rva 0x2226a0) drops the message and only
// longjmps out of the failing script thread:
//   if (!flag_a && !flag_b && depth) longjmp(&jmpbufs[index], -1);
// The message was formatted with va() just before, into one of two 1 KB slots of a
// thread-local block (va @0x273440 -> block = 0x203700(1); slot index at block+0x800).
// Our replacement logs both slots, then does exactly what the original does, with the game's
// own longjmp (rva 0x2e485c) on the game's own jmp_buf.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "scrdebug.h"

#define RVA_SCR_ERROR        0x2226a0
#define RVA_SCR_ERR_FLAG_A   0x15a038b // uint8
#define RVA_SCR_ERR_FLAG_B   0x1463bce // uint8
#define RVA_SCR_ERR_DEPTH    0x18f44a0 // int32
#define RVA_SCR_JMPBUF_INDEX 0x18fca44 // int32
#define RVA_SCR_JMPBUFS      0x18fca50 // jmp_buf[], 0x100 bytes each
#define RVA_LONGJMP          0x2e485c  // the game's CRT longjmp
#define RVA_TLS_BLOCK        0x203700  // void *Sys_GetValue(int)

static uint8_t *base;
static volatile LONG budget = 60;

static void scr_error(void)
{
	if (base[RVA_SCR_ERR_FLAG_A] || base[RVA_SCR_ERR_FLAG_B] || !*(int32_t *)(base + RVA_SCR_ERR_DEPTH))
		return;
	if (InterlockedDecrement(&budget) >= 0) {
		const char *block = ((const char *(*)(int))(base + RVA_TLS_BLOCK))(1);
		int slot = *(const int32_t *)(block + 0x800);
		char last[200], prev[200];
		lstrcpynA(last, block + (slot ^ 1) * 0x400, sizeof(last));
		lstrcpynA(prev, block + slot * 0x400, sizeof(prev));
		log_printf("SCRIPT ERROR: %s   (older va: %s)", last, prev);
	}
	int index = *(int32_t *)(base + RVA_SCR_JMPBUF_INDEX);
	((void (*)(void *, int))(base + RVA_LONGJMP))(base + RVA_SCR_JMPBUFS + (int64_t)index * 0x100, -1);
}

static const uint8_t expected[] = {0x48, 0x83, 0xec, 0x28, 0x80, 0x3d, 0xe0, 0xdc, 0x37, 0x01, 0x00};

void scrdebug_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *target = base + RVA_SCR_ERROR;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("scrdebug: unexpected bytes at Scr_Error, not hooking");
		return;
	}
	uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t to = (uint64_t)(uintptr_t)scr_error;
	memcpy(jmp + 6, &to, 8);
	DWORD old;
	VirtualProtect(target, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, jmp, sizeof(jmp));
	VirtualProtect(target, sizeof(jmp), old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, sizeof(jmp));
	log_printf("scrdebug: Scr_Error replaced (runtime script errors are logged)");
}
