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
// 1 as the view zooms in (k = 1 + (k_hip - 1) * (1 - zoom)). The zoom is the game's own fraction, the one its view's field of view is blended by: the raw
// ADS fraction (the weapon on its way to the eye) remapped by the weapon's zoom-in and zoom-out fractions, so the view starts to narrow only once the weapon
// is part of the way up (64-bit rva 0xc3f70, 32-bit VA 0x41d630: f(cg, weaponDef); every caller passes local client 0's cgame). The mod keeps what that
// getter returned last (a stub around it); before 2026-11-24 the blend followed the raw fraction and the weapon changed size before the view zoomed (owner:
// "do it the way the view zooms", after Vlad's CoD UO weapon FOV, which follows the view's field of view). MW2COOP_WF_RAWADS=1: the raw fraction (QA). Known limits: the muzzle flash and the barrel's smoke are sprites drawn with the
// world's projection, so with a weapon FOV far from the view's they sit off the barrel; the hands of scripted scenes (climbs, the knife fight) are a world
// model and keep the view's FOV.
//
// The hook: 0x4fd00's caller (0x4fd60) goes on using RCX and R8 after the call (the whole-program optimiser's own convention), so the replacement is a small
// machine code stub that keeps every register, calls the original through the trampoline, then the C step with the source state.
//
// The 32-bit game (the official exe and iw4x-sp's 1.0.159, 2026-11-24): the same function at VA 0x541310 (1.0.159 0x541b30), the source state in EAX (the
// optimiser's own convention again): it copies the view's projection from +0x1390 to +0x100 and, when +0x1628 is 2, puts the weapon's near clip (+0x149c) in.
// Its callers are VA 0x541360 (the weapon's matrices, while the flag is 2) and a tail jump from the matrix getter VA 0x5415c0. The ADS fraction is the cgame's
// +0x15c4c (cg VA 0x75f2a0; VA 0x41d630 reads it, the view's field of view at VA 0x440d20 blends with it). cg_fov and cg_fovScale are found by name.
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "native.h"
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
#define REF_WF_ZOOM 0xc3f70		   // float zoom(cg, weaponDef): the raw ADS fraction remapped by the weapon's zoom-in/out fractions (+0x448, +0x44c)
static const uint8_t zoom_head[14] = {0xf3, 0x0f, 0x10, 0x89, 0x60, 0x5c, 0x01, 0x00, 0x0f, 0x57, 0xd2, 0x0f, 0x2f, 0xd1}; // movss xmm1,[rcx+0x15c60]; xorps; comiss
#else
#define REF_WF_PROJECTION 0x141310	   // VA 0x541310: the weapon's projection from the view's (source state in EAX)
#define REF_WF_CG 0x35f2a0		   // VA 0x75f2a0: the cgame's state (local client 0); +0x15c4c is the ADS fraction (0..1)
#define REF_WF_REGISTER_FLOAT 0x9f700  // VA 0x49f700, cdecl: dvar_t *Dvar_RegisterFloat(name, value, min, max, flags, description)
#define WF_ADS_OFFSET 0x15c4c
#define WF_FLAGS 0x1628			   // the source state's depth hack flag: 2 while the weapon's draws are made
#define WF_MATRIX 0x100			   // the source state's projection
#define REF_WF_ZOOM 0x1d630		   // VA 0x41d630, cdecl float zoom(cg, weaponDef) in st0 (the weapon's zoom-in/out fractions at +0x300, +0x304)
static const uint8_t zoom_head[10] = {0x8b, 0x4c, 0x24, 0x04, 0xd9, 0x81, 0x4c, 0x5c, 0x01, 0x00}; // mov ecx,[esp+4]; fld dword [ecx+0x15c4c]
#endif

static uint8_t *base;
static dvar_t *weapon_fov;
static void *original; // the trampoline (the stub calls through this variable)
static volatile LONG scaled_count;
static float last_k = 1.0f;
static void *zoom_original;		 // the zoom getter's trampoline
static volatile float zoom_last;	 // what it returned last (the stub writes it)
static int zoom_hooked;

#ifdef _WIN64
static float dvar_float(uintptr_t rva_of_pointer, float fallback)
{
	dvar_t *d = *(dvar_t **)(base + rva_of_pointer);
	return d ? d->current.value : fallback;
}

static float view_fov(void)
{
	return dvar_float(XR(REF_WF_FOV_DVAR), 65.0f) * dvar_float(XR(REF_WF_FOVSCALE_DVAR), 1.0f);
}
#else
static dvar_t *fov_dvar, *fovscale_dvar; // (found by name on the main thread, weaponfov_tick)

static float view_fov(void)
{
	dvar_t *f = fov_dvar, *s = fovscale_dvar;
	return (f ? f->current.value : 65.0f) * (s ? s->current.value : 1.0f);
}
#endif

// the render thread (or the game's backend thread): the weapon's projection was just made from the view's
static void post_step(uint8_t *src)
{
	if (!weapon_fov || *(volatile int *)(src + WF_FLAGS) != 2)
		return;
	const float g = weapon_fov->current.value;
	if (g < 1.0f || g > 170.0f)
		return; // 0: the game's way
	const float f = view_fov();
	if (f < 1.0f || f > 170.0f)
		return;
	const float k_hip = tanf(f * 3.14159265f / 360.0f) / tanf(g * 3.14159265f / 360.0f);
	float ads = *(volatile float *)(base + XR(REF_WF_CG) + WF_ADS_OFFSET);
	if (!(ads >= 0.0f && ads <= 1.0f))
		ads = 0.0f;
	float zoom = ads;
	if (zoom_hooked && ads > 0.0f && ads < 1.0f && !native_split_active()) { // (the split screen: both players ask the getter in turn, its last answer may be the other's: the raw fraction there)
		// (at 0 and 1 the getter gives 0 and 1 itself; between them its last answer, this frame's: the view's field of view asks it)
		zoom = zoom_last;
		if (!(zoom >= 0.0f && zoom <= 1.0f))
			zoom = ads;
	}
	float k = 1.0f + (k_hip - 1.0f) * (1.0f - zoom);
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

#ifdef _WIN64
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
#else
// the stub: the original runs with the game's EAX; its EAX, ECX and EDX are kept for the caller; the x87 and SSE state is put aside (fxsave) and the
// x87 stack emptied for the C step (the callers are x87 code that may keep values on that stack across the call); post_step(src); all of it back; ret
static uint8_t *make_stub(void)
{
	uint8_t c[96];
	size_t n = 0;
#define B(...)                                                                                                                                                         \
	do {                                                                                                                                                           \
		const uint8_t b_[] = {__VA_ARGS__};                                                                                                                    \
		memcpy(c + n, b_, sizeof(b_));                                                                                                                       \
		n += sizeof(b_);                                                                                                                                      \
	} while (0)
	B(0x55);				 // push ebp
	B(0x89, 0xe5);				 // mov ebp, esp
	B(0x50);				 // push eax ([ebp-4]: the source state)
	B(0xff, 0x15);				 // call [&original]
	const uint32_t orig_var = (uint32_t)(uintptr_t)&original;
	memcpy(c + n, &orig_var, 4), n += 4;
	B(0x50, 0x51, 0x52);			 // push eax, ecx, edx (the original's, for the caller)
	B(0x81, 0xec, 0x10, 0x02, 0x00, 0x00);	 // sub esp, 0x210
	B(0x83, 0xe4, 0xf0);			 // and esp, -16
	B(0x0f, 0xae, 0x04, 0x24);		 // fxsave [esp]
	B(0xdb, 0xe3);				 // fninit
	B(0xff, 0x75, 0xfc);			 // push dword [ebp-4]
	B(0xb8);				 // mov eax, post_step
	const uint32_t post = (uint32_t)(uintptr_t)post_step;
	memcpy(c + n, &post, 4), n += 4;
	B(0xff, 0xd0);				 // call eax
	B(0x83, 0xc4, 0x04);			 // add esp, 4
	B(0x0f, 0xae, 0x0c, 0x24);		 // fxrstor [esp]
	B(0x8d, 0x65, 0xf0);			 // lea esp, [ebp-0x10]
	B(0x5a, 0x59, 0x58);			 // pop edx, ecx, eax
	B(0xc9);				 // leave
	B(0xc3);				 // ret
#undef B
	return hook_code(c, n);
}
#endif

// the zoom getter's stub: the original runs (through the trampoline), its answer is kept in zoom_last and returned; every register is the original's
#ifdef _WIN64
static uint8_t *make_zoom_stub(void)
{
	uint8_t c[64];
	size_t n = 0;
#define B(...)                                                                                                                                                         \
	do {                                                                                                                                                           \
		const uint8_t b_[] = {__VA_ARGS__};                                                                                                                    \
		memcpy(c + n, b_, sizeof(b_));                                                                                                                       \
		n += sizeof(b_);                                                                                                                                      \
	} while (0)
	B(0x50);			    // push rax (entry rsp = 8 mod 16: now 0)
	B(0x48, 0x83, 0xec, 0x20);	    // sub rsp, 0x20
	B(0x48, 0xb8);			    // mov rax, &zoom_original
	const uint64_t orig_var = (uint64_t)(uintptr_t)&zoom_original;
	memcpy(c + n, &orig_var, 8), n += 8;
	B(0xff, 0x10);			    // call [rax] (rcx, rdx are the caller's)
	B(0x48, 0x83, 0xc4, 0x20);	    // add rsp, 0x20
	B(0x48, 0xb8);			    // mov rax, &zoom_last
	const uint64_t last_var = (uint64_t)(uintptr_t)&zoom_last;
	memcpy(c + n, &last_var, 8), n += 8;
	B(0xf3, 0x0f, 0x11, 0x00);	    // movss [rax], xmm0
	B(0x58);			    // pop rax
	B(0xc3);			    // ret
#undef B
	return hook_code(c, n);
}
#else
static uint8_t *make_zoom_stub(void)
{
	uint8_t c[48];
	size_t n = 0;
#define B(...)                                                                                                                                                         \
	do {                                                                                                                                                           \
		const uint8_t b_[] = {__VA_ARGS__};                                                                                                                    \
		memcpy(c + n, b_, sizeof(b_));                                                                                                                       \
		n += sizeof(b_);                                                                                                                                      \
	} while (0)
	B(0xff, 0x74, 0x24, 0x08);	    // push dword [esp+8] (weaponDef)
	B(0xff, 0x74, 0x24, 0x08);	    // push dword [esp+8] (cg)
	B(0xff, 0x15);			    // call [&zoom_original]
	const uint32_t orig_var = (uint32_t)(uintptr_t)&zoom_original;
	memcpy(c + n, &orig_var, 4), n += 4;
	B(0x83, 0xc4, 0x08);		    // add esp, 8
	B(0xd9, 0x15);			    // fst dword [&zoom_last] (st0 stays the answer)
	const uint32_t last_var = (uint32_t)(uintptr_t)&zoom_last;
	memcpy(c + n, &last_var, 4), n += 4;
	B(0xc3);			    // ret
#undef B
	return hook_code(c, n);
}
#endif

static void zoom_install(void)
{
	if (getenv("MW2COOP_WF_RAWADS")) {
		log_printf("weaponfov: the ADS blend follows the raw ADS fraction (MW2COOP_WF_RAWADS)");
		return;
	}
	uint8_t *stub;
	if (!addr_resolved(REF_WF_ZOOM) || !(stub = make_zoom_stub()) ||
	    !hook_function(base + XR(REF_WF_ZOOM), zoom_head, sizeof(zoom_head), stub, &zoom_original)) {
		log_printf("weaponfov: the game's ADS zoom fraction (rva 0x%x) was not found: the blend follows the raw ADS fraction", (unsigned)REF_WF_ZOOM);
		return;
	}
	zoom_hooked = 1;
	log_printf("weaponfov: the ADS blend follows the game's zoom fraction");
}

void weaponfov_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
#ifdef _WIN64
	if (!addr_resolved(REF_WF_PROJECTION) || !addr_resolved(REF_WF_FOV_DVAR) || !addr_resolved(REF_WF_FOVSCALE_DVAR) || !addr_resolved(REF_WF_CG)) {
		log_printf("weaponfov: the weapon's projection was not found in this build: no cg_weaponFov");
		return;
	}
	static const uint8_t head[14] = {0x0f, 0x10, 0x81, 0x00, 0x14, 0x00, 0x00, 0x0f, 0x29, 0x81, 0x00, 0x01, 0x00, 0x00}; // movups xmm0,[rcx+0x1400]; movaps [rcx+0x100],xmm0
#else
	if (!addr_resolved(REF_WF_PROJECTION) || !addr_resolved(REF_WF_CG)) {
		log_printf("weaponfov: the weapon's projection was not found in this build: no cg_weaponFov");
		return;
	}
	static const uint8_t head[14] = {0x56, 0x57, 0x8d, 0x90, 0x00, 0x01, 0x00, 0x00, 0x8d, 0xb0, 0x90, 0x13, 0x00, 0x00}; // push esi; push edi; lea edx,[eax+0x100]; lea esi,[eax+0x1390]
#endif
	uint8_t *stub = make_stub();
	if (!stub || !hook_function(base + XR(REF_WF_PROJECTION), head, sizeof(head), stub, &original)) {
		log_printf("weaponfov: the weapon's projection (rva 0x%x) is not the expected code: no cg_weaponFov", (unsigned)REF_WF_PROJECTION);
		return;
	}
	log_printf("weaponfov: the weapon's projection goes through the mod (cg_weaponFov)");
	zoom_install();
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
#ifndef _WIN64
	if (!fov_dvar)
		fov_dvar = game_find_dvar("cg_fov");
	if (!fovscale_dvar)
		fovscale_dvar = game_find_dvar("cg_fovScale");
#endif
	static float told_k = 1.0f;
	if (fabsf(last_k - told_k) > 0.01f) {
		told_k = last_k;
		log_printf("weaponfov: the weapon's projection scaled by %.3f (%ld scaled so far)", last_k, (long)scaled_count);
	}
}
