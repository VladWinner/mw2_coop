// A field of view of its own for the weapon in the player's hands: the dvar cg_weaponFov (Vlad, 0.6.0 ideas; the owner's night list 2026-11-23). 0 (the default)
// keeps the game's way: the weapon follows the view's field of view; 65 is the console game's weapon whatever the view's FOV; 1..160 any other.
//
// How the game draws the weapon (static reading of the 64-bit exe, 2026-11-23): one set of view parameters for the scene; the weapon's draws (the view model,
// "depth hack") run in the depth range 0..1/64 with a projection of their own that the game derives from the world's at rva 0x4fd00, the only reader of the
// weapon's near clip (r_znear_depthhack): it copies the view's projection to the source state's +0x100 and, when the depth hack flag (+0x16b8) is 2, puts the
// weapon's near clip in. Every matrix of the weapon's draws (view-projection, world-view-projection, the inverses) is derived from that one by the game's own
// version cache, and the world's draws never go through it. So the weapon gets another field of view by scaling the x and y columns of that matrix by
// k = tan(view fov / 2) / tan(weapon fov / 2) right after the game has made it: on Direct3D 9 and on the mod's own DirectX 11/12 renderers alike (they call the
// game's matrix code). The earlier try (a device hook on SetViewport, 2026-11-04) took the depth range the wrong way round (MinZ > 0 is the world's) and bent
// the world.
//
// Aiming down the sights: the game narrows the view to the weapon's own ADS field of view and the sights must sit where the game puts them, so k goes back to
// 1 as the ADS fraction goes to 1 (k = 1 + (k_hip - 1) * (1 - ads)). Known limits: the muzzle flash and the barrel's smoke are sprites drawn with the
// world's projection, so with a weapon FOV far from the view's they sit off the barrel; the hands of scripted scenes (climbs, the knife fight) are a world
// model and keep the view's FOV.
//
// The hook: 0x4fd00's caller (0x4fd60) goes on using RCX and R8 after the call (the whole-program optimiser's own convention), so the replacement is a small
// machine code stub that keeps every register, calls the original through the trampoline, then the C step with the source state.
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "weaponfov.h"

#ifdef _WIN64
#define REF_WF_PROJECTION 0x4fd00	   // the weapon's projection from the view's (source state in RCX)
#define REF_WF_FOV_DVAR 0x51e1a8	   // dvar_t *cg_fov
#define REF_WF_FOVSCALE_DVAR 0x51e1b8  // dvar_t *cg_fovScale
#define REF_WF_CG 0x40caa0		   // the cgame's state (local client 0); +0x15c60 is the ADS fraction (0..1)
#define REF_WF_REGISTER_FLOAT 0x26cb90 // dvar_t *Dvar_RegisterFloat(name, value, min, max, flags, description)
#define WF_ADS_OFFSET 0x15c60
#define WF_FLAGS 0x16b8			   // the source state's depth hack flag: 2 while the weapon's draws are made
#define WF_MATRIX 0x100			   // the source state's projection (row vectors: clip = v * P)

static uint8_t *base;
static dvar_t *weapon_fov;
static void *original; // the trampoline (the stub calls through this variable)
static volatile LONG scaled_count;
static float last_k = 1.0f;

static float dvar_float(uintptr_t rva_of_pointer, float fallback)
{
	dvar_t *d = *(dvar_t **)(base + rva_of_pointer);
	return d ? d->current.value : fallback;
}

// the render thread (or the game's backend thread): the weapon's projection was just made from the view's
static void post_step(uint8_t *src)
{
	if (!weapon_fov || *(volatile int *)(src + WF_FLAGS) != 2)
		return;
	const float g = weapon_fov->current.value;
	if (g < 1.0f || g > 170.0f)
		return; // 0: the game's way
	const float f = dvar_float(XR(REF_WF_FOV_DVAR), 65.0f) * dvar_float(XR(REF_WF_FOVSCALE_DVAR), 1.0f);
	if (f < 1.0f || f > 170.0f)
		return;
	const float k_hip = tanf(f * 3.14159265f / 360.0f) / tanf(g * 3.14159265f / 360.0f);
	float ads = *(volatile float *)(base + XR(REF_WF_CG) + WF_ADS_OFFSET);
	if (!(ads >= 0.0f && ads <= 1.0f))
		ads = 0.0f;
	float k = 1.0f + (k_hip - 1.0f) * (1.0f - ads);
	if (k < 0.3f)
		k = 0.3f;
	else if (k > 3.0f)
		k = 3.0f;
	float *m = (float *)(src + WF_MATRIX);
	for (int r = 0; r < 4; r++)
		m[r * 4 + 0] *= k, m[r * 4 + 1] *= k;
	last_k = k;
	InterlockedIncrement(&scaled_count);
}

// the stub: push rcx, rdx, r8-r11, rax; xmm0-5 to the stack; call [original]; keep its rax; rcx = the saved one; call post_step; everything back; ret
static uint8_t *make_stub(void)
{
	uint8_t c[192];
	size_t n = 0;
#define B(...)                                                                                                                                                         \
	do {                                                                                                                                                           \
		const uint8_t b_[] = {__VA_ARGS__};                                                                                                                    \
		memcpy(c + n, b_, sizeof(b_));                                                                                                                       \
		n += sizeof(b_);                                                                                                                                      \
	} while (0)
	B(0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x50); // push rcx, rdx, r8, r9, r10, r11, rax (entry rsp = 8 mod 16: now 0 mod 16)
	B(0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00);			    // sub rsp, 0x80 (0x20 home space, 0x60 for xmm0-5)
	B(0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x20);				    // movdqu [rsp+0x20], xmm0
	B(0xf3, 0x0f, 0x7f, 0x4c, 0x24, 0x30);				    // ... xmm1
	B(0xf3, 0x0f, 0x7f, 0x54, 0x24, 0x40);				    // ... xmm2
	B(0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x50);				    // ... xmm3
	B(0xf3, 0x0f, 0x7f, 0x64, 0x24, 0x60);				    // ... xmm4
	B(0xf3, 0x0f, 0x7f, 0x6c, 0x24, 0x70);				    // ... xmm5
	B(0x48, 0xb8);								    // mov rax, &original
	const uint64_t orig_var = (uint64_t)(uintptr_t)&original;
	memcpy(c + n, &orig_var, 8), n += 8;
	B(0xff, 0x10);							    // call [rax] (rcx is still the game's)
	B(0x48, 0x89, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00);			    // mov [rsp+0x80], rax (the pushed rax's slot: the original's result goes back)
	B(0x48, 0x8b, 0x8c, 0x24, 0xb0, 0x00, 0x00, 0x00);			    // mov rcx, [rsp+0xb0] (the pushed rcx)
	B(0x48, 0xb8);								    // mov rax, post_step
	const uint64_t post = (uint64_t)(uintptr_t)post_step;
	memcpy(c + n, &post, 8), n += 8;
	B(0xff, 0xd0);							    // call rax
	B(0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x20);				    // movdqu xmm0, [rsp+0x20]
	B(0xf3, 0x0f, 0x6f, 0x4c, 0x24, 0x30);
	B(0xf3, 0x0f, 0x6f, 0x54, 0x24, 0x40);
	B(0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x50);
	B(0xf3, 0x0f, 0x6f, 0x64, 0x24, 0x60);
	B(0xf3, 0x0f, 0x6f, 0x6c, 0x24, 0x70);
	B(0x48, 0x81, 0xc4, 0x80, 0x00, 0x00, 0x00);			    // add rsp, 0x80
	B(0x58, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59); // pop rax, r11, r10, r9, r8, rdx, rcx
	B(0xc3);								    // ret
#undef B
	return hook_code(c, n);
}

void weaponfov_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!addr_resolved(REF_WF_PROJECTION) || !addr_resolved(REF_WF_FOV_DVAR) || !addr_resolved(REF_WF_FOVSCALE_DVAR) || !addr_resolved(REF_WF_CG)) {
		log_printf("weaponfov: the weapon's projection was not found in this build: no cg_weaponFov");
		return;
	}
	static const uint8_t head[14] = {0x0f, 0x10, 0x81, 0x00, 0x14, 0x00, 0x00, 0x0f, 0x29, 0x81, 0x00, 0x01, 0x00, 0x00}; // movups xmm0,[rcx+0x1400]; movaps [rcx+0x100],xmm0
	uint8_t *stub = make_stub();
	if (!stub || !hook_function(base + XR(REF_WF_PROJECTION), head, sizeof(head), stub, &original)) {
		log_printf("weaponfov: the weapon's projection (0x4fd00) is not the expected code: no cg_weaponFov");
		return;
	}
	log_printf("weaponfov: the weapon's projection goes through the mod (cg_weaponFov)");
}

// the menu tick (main thread): cg_weaponFov once the game's dvars are there; a line in the log when the scale changes
void weaponfov_tick(void)
{
	if (!base || !original)
		return;
	if (!weapon_fov && game_find_dvar("com_maxfps") && addr_resolved(REF_WF_REGISTER_FLOAT)) { // (flags 1: archived, the game saves it in its config)
		dvar_t *(*reg_float)(const char *, float, float, float, unsigned, const char *) = (void *)(base + XR(REF_WF_REGISTER_FLOAT));
		weapon_fov = reg_float("cg_weaponFov", 0.0f, 0.0f, 160.0f, 1,
				       "The weapon's own field of view (0: the weapon follows the view's, as the game draws it; 65: the console game's)");
		log_printf("weaponfov: cg_weaponFov registered (%g)", weapon_fov ? weapon_fov->current.value : -1.0f);
	}
	static float told_k = 1.0f;
	if (fabsf(last_k - told_k) > 0.01f) {
		told_k = last_k;
		log_printf("weaponfov: the weapon's projection scaled by %.3f (%ld scaled so far)", last_k, (long)scaled_count);
	}
}
#else
void weaponfov_install(void)
{
}

void weaponfov_tick(void)
{
}
#endif
