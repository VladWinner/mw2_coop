// The Cliffhanger ice-pick climb swings the right pick with the aim button. The original script read the raw key (ButtonPressed("mouse2")); in co-op the swing is read from the
// player's usercmd instead (AdsButtonPressed, maps\_mw2coop_climb::swing_pressed) so that both machines see the same input. The function that builds the command from the keys
// (64-bit rva 0xcf900, 32-bit VA 0x578c20) sets the aim bit (0x800) and the movement bytes only while the playerstate flag 0x800 (a scripted or linked player) is clear, or the dvar behind
// rva 0x5dacc0 is 2: a climbing player is linked to the wall, so his command carried no right click at all and the right pick did not swing (owner's report, 2026-11-04: clicks lost on the
// client, a fall after a very short release; the attack bit is not behind this gate and is not touched).
// While the climb script polls the swings it writes the dvar mw2coop_keepads (a new value each poll); a value that changed within the last 1.5 s opens the gate here, and the aim bit is put
// back into the command when the key is down, by the same rule the game uses (the key's kbutton is active, equal to the toggle state of the aim: mouse state byte +0 is 0).
// [native] keepads=0 switches it off. Not in the iw4x-sp profile (its addresses are not mapped).
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "climbads.h"
#include "game.h"
#include "hook.h"
#include "settings.h"
#include "log.h"

#define BUTTON_ADS 0x800u
#define PS_LOCKED 0x800u

#ifdef _WIN64
#define RVA_PS_FLAGS 0x535d44 // playerstate flags of local client 0
#define RVA_KB_ADS 0x52fc84   // input block (rva 0x52fbc0) + 0xc4: the active byte of +speed_throw
#define RVA_ADS_STATE 0x533cc0 // mouse state byte +0
#else
#define RVA_PS_FLAGS XR(0x526c64)
#define RVA_KB_ADS XR(0x488a74)
#define RVA_ADS_STATE XR(0x525be0)
#endif

static uint8_t *base;

static int gate_open(void)
{
	static char last[40];
	static DWORD changed;
	char v[40];
	if (!game_dvar_raw("mw2coop_keepads", v, sizeof(v)) || !v[0])
		return 0;
	DWORD now = GetTickCount();
	if (strcmp(v, last)) {
		memcpy(last, v, sizeof(last));
		last[sizeof(last) - 1] = 0;
		changed = now;
	}
	return now - changed < 1500;
}

static void climbads_post(uint8_t *cmd, int lc)
{
	if (lc != 0 || !cmd)
		return;
	uint32_t *buttons = (uint32_t *)(cmd + 4);
	uint32_t ps = *(uint32_t *)(base + RVA_PS_FLAGS);
	if (!(ps & PS_LOCKED) || !gate_open())
		return;
	int key = base[RVA_KB_ADS] != 0, off = base[RVA_ADS_STATE] == 0;
	uint32_t before = *buttons;
	if (key == off)
		*buttons |= BUTTON_ADS;
	static DWORD logged;
	DWORD now = GetTickCount();
	if (now - logged >= 2000) {
		logged = now;
		log_printf("climbads: ps flags %08x, key %d toggle-off %d, command buttons %08x -> %08x", ps, key, off, before, *buttons);
	}
}

#ifdef _WIN64
static void (*climbads_original)(int, uint8_t *);
static void climbads_hook(int lc, uint8_t *cmd)
{
	climbads_original(lc, cmd);
	climbads_post(cmd, lc);
}
#else
void *climbads_orig32;
void climbads_post32(uint8_t *cmd, int lc);
void climbads_post32(uint8_t *cmd, int lc)
{
	climbads_post(cmd, lc);
}
// entry: EDI = the command, [esp + 4] = the local client, plain ret
__asm__(".text\n"
	".globl _climbads_stub32\n"
	"_climbads_stub32:\n"
	"	pushl %edi\n"
	"	pushl 8(%esp)\n"
	"	call *_climbads_orig32\n"
	"	addl $4, %esp\n"
	"	movl (%esp), %edi\n"
	"	pushl 8(%esp)\n"
	"	pushl %edi\n"
	"	call _climbads_post32\n"
	"	addl $8, %esp\n"
	"	popl %edi\n"
	"	ret\n");
extern char climbads_stub32[];
#endif

void climbads_install(void)
{
	if (game_is_iw4x() || !settings_keepads())
		return; // (not mapped for the iw4x-sp client)
	base = (uint8_t *)GetModuleHandleA(NULL);
#ifdef _WIN64
	static const uint8_t bytes[15] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20}; // mov [rsp+0x18], rbx; mov [rsp+0x20], rsi; push rdi; sub rsp, 0x20
	log_printf("climbads: the usercmd aim/movement function at rva 0xcf900 %s", hook_function(base + 0xcf900, bytes, sizeof(bytes), (void *)climbads_hook, (void **)&climbads_original) ? "is watched" : "is not as expected, not watched");
#else
	static const uint8_t bytes[6] = {0x51, 0x55, 0x8b, 0x6c, 0x24, 0x0c}; // push ecx; push ebp; mov ebp, [esp+0xc]
	log_printf("climbads: the usercmd aim/movement function at VA 0x578c20 %s", hook_function(base + XR(0x178c20), bytes, sizeof(bytes), (void *)climbads_stub32, &climbads_orig32) ? "is watched" : "is not as expected, not watched");
#endif
}
