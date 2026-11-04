// A field of view of its own for the weapon in the player's hands ([native] weaponfov=<degrees>, 0 = off: the weapon follows cg_fov as the game draws it, which is also how the
// Xbox 360 game does it).
//
// The game draws the first-person weapon in the main scene, with the world's projection, in a depth range of its own (the "depth hack": a viewport with a different MinZ / MaxZ,
// so the weapon does not go into walls). Its vertex shaders (compiled HLSL, with a constant table) take the matrix that goes from the model to the clip space in a constant named
// worldViewProjectionMatrix (or similar). A different field of view for the weapon is the same picture with x and y of the clip space scaled by
// tan(view fov / 2) / tan(weapon fov / 2): the rows of that matrix that make clip x and y are scaled while the draw is made in the weapon's depth range.
//
// This file hooks three methods of the device: SetViewport (the depth range), SetVertexShader (which constants the shader has: its constant table is read once) and
// SetVertexShaderConstantF (the matrix is scaled on its way). The numbers the scale needs: the view's tan(fov / 2) the game works with (read through the pointer given at install).
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "weaponfov.h"

#define MAX_SHADERS 1024
#define MAX_REGS 4

typedef struct {
	IDirect3DVertexShader9 *vs;
	int reg[MAX_REGS]; // the registers of the constants named like a matrix to clip space (-1: none)
	int count;
	char names[MAX_REGS][40];
} shader_info_t;

static shader_info_t shaders[MAX_SHADERS];
static int nshaders;
static IDirect3DVertexShader9 *cur_vs;
static shader_info_t *cur_info;
static float cur_minz, cur_maxz;
static int log_on;
static float (*tan_view)(void); // tan(fov / 2) of the view as the game builds it (set at install)
static float weapon_tan; // tan(weapon fov / 2) in the same unit; 0 = off

static HRESULT(STDMETHODCALLTYPE *vp_orig)(IDirect3DDevice9 *, const D3DVIEWPORT9 *);
static HRESULT(STDMETHODCALLTYPE *vs_orig)(IDirect3DDevice9 *, IDirect3DVertexShader9 *);
static HRESULT(STDMETHODCALLTYPE *vsc_orig)(IDirect3DDevice9 *, UINT, const float *, UINT);

// the constants of a shader: the constant table is a comment block ("CTAB") at the start of the shader's tokens
static void read_table(shader_info_t *si, const DWORD *code)
{
	si->count = 0;
	for (int i = 0; i < MAX_REGS; i++)
		si->reg[i] = -1;
	for (size_t i = 1; code[i] != 0x0000ffff && i < 4096;) {
		if ((code[i] & 0xffff) != 0xfffe)
			break;
		size_t len = (code[i] >> 16) & 0x7fff;
		if (len > 8 && code[i + 1] == 0x42415443) { // 'CTAB'
			const uint8_t *base = (const uint8_t *)&code[i + 2];
			const DWORD *h = (const DWORD *)base; // size, creator, version, constants, constant info, flags, target
			DWORD n = h[3], info = h[4];
			for (DWORD k = 0; k < n && si->count < MAX_REGS; k++) {
				const DWORD *c = (const DWORD *)(base + info + k * 20);
				const char *name = (const char *)(base + c[0]);
				uint16_t set = (uint16_t)(c[1] & 0xffff), reg = (uint16_t)(c[1] >> 16), cnt = (uint16_t)(c[2] & 0xffff);
				if (set == 2 && cnt == 4 && (strstr(name, "iewProj") || strstr(name, "iewproj"))) { // a float4 x 4 constant with ViewProj in its name
					si->reg[si->count] = reg;
					lstrcpynA(si->names[si->count], name, sizeof(si->names[0]));
					si->count++;
				}
			}
			return;
		}
		i += 1 + len;
	}
}

static shader_info_t *info_of(IDirect3DDevice9 *dev, IDirect3DVertexShader9 *vs)
{
	for (int i = 0; i < nshaders; i++)
		if (shaders[i].vs == vs)
			return &shaders[i];
	if (nshaders >= MAX_SHADERS)
		return NULL;
	UINT size = 0;
	shader_info_t *si = &shaders[nshaders];
	memset(si, 0, sizeof(*si));
	si->vs = vs;
	for (int i = 0; i < MAX_REGS; i++)
		si->reg[i] = -1;
	if (SUCCEEDED(IDirect3DVertexShader9_GetFunction(vs, NULL, &size)) && size) {
		DWORD *code = HeapAlloc(GetProcessHeap(), 0, size);
		if (code && SUCCEEDED(IDirect3DVertexShader9_GetFunction(vs, code, &size)))
			read_table(si, code);
		if (code)
			HeapFree(GetProcessHeap(), 0, code);
	}
	nshaders++;
	if (log_on && si->count)
		log_printf("weaponfov: shader %p has %s at c%d (and %d more)", (void *)vs, si->names[0], si->reg[0], si->count - 1);
	return si;
}

// The game's vertex shaders all take the view-projection matrix in c0..c3 (viewProjectionMatrix: every shader seen in the log has it at c0; each vertex is already in world space)
// and the weapon is drawn in a viewport whose depth range starts above 0 (0.015625: the depth hack). When the viewport goes into that range the four registers are read and
// written back with rows 0 and 1 (clip x and y) scaled; the game's own uploads while it lasts are scaled on their way, and when the viewport leaves the range the game's own
// matrix is put back (the game's cache of what the registers hold is then right).
static float orig_matrix[16];
static int scaled;

static float scale_factor(void)
{
	float view_tan = tan_view ? tan_view() : 0;
	return view_tan > 0 && weapon_tan > 0 ? view_tan / weapon_tan : 0;
}

static void apply_scale(const float *in, float *out, float k)
{
	memcpy(out, in, 16 * sizeof(float));
	for (int i = 0; i < 8; i++)
		out[i] *= k;
}

static HRESULT STDMETHODCALLTYPE hook_viewport(IDirect3DDevice9 *dev, const D3DVIEWPORT9 *vp)
{
	if (vp) {
		cur_minz = vp->MinZ, cur_maxz = vp->MaxZ;
		if (log_on) {
			static float seen[16][2];
			static int n;
			int found = 0;
			for (int i = 0; i < n; i++)
				if (seen[i][0] == vp->MinZ && seen[i][1] == vp->MaxZ)
					found = 1;
			if (!found && n < 16) {
				seen[n][0] = vp->MinZ, seen[n][1] = vp->MaxZ, n++;
				log_printf("weaponfov: viewport %lu,%lu %lux%lu depth range %g to %g", vp->X, vp->Y, vp->Width, vp->Height, vp->MinZ, vp->MaxZ);
			}
		}
		int weapon = vp->MinZ > 0.0f;
		if (weapon && !scaled && weapon_tan > 0) {
			float k = scale_factor();
			float now[16], out[16];
			// only a perspective matrix: clip w comes from depth (row 3 has no constant w part); the 2D passes (the intro's black cards, the HUD) are drawn in the same depth range
			// with an orthographic matrix and must stay as they are
			if (k > 0 && SUCCEEDED(IDirect3DDevice9_GetVertexShaderConstantF(dev, 0, now, 4)) && fabsf(now[15]) < 1e-4f && (fabsf(now[12]) + fabsf(now[13]) + fabsf(now[14])) > 1e-4f) {
				memcpy(orig_matrix, now, sizeof(now));
				apply_scale(now, out, k);
				vsc_orig(dev, 0, out, 4);
				scaled = 1;
				static int logged;
				if (log_on && logged++ < 5)
					log_printf("weaponfov: weapon pass: view-projection scaled by %g (weapon fov %g)", k, 2 * atanf(weapon_tan) * 180.0f / 3.14159265f);
			}
		} else if (!weapon && scaled) {
			vsc_orig(dev, 0, orig_matrix, 4);
			scaled = 0;
		}
	}
	return vp_orig(dev, vp);
}

static HRESULT STDMETHODCALLTYPE hook_shader(IDirect3DDevice9 *dev, IDirect3DVertexShader9 *vs)
{
	cur_vs = vs;
	cur_info = vs ? info_of(dev, vs) : NULL;
	return vs_orig(dev, vs);
}

static HRESULT STDMETHODCALLTYPE hook_constants(IDirect3DDevice9 *dev, UINT start, const float *data, UINT count)
{
	if (scaled && data && start == 0 && count >= 4 && !(fabsf(data[15]) < 1e-4f && (fabsf(data[12]) + fabsf(data[13]) + fabsf(data[14])) > 1e-4f))
		scaled = 0; // an orthographic matrix (a 2D pass in the same depth range): not the weapon any more, left as the game sends it
	if (scaled && data && start < 4 && start + count > 0 && count <= 256) {
		float k = scale_factor();
		if (k > 0) {
			float tmp[256 * 4];
			memcpy(tmp, data, count * 16);
			for (UINT r = start; r < start + count && r < 4; r++) {
				memcpy(orig_matrix + r * 4, data + (r - start) * 4, 16); // what the game holds in the register
				if (r < 2)
					for (int j = 0; j < 4; j++)
						tmp[(r - start) * 4 + j] *= k;
			}
			return vsc_orig(dev, start, tmp, count);
		}
	}
	return vsc_orig(dev, start, data, count);
}

void weaponfov_hook(IDirect3DDevice9 *dev, void (*patch)(void **slot, void *hook, void **orig), float (*view_tan)(void), float weapon_fov_degrees, int log)
{
	void **vt = *(void ***)dev;
	log_on = log;
	tan_view = view_tan;
	weapon_tan = weapon_fov_degrees > 0 ? tanf(weapon_fov_degrees * 3.14159265f / 360.0f) : 0;
	nshaders = 0;
	cur_info = NULL;
	cur_vs = NULL;
	cur_minz = 0, cur_maxz = 1;
	scaled = 0;
	patch(&vt[47], (void *)hook_viewport, (void **)&vp_orig);
	patch(&vt[92], (void *)hook_shader, (void **)&vs_orig);
	patch(&vt[94], (void *)hook_constants, (void **)&vsc_orig);
	log_printf("weaponfov: device hooks %s (weapon fov %g)", log ? "with a log" : "on", weapon_fov_degrees);
}
