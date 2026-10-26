// Native split screen, experiment 1 (docs/native-splitscreen-plan.md, phase 3 first): can the renderer take two views in one frame?
//
// The cgame's draw-active-frame (rva 0xbca00, local client number first) builds the view and queues the 3D scene; the renderer's view setup (rva 0x7e40, from
// rva 0x8150) reads the dvar r_subwindow, a vec4 (left, right, top, bottom: fractions of the picture), and corrects the projection for that rectangle. This
// hook runs the draw twice per frame, the first with the top half as r_subwindow and the second with the bottom half. settings.ini [native] experiment=1
// switches it on; with 2 the second view is turned around (yaw + 180) to show that the views are drawn separately. 64-bit only for now.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hook.h"
#include "log.h"
#include "localize.h"
#include "native.h"
#include "shot.h"

#ifdef _WIN64
#define RVA_CG_DRAW 0x0bca00         // void (unsigned local_client, int, int): the cgame's frame; it calls the begin-frame below in the middle
#define RVA_BEGIN_FRAME 0x00c840     // void (void): swaps the renderer's two frame data sets and resets their command buffers: once per frame
#define RVA_SUBWINDOW_DVAR 0x4116158 // dvar_t *, the value (4 floats) at +0x10

static const uint8_t cg_draw_bytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0xc0, 0x00, 0x00, 0x00};
static const uint8_t begin_frame_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
static void (*cg_draw_original)(unsigned, int, int);
static void (*begin_frame_original)(void);
static uint8_t *base;
static char settings_path[MAX_PATH];
static int mode;
static int second_pass;

static float *subwindow(void)
{
	uint8_t *dvar = *(uint8_t **)(base + RVA_SUBWINDOW_DVAR);
	return dvar ? (float *)(dvar + 0x10) : NULL;
}

// ---- the compositor (mode 2): one scene per frame, the two halves kept between frames
// Each frame the cgame draws one player's view into one half of the picture (r_subwindow). The frame's end (EndScene of the game's device, on the render thread)
// copies that half into a texture of its own and fills the other half from the other texture, so the picture always has both views whole.
#define RVA_D3D_DEVICE 0x410fd20
static volatile LONG ring[8];
static volatile DWORD render_tid;
static volatile LONG ring_head, ring_tail; // frames queued by the main thread, and the halves they were drawn into, in order
static IDirect3DDevice9 *comp_device;
static IDirect3DTexture9 *comp_tex[2];
static IDirect3DSurface9 *comp_surf[2];
static int comp_have[2];
static UINT comp_w, comp_h;
// mode 10: the engine's 2D pass of the scene (rva 0x63680) is drawn into a layer of its own (one per half), cleared to transparent; the compositor lays the top rows of it over the squeezed view
static IDirect3DTexture9 *lay_tex[2];
static IDirect3DSurface9 *lay_surf[2];
static int lay_have[2];
static IDirect3DStateBlock9 *comp_sb;
static volatile LONG layer_pass; // the render thread is inside the 2D pass

static void comp_release(void)
{
	for (int i = 0; i < 2; i++) {
		if (comp_surf[i])
			IDirect3DSurface9_Release(comp_surf[i]);
		if (comp_tex[i])
			IDirect3DTexture9_Release(comp_tex[i]);
		comp_surf[i] = NULL, comp_tex[i] = NULL, comp_have[i] = 0;
		if (lay_surf[i])
			IDirect3DSurface9_Release(lay_surf[i]);
		if (lay_tex[i])
			IDirect3DTexture9_Release(lay_tex[i]);
		lay_surf[i] = NULL, lay_tex[i] = NULL, lay_have[i] = 0;
	}
	if (comp_sb)
		IDirect3DStateBlock9_Release(comp_sb);
	comp_sb = NULL;
}

// Lays the top half of a 2D layer (premultiplied alpha, see layer_fix) over the half texture with the fixed pipeline. Everything the game had set on the device is kept in a state block
// and put back, so the engine's own state caching is not fooled.
static void overlay_layer(IDirect3DDevice9 *dev, int half)
{
	IDirect3DSurface9 *old_rt = NULL, *old_ds = NULL;
	if (!comp_sb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &comp_sb)))
		return;
	IDirect3DStateBlock9_Capture(comp_sb);
	IDirect3DDevice9_GetRenderTarget(dev, 0, &old_rt);
	IDirect3DDevice9_GetDepthStencilSurface(dev, &old_ds);
	IDirect3DDevice9_SetRenderTarget(dev, 0, comp_surf[half]);
	IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);
	D3DVIEWPORT9 vp = {0, 0, comp_w, comp_h / 2, 0.0f, 1.0f};
	IDirect3DDevice9_SetViewport(dev, &vp);
	if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
		static const struct {
			D3DRENDERSTATETYPE s;
			DWORD v;
		} rs[] = {{D3DRS_ZENABLE, 0}, {D3DRS_ZWRITEENABLE, 0}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_ALPHATESTENABLE, 0}, {D3DRS_ALPHABLENDENABLE, 1},
			  {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_SEPARATEALPHABLENDENABLE, 0}, {D3DRS_BLENDOP, D3DBLENDOP_ADD},
			  {D3DRS_STENCILENABLE, 0}, {D3DRS_SCISSORTESTENABLE, 0}, {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FOGENABLE, 0}, {D3DRS_LIGHTING, 0}, {D3DRS_SRGBWRITEENABLE, 0}};
		for (size_t i = 0; i < sizeof(rs) / sizeof(rs[0]); i++)
			IDirect3DDevice9_SetRenderState(dev, rs[i].s, rs[i].v);
		IDirect3DDevice9_SetVertexShader(dev, NULL);
		IDirect3DDevice9_SetPixelShader(dev, NULL);
		IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
		IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)lay_tex[half]);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, 0);
		float w = (float)comp_w - 0.5f, h = (float)(comp_h / 2) - 0.5f;
		float q[4][6] = {{-0.5f, -0.5f, 0, 1, 0, 0}, {w, -0.5f, 0, 1, 1, 0}, {-0.5f, h, 0, 1, 0, 0.5f}, {w, h, 0, 1, 1, 0.5f}}; // the top half of the layer
		IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(q[0]));
		IDirect3DDevice9_EndScene(dev);
	}
	IDirect3DDevice9_SetDepthStencilSurface(dev, old_ds);
	IDirect3DDevice9_SetRenderTarget(dev, 0, old_rt);
	IDirect3DStateBlock9_Apply(comp_sb);
	if (old_rt)
		IDirect3DSurface9_Release(old_rt);
	if (old_ds)
		IDirect3DSurface9_Release(old_ds);
}

static void compose(IDirect3DDevice9 *dev, int half)
{
	IDirect3DSurface9 *back = NULL;
	D3DSURFACE_DESC d;
	if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return;
	IDirect3DSurface9_GetDesc(back, &d);
	if (dev != comp_device || d.Width != comp_w || d.Height != comp_h) {
		comp_release();
		comp_device = dev, comp_w = d.Width, comp_h = d.Height;
		for (int i = 0; i < 2; i++) {
			if (FAILED(IDirect3DDevice9_CreateTexture(dev, d.Width, d.Height / 2, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &comp_tex[i], NULL)) ||
			    FAILED(IDirect3DTexture9_GetSurfaceLevel(comp_tex[i], 0, &comp_surf[i]))) {
				log_printf("native: cannot make the half textures (%ux%u)", d.Width, d.Height / 2);
				comp_release();
				IDirect3DSurface9_Release(back);
				return;
			}
		}
		for (int i = 0; i < 2 && mode == 10; i++) {
			if (FAILED(IDirect3DDevice9_CreateTexture(dev, d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lay_tex[i], NULL)) ||
			    FAILED(IDirect3DTexture9_GetSurfaceLevel(lay_tex[i], 0, &lay_surf[i]))) {
				log_printf("native: cannot make the 2D layers (%ux%u)", d.Width, d.Height);
				comp_release();
				IDirect3DSurface9_Release(back);
				return;
			}
		}
		log_printf("native: compositor on, %ux%u picture, two %ux%u halves", d.Width, d.Height, d.Width, d.Height / 2);
	}
	if (mode == 10) { // the squeezed view, with the top rows of the 2D layer laid over it
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, NULL, comp_surf[half], NULL, D3DTEXF_LINEAR)))
			comp_have[half] = 1;
		if (lay_have[half] && comp_have[half])
			overlay_layer(dev, half);
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	if (mode == 9) { // the top rows hold the view recorded for a half-height screen: they are copied 1:1 into the half texture
		RECT rows = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, &rows, comp_surf[half], NULL, D3DTEXF_NONE)))
			comp_have[half] = 1;
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	if (mode == 7 || mode == 8) { // the whole frame, drawn for the shape of a half, is squeezed into the half texture
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, NULL, comp_surf[half], NULL, D3DTEXF_LINEAR)))
			comp_have[half] = 1;
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	if (mode == 6) { // the drawn view is the middle band of the picture; it fills the whole half texture, and both halves are put on the picture
		RECT band = {0, (LONG)(d.Height / 4), (LONG)d.Width, (LONG)(d.Height * 3 / 4)};
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, &band, comp_surf[half], NULL, D3DTEXF_LINEAR)))
			comp_have[half] = 1;
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	RECT drawn = {0, half ? (LONG)(d.Height / 2) : 0, (LONG)d.Width, half ? (LONG)d.Height : (LONG)(d.Height / 2)};
	RECT other = {0, half ? 0 : (LONG)(d.Height / 2), (LONG)d.Width, half ? (LONG)(d.Height / 2) : (LONG)d.Height};
	if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, &drawn, comp_surf[half], NULL, D3DTEXF_NONE)))
		comp_have[half] = 1;
	if (comp_have[1 - half])
		IDirect3DDevice9_StretchRect(dev, comp_surf[1 - half], NULL, back, &other, D3DTEXF_NONE);
	IDirect3DSurface9_Release(back);
}

static volatile LONG pops_total, pushes_total;
static volatile LONG present_hits[3]; // device Present, device PresentEx, swap chain Present: which one the game calls
static volatile LONG compose_via = -1;

typedef HRESULT(STDMETHODCALLTYPE *present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(STDMETHODCALLTYPE *present_ex_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *sc_present_t)(IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
static present_t present_orig;
static present_ex_t present_ex_orig;
static sc_present_t sc_present_orig;
static IDirect3DDevice9 *the_device;
static const GUID iid_device9ex = {0xb18b10ce, 0x2649, 0x405a, {0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a}};

// before the picture is shown: the half this frame was drawn into is kept and the other one filled in (once per frame, whichever way the game presents)
static void trace_present(void);
static void before_present(int via, IDirect3DDevice9 *dev)
{
	trace_present();
	InterlockedIncrement(&present_hits[via]);
	render_tid = GetCurrentThreadId();
	{
		static DWORD last_log;
		DWORD now = GetTickCount();
		if (now - last_log > 3000 && present_hits[0] + present_hits[1] + present_hits[2] > 0 && pushes_total > 0) {
			last_log = now;
			log_printf("native: present hits %ld/%ld/%ld, via %ld, pushes %ld, ring %ld-%ld", (long)present_hits[0], (long)present_hits[1], (long)present_hits[2], (long)compose_via,
				   (long)pushes_total, (long)ring_tail, (long)ring_head);
		}
	}
	if (compose_via < 0)
		compose_via = via;
	if (compose_via == via && ring_tail == ring_head)
		comp_have[0] = comp_have[1] = 0; // not a split frame: the halves of before are stale
	if (compose_via != via || ring_tail == ring_head)
		return;
	int half = (int)ring[ring_tail & 7];
	InterlockedIncrement(&ring_tail);
	LONG pops = InterlockedIncrement(&pops_total);
	compose(dev, half);
	if (pops >= 3000 && pops < 3012) { // the composed picture about once a second (QA of the experiment: "seq-<pops>.bmp")
		char name[32];
		snprintf(name, sizeof(name), "seq-%ld", (long)pops);
		shot_save_now(dev, name);
	}
	if (pops % 300 == 0)
		log_printf("native: %ld frames composed via %d, presents %ld/%ld/%ld, %ld views queued, queue %ld", (long)pops, via, (long)present_hits[0],
			   (long)present_hits[1], (long)present_hits[2], (long)pushes_total, (long)(ring_head - ring_tail));
}

static HRESULT STDMETHODCALLTYPE present_hook(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND c, const RGNDATA *d)
{
	before_present(0, dev);
	return present_orig(dev, a, b, c, d);
}

static HRESULT STDMETHODCALLTYPE present_ex_hook(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND c, const RGNDATA *d, DWORD e)
{
	before_present(1, dev);
	return present_ex_orig(dev, a, b, c, d, e);
}

static HRESULT STDMETHODCALLTYPE sc_present_hook(IDirect3DSwapChain9 *sc, const RECT *a, const RECT *b, HWND c, const RGNDATA *d, DWORD e)
{
	before_present(2, the_device);
	return sc_present_orig(sc, a, b, c, d, e);
}

// The vtable slots of the D3D9 device and swap chain that this file patches. The Steam overlay (gameoverlayrenderer64.dll) hooks the same functions and puts its hooks back after
// a device Reset; two hooks on one slot that each saved the other's address call each other until the stack is gone (0xc00000fd inside the overlay). So around a Reset all of
// ours are taken out first and put back on top of whatever is there afterwards.
static struct {
	void **slot;
	void *hook;
	void **orig;
	int in;
} slots[24];
static int nslots;

static void patch_slot(void **slot, void *hook, void **orig)
{
	DWORD old;
	if (nslots >= 24 || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
		return;
	*orig = *slot;
	*slot = hook;
	VirtualProtect(slot, sizeof(void *), old, &old);
	slots[nslots].slot = slot, slots[nslots].hook = hook, slots[nslots].orig = orig, slots[nslots].in = 1;
	nslots++;
}

static void slots_take_out(void)
{
	for (int i = 0; i < nslots; i++) {
		DWORD old;
		if (!slots[i].in || *slots[i].slot != slots[i].hook || !VirtualProtect(slots[i].slot, sizeof(void *), PAGE_READWRITE, &old))
			continue; // somebody else is on top of ours: left as it is
		*slots[i].slot = *slots[i].orig;
		VirtualProtect(slots[i].slot, sizeof(void *), old, &old);
		slots[i].in = 0;
	}
}

static void slots_put_back(void)
{
	for (int i = 0; i < nslots; i++) {
		DWORD old;
		if (slots[i].in || !VirtualProtect(slots[i].slot, sizeof(void *), PAGE_READWRITE, &old))
			continue;
		*slots[i].orig = *slots[i].slot; // whatever is there now is what ours calls
		*slots[i].slot = slots[i].hook;
		VirtualProtect(slots[i].slot, sizeof(void *), old, &old);
		slots[i].in = 1;
	}
}

// Reset (vtable 16) fails while default-pool resources of ours are alive: the half textures are let go first (a lost device, a vid_restart, a change of the desktop: "DirectX error")
typedef HRESULT(STDMETHODCALLTYPE *reset_t)(IDirect3DDevice9 *, void *);
static reset_t reset_orig;
static HRESULT STDMETHODCALLTYPE reset_hook(IDirect3DDevice9 *dev, void *params)
{
	log_printf("native: device reset: the half textures are released, the hooks taken out for it");
	comp_release();
	reset_t call = reset_orig;
	slots_take_out();
	HRESULT hr = call(dev, params);
	slots_put_back();
	return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *reset_ex_t)(IDirect3DDevice9 *, void *, void *);
static reset_ex_t reset_ex_orig;
static HRESULT STDMETHODCALLTYPE reset_ex_hook(IDirect3DDevice9 *dev, void *params, void *mode)
{
	log_printf("native: device reset (Ex): the half textures are released, the hooks taken out for it");
	comp_release();
	reset_ex_t call = reset_ex_orig;
	slots_take_out();
	HRESULT hr = call(dev, params, mode);
	slots_put_back();
	return hr;
}

// Watchdog ([native] watchdog=1, default on): when the swap chain's Present has not been reached for 6 s while the main thread keeps queueing views, the render thread is stuck;
// its instruction pointer and the return addresses on its stack are logged as module+offset (once), to see what it waits for.
static DWORD WINAPI watchdog_thread(LPVOID unused)
{
	(void)unused;
	LONG last = -1;
	int stuck = 0;
	for (;;) {
		Sleep(2000);
		LONG now = present_hits[2] + present_hits[0] + present_hits[1];
		if (now == last && ring_head - ring_tail >= 5 && render_tid) {
			if (++stuck == 3) {
				HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, render_tid);
				if (th && SuspendThread(th) != (DWORD)-1) {
					CONTEXT ctx;
					memset(&ctx, 0, sizeof(ctx));
					ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
					if (GetThreadContext(th, &ctx)) {
						char line[1600];
						int n = 0;
						const uint64_t *sp = (const uint64_t *)ctx.Rsp;
						uint64_t addrs[1 + 400];
						int na = 0;
						addrs[na++] = ctx.Rip;
						for (int i = 0; i < 400 && na < 41; i++) {
							uint64_t v = 0;
							if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, sizeof(v), NULL))
								break;
							MEMORY_BASIC_INFORMATION mbi;
							if (v > 0x10000 && VirtualQuery((void *)v, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
								addrs[na++] = v;
						}
						n += snprintf(line + n, sizeof(line) - n, "native: WATCHDOG render thread %lu stuck, rip + stack:", (unsigned long)render_tid);
						for (int i = 0; i < na && n < (int)sizeof(line) - 80; i++) {
							HMODULE m = NULL;
							char name[MAX_PATH] = "?";
							if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)addrs[i], &m) && m) {
								GetModuleFileNameA(m, name, sizeof(name));
								const char *slash = strrchr(name, '\\');
								n += snprintf(line + n, sizeof(line) - n, " %s+%llx", slash ? slash + 1 : name, (unsigned long long)(addrs[i] - (uintptr_t)m));
							}
						}
						log_printf("%s", line);
					}
					ResumeThread(th);
				}
				if (th)
					CloseHandle(th);
			}
		} else {
			stuck = 0;
		}
		last = now;
	}
	return 0;
}

// ---- diagnostics ([native] d3dtrace=1): the render thread's calls of one frame (render targets, viewports, clears, stretches, draw counts), to see where the 3D ends and the 2D begins
static int trace_on;
static char trace_buf[40000];
static int trace_len, trace_draws;
static IDirect3DDevice9 *trace_dev;
static char trace_key[96], trace_key_last[96], trace_stack[160], trace_stack_last[160];
static int bb_pass, bb_pass_done, bb_draws;
static void trace_flush_draws(void)
{
	if (trace_draws && trace_len < (int)sizeof(trace_buf) - 160)
		trace_len += snprintf(trace_buf + trace_len, sizeof(trace_buf) - trace_len, "draw x%d %s  @%s\n", trace_draws, trace_key_last, trace_stack_last);
	trace_draws = 0;
}
// one draw: the shaders and states it uses; a run of draws with the same ones is one line
static void trace_draw(IDirect3DDevice9 *dev)
{
	IDirect3DVertexShader9 *vs = NULL;
	IDirect3DPixelShader9 *ps = NULL;
	DWORD z = 0, zw = 0, ab = 0;
	IDirect3DDevice9_GetVertexShader(dev, &vs);
	IDirect3DDevice9_GetPixelShader(dev, &ps);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ZENABLE, &z);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ZWRITEENABLE, &zw);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ALPHABLENDENABLE, &ab);
	if (bb_pass && !bb_pass_done && bb_draws % 60 == 0 && bb_draws > 0) { // what the picture holds after this many draws of the pass that ends with the screen copy
		char name[24];
		trace_on = 0;
		snprintf(name, sizeof(name), "tr-d%03d", bb_draws);
		shot_save_now(dev, name);
		trace_on = 1;
	}
	bb_draws++;
	snprintf(trace_key, sizeof(trace_key), "vs=%p ps=%p z=%lu zw=%lu blend=%lu", (void *)vs, (void *)ps, (unsigned long)z, (unsigned long)zw, (unsigned long)ab);
	if (vs)
		IDirect3DVertexShader9_Release(vs);
	if (ps)
		IDirect3DPixelShader9_Release(ps);
	{
		void *frames[8];
		USHORT n = CaptureStackBackTrace(2, 8, frames, NULL); // the callers of the engine's draw wrapper, as offsets into the game's image
		int len = 0;
		trace_stack[0] = 0;
		for (USHORT i = 0; i < n && len < (int)sizeof(trace_stack) - 12; i++) {
			uintptr_t off = (uintptr_t)frames[i] - (uintptr_t)base;
			if (off < 0x4000000)
				len += snprintf(trace_stack + len, sizeof(trace_stack) - len, "%lx,", (unsigned long)off);
		}
	}
	if (strcmp(trace_key, trace_key_last) || strcmp(trace_stack, trace_stack_last)) {
		trace_flush_draws();
		strcpy(trace_key_last, trace_key);
		strcpy(trace_stack_last, trace_stack);
	}
	trace_draws++;
}
static void trace_event(const char *fmt, ...)
{
	trace_flush_draws();
	if (trace_len > (int)sizeof(trace_buf) - 200)
		return;
	va_list ap;
	va_start(ap, fmt);
	trace_len += vsnprintf(trace_buf + trace_len, sizeof(trace_buf) - trace_len, fmt, ap);
	va_end(ap);
	trace_buf[trace_len++] = '\n';
}
static const char *trace_surf(IDirect3DDevice9 *dev, IDirect3DSurface9 *s)
{
	static char out[64];
	IDirect3DSurface9 *back = NULL;
	D3DSURFACE_DESC d = {0};
	if (s)
		IDirect3DSurface9_GetDesc(s, &d);
	if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back)
		IDirect3DSurface9_Release(back);
	snprintf(out, sizeof(out), "%p %ux%u fmt%d%s", (void *)s, d.Width, d.Height, (int)d.Format, s && s == back ? " BACKBUFFER" : "");
	return out;
}
// In the 2D layer the engine's alpha blending (source alpha, inverse source alpha) leaves the colour premultiplied by the alpha and the alpha squared; asking for "source alpha one,
// inverse source alpha" for the alpha channel alone makes the layer a proper premultiplied picture (blending states 206-208 are ones the engine never sets itself).
static void layer_fix(IDirect3DDevice9 *dev)
{
	DWORD on = 0, src = 0, dst = 0;
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ALPHABLENDENABLE, &on);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_SRCBLEND, &src);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_DESTBLEND, &dst);
	if (on && src == D3DBLEND_SRCALPHA && dst == D3DBLEND_INVSRCALPHA) {
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, 1);
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
		IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	} else {
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, 0);
	}
}
typedef HRESULT(STDMETHODCALLTYPE *set_rt_t)(IDirect3DDevice9 *, DWORD, IDirect3DSurface9 *);
typedef HRESULT(STDMETHODCALLTYPE *clear_t)(IDirect3DDevice9 *, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *stretch_t)(IDirect3DDevice9 *, IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const RECT *, D3DTEXTUREFILTERTYPE);
typedef HRESULT(STDMETHODCALLTYPE *viewport_t)(IDirect3DDevice9 *, const D3DVIEWPORT9 *);
typedef HRESULT(STDMETHODCALLTYPE *dip_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *dp_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *dpup_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT(STDMETHODCALLTYPE *dipup_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT);
typedef HRESULT(STDMETHODCALLTYPE *scene_t)(IDirect3DDevice9 *);
static set_rt_t set_rt_orig;
static clear_t clear_orig;
static stretch_t stretch_orig;
static viewport_t viewport_orig;
static dip_t dip_orig;
static dp_t dp_orig;
static dpup_t dpup_orig;
static dipup_t dipup_orig;
static scene_t begin_scene_orig, end_scene_orig;
static HRESULT STDMETHODCALLTYPE t_dpup(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, UINT pc, const void *v, UINT st)
{
	if (layer_pass)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	return dpup_orig(dev, t, pc, v, st);
}
static HRESULT STDMETHODCALLTYPE t_dipup(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void *i, D3DFORMAT f, const void *v, UINT st)
{
	if (layer_pass)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	return dipup_orig(dev, t, mi, nv, pc, i, f, v, st);
}
static HRESULT STDMETHODCALLTYPE t_set_rt(IDirect3DDevice9 *dev, DWORD i, IDirect3DSurface9 *s)
{
	if (trace_on && i == 0) {
		const char *d = trace_surf(dev, s);
		trace_event("SetRenderTarget %s", d);
		if (strstr(d, "BACKBUFFER") && !bb_pass_done)
			bb_pass = 1, bb_draws = 0;
	}
	return set_rt_orig(dev, i, s);
}
static HRESULT STDMETHODCALLTYPE t_clear(IDirect3DDevice9 *dev, DWORD n, const D3DRECT *r, DWORD f, D3DCOLOR c, float z, DWORD st)
{
	if (trace_on)
		trace_event("Clear flags %lx color %08lx rects %lu", (unsigned long)f, (unsigned long)c, (unsigned long)n);
	return clear_orig(dev, n, r, f, c, z, st);
}
static HRESULT STDMETHODCALLTYPE t_stretch(IDirect3DDevice9 *dev, IDirect3DSurface9 *a, const RECT *ra, IDirect3DSurface9 *b, const RECT *rb, D3DTEXTUREFILTERTYPE f)
{
	if (trace_on) {
		char s1[80];
		snprintf(s1, sizeof(s1), "%s", trace_surf(dev, a));
		trace_event("StretchRect %s -> %s", s1, trace_surf(dev, b));
		if (strstr(s1, "BACKBUFFER") && !strstr(trace_surf(dev, b), "1920x540")) {
			trace_on = 0;
			shot_save_now(dev, "tr-copy");
			trace_on = 1;
			bb_pass_done = 1;
			trace_event("(%d draws in the pass before the screen copy)", bb_draws);
		}
	}
	return stretch_orig(dev, a, ra, b, rb, f);
}
static HRESULT STDMETHODCALLTYPE t_viewport(IDirect3DDevice9 *dev, const D3DVIEWPORT9 *v)
{
	if (trace_on && v)
		trace_event("SetViewport %lu,%lu %lux%lu", (unsigned long)v->X, (unsigned long)v->Y, (unsigned long)v->Width, (unsigned long)v->Height);
	return viewport_orig(dev, v);
}
static HRESULT STDMETHODCALLTYPE t_dip(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc)
{
	if (layer_pass)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	return dip_orig(dev, t, bv, mi, nv, si, pc);
}
static HRESULT STDMETHODCALLTYPE t_dp(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, UINT sv, UINT pc)
{
	if (layer_pass)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	return dp_orig(dev, t, sv, pc);
}
static HRESULT STDMETHODCALLTYPE t_begin(IDirect3DDevice9 *dev)
{
	if (trace_on)
		trace_event("BeginScene");
	return begin_scene_orig(dev);
}
static HRESULT STDMETHODCALLTYPE t_end(IDirect3DDevice9 *dev)
{
	if (trace_on)
		trace_event("EndScene");
	return end_scene_orig(dev);
}
static void trace_install(IDirect3DDevice9 *dev)
{
	void **vt = *(void ***)dev;
	patch_slot(&vt[37], (void *)t_set_rt, (void **)&set_rt_orig);
	patch_slot(&vt[43], (void *)t_clear, (void **)&clear_orig);
	patch_slot(&vt[34], (void *)t_stretch, (void **)&stretch_orig);
	patch_slot(&vt[47], (void *)t_viewport, (void **)&viewport_orig);
	patch_slot(&vt[82], (void *)t_dip, (void **)&dip_orig);
	patch_slot(&vt[81], (void *)t_dp, (void **)&dp_orig);
	patch_slot(&vt[83], (void *)t_dpup, (void **)&dpup_orig);
	patch_slot(&vt[84], (void *)t_dipup, (void **)&dipup_orig);
	patch_slot(&vt[41], (void *)t_begin, (void **)&begin_scene_orig);
	patch_slot(&vt[42], (void *)t_end, (void **)&end_scene_orig);
	trace_dev = dev;
}
// called at each Present of the render thread: the frame after the 900th composed one is traced
static void trace_present(void)
{
	static int armed = -1;
	if (armed < 0)
		armed = GetPrivateProfileIntA("native", "d3dtrace", 0, settings_path);
	if (!armed || !trace_dev)
		return;
	if (trace_on) {
		trace_event("Present");
		shot_save_now(trace_dev, "tr-end");
		trace_on = 0;
		{
			char path[MAX_PATH];
			snprintf(path, sizeof(path), "%s", settings_path);
			char *slash = strrchr(path, '\\');
			if (slash)
				strcpy(slash + 1, "d3dtrace.txt");
			FILE *f = fopen(path, "wb");
			if (f) {
				fwrite(trace_buf, 1, trace_len, f);
				fclose(f);
			}
			log_printf("native: D3D TRACE of one frame written to %s (%d bytes)", path, trace_len);
		}
		armed = 0;
	} else if (pops_total >= 600) {
		trace_len = 0, trace_draws = 0, bb_pass = 0, bb_pass_done = 0, bb_draws = 0;
		trace_on = 1;
	}
}

// Mode 10: the scene function's last pass (rva 0x63680, called at 0x5c5bf: the 2D texts and signs) draws into the half's layer instead of the picture, so the picture keeps the view alone
// (the glow and blur that follow work on it) and the 2D, laid out by the client for a half-height screen, stays whole.
static void (*scene2d_original)(void *);
static void scene2d_hook(void *vp)
{
	IDirect3DDevice9 *dev = the_device;
	if (mode != 10 || !dev || ring_tail == ring_head || !lay_surf[0] || !lay_surf[1]) {
		scene2d_original(vp);
		return;
	}
	int half = (int)ring[ring_tail & 7];
	IDirect3DSurface9 *old = NULL;
	IDirect3DDevice9_GetRenderTarget(dev, 0, &old);
	IDirect3DDevice9_SetRenderTarget(dev, 0, lay_surf[half]);
	IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
	lay_have[half] = 1;
	layer_pass = 1;
	scene2d_original(vp);
	layer_pass = 0;
	IDirect3DDevice9_SetRenderTarget(dev, 0, old);
	if (old)
		IDirect3DSurface9_Release(old);
}

static void comp_hook(void)
{
	if (the_device)
		return;
	IDirect3DDevice9 *dev = *(IDirect3DDevice9 **)(base + RVA_D3D_DEVICE);
	if (!dev)
		return;
	the_device = dev;
	void **vt = *(void ***)dev;
	patch_slot(&vt[17], (void *)present_hook, (void **)&present_orig);
	if (GetPrivateProfileIntA("native", "resethook", 1, settings_path))
		patch_slot(&vt[16], (void *)reset_hook, (void **)&reset_orig);
	IDirect3DDevice9Ex *ex = NULL;
	if (SUCCEEDED(IDirect3DDevice9_QueryInterface(dev, &iid_device9ex, (void **)&ex)) && ex) {
		patch_slot(&vt[121], (void *)present_ex_hook, (void **)&present_ex_orig); // PresentEx: the slot of the Ex interface
		if (GetPrivateProfileIntA("native", "resethook", 1, settings_path))
			patch_slot(&vt[132], (void *)reset_ex_hook, (void **)&reset_ex_orig);
		IDirect3DDevice9Ex_Release(ex);
	}
	IDirect3DSwapChain9 *sc = NULL;
	if (SUCCEEDED(IDirect3DDevice9_GetSwapChain(dev, 0, &sc)) && sc) {
		void **svt = *(void ***)sc;
		patch_slot(&svt[3], (void *)sc_present_hook, (void **)&sc_present_orig);
		IDirect3DSwapChain9_Release(sc);
	}
	{
		HMODULE m1 = NULL, m2 = NULL;
		char n1[MAX_PATH] = "?", n2[MAX_PATH] = "?";
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)present_orig, &m1);
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)sc_present_orig, &m2);
		if (m1)
			GetModuleFileNameA(m1, n1, sizeof(n1));
		if (m2)
			GetModuleFileNameA(m2, n2, sizeof(n2));
		log_printf("native: device Present in %s, swap chain Present in %s", n1, n2);
	}
	if (mode == 10 || GetPrivateProfileIntA("native", "d3dtrace", 0, settings_path))
		trace_install(dev);
	log_printf("native: Present hooks set (device %p, Ex %s)", (void *)dev, present_ex_orig ? "yes" : "no");
	if (GetPrivateProfileIntA("native", "watchdog", 1, settings_path))
		CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL);
}

// the second draw of the frame must not start a new frame: it adds its view to the one the first draw began
static void begin_frame_hook(void)
{
	if (second_pass)
		return;
	begin_frame_original();
}

static int ctx_on;
static float cg_aspect_saved;
static int cg_aspect_have;
static float asp_saved[2];
static int asp_have;
static int split_on = 1; // the scripts set the dvar mw2coop_split_view once the intro is over; before that there is one picture
static int frame_bottom; // mode 5: this client frame is context 1's (the bottom half)
static int ctx1_ready;
static void ctx_swap(int to);
void native_unswap_entities(void);
static void ctx_after_draw(void);

// Diagnostics ([native] scanplace=1, once at the first split frame): where the 2D placement keeps the screen scale. The values the placement holds for a 1920x1080 screen are looked for in the
// game's image: the pair of floats (3.0, 2.25) (real pixels per virtual unit), (1/3, 0.4444), and the int pair (1920, 1080); every hit is logged with the floats around it.
static void scan_place(void)
{
	static int done;
	if (done || !GetPrivateProfileIntA("native", "scanplace", 0, settings_path))
		return;
	done = 1;
	uint8_t *lo = base, *hi = base + ((IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew))->OptionalHeader.SizeOfImage;
	int hits = 0;
	for (uint8_t *a = lo; a < hi && hits < 60;) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(a, &mbi, sizeof(mbi)))
			break;
		uint8_t *end = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
		if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD)) {
			for (uint8_t *q = (uint8_t *)mbi.BaseAddress; q + 8 <= end && hits < 60; q += 4) {
				float f0 = *(float *)q, f1 = *(float *)(q + 4);
				int i0 = *(int *)q, i1 = *(int *)(q + 4);
				const char *kind = NULL;
				if (f0 == 3.0f && f1 == 2.25f)
					kind = "scale 3.0 2.25";
				else if (f0 > 0.33332f && f0 < 0.33334f && f1 > 0.44443f && f1 < 0.44446f)
					kind = "inverse 1/3 0.4444";
				else if (i0 == 1920 && i1 == 1080)
					kind = "ints 1920 1080";
				if (kind) {
					hits++;
					char line[700];
					int n = snprintf(line, sizeof(line), "native: scanplace %s at rva 0x%llx:", kind, (unsigned long long)(q - base));
					for (int k = -2; k < 28 && n < (int)sizeof(line) - 24; k++)
						n += snprintf(line + n, sizeof(line) - n, " %g", (double)*(float *)(q + k * 4));
					log_printf("%s", line);
				}
			}
		}
		a = end;
	}
	log_printf("native: scanplace done, %d hits", hits);
}

static void cg_draw_hook(unsigned local_client, int a, int b)
{
	float *sw = subwindow();
	if (split_on)
		scan_place();
	if (!mode || !sw || second_pass || (mode >= 5 && !split_on)) {
		if ((mode == 8 || mode == 10) && cg_aspect_have) { // back from the split: the cgame's aspect as it was
			*(float *)(base + 0x43c640) = cg_aspect_saved;
			cg_aspect_have = 0;
		}
		if (mode == 7 && asp_have) { // back from the split: the video aspect as the game had it
			memcpy(base + 0x4113738, asp_saved, sizeof(asp_saved));
			asp_have = 0;
		}
		cg_draw_original(local_client, a, b);
		if (ctx_on)
			ctx_after_draw();
		return;
	}
	float saved[4];
	memcpy(saved, sw, sizeof(saved));
	if (mode >= 2 && mode <= 10) { // alternate frames: one scene per frame, the top half on even frames and the bottom half on odd ones
		static unsigned frame;
		int bottom = mode >= 5 ? frame_bottom : (int)(frame++ & 1); // modes 5 and 6: the client frame (below) says whose frame it is
		sw[0] = 0, sw[1] = 1, sw[2] = bottom ? 0.5f : 0, sw[3] = bottom ? 1 : 0.5f;
		if (mode == 6) // the middle band of the full view in both halves: right proportions, half the vertical field
			sw[2] = 0.25f, sw[3] = 0.75f;
		if (mode == 9) // the views are recorded for a half-height screen: the whole viewport, in the top rows
			sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 1;
		if (mode == 8 || mode == 10) { // the console's way: the whole view, built for the shape of a half with the cgame's own aspect (rva 0x43c640, read by CG_CalcFov), drawn over the full frame and squeezed into the half
			sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 1;
			if (comp_w && comp_h) {
				float *aspect = (float *)(base + 0x43c640);
				if (!cg_aspect_have) {
					cg_aspect_saved = *aspect;
					cg_aspect_have = 1;
				}
				*aspect = (float)comp_w / ((float)comp_h * 0.5f);
			}
			{ // [native] pixelaspect=<f>: the pixel aspect the 2D is laid out with (rva 0x411373c); the squeeze of the frame into its half flattens text 2:1, a factor of 2 or 0.5 here
			  // pre-stretches it. Kept while the picture is split.
				static float want = -1;
				static float saved;
				static int have;
				float *pa = (float *)(base + 0x411373c);
				if (want < 0) {
					char v[32];
					GetPrivateProfileStringA("native", "pixelaspect", "0", v, sizeof(v), settings_path);
					want = (float)atof(v);
				}
				if (want > 0) {
					if (!have) {
						saved = *pa;
						have = 1;
					}
					*pa = want;
				}
			}
		}
		if (mode == 7) { // the whole view, built for the shape of a half (wide), drawn over the full frame: the compositor squeezes it into the half (rva 0x4113738: aspect, 0x411373c: pixel aspect)
			sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 1;
			if (comp_w && comp_h) {
				float *aspect = (float *)(base + 0x4113738);
				if (!asp_have) {
					memcpy(asp_saved, aspect, sizeof(asp_saved));
					asp_have = 1;
				}
				aspect[0] = (float)comp_w / ((float)comp_h * 0.5f);
				aspect[1] = 1.0f;
			}
		}
		comp_hook();
		{
			static DWORD last_log;
			static unsigned draws;
			draws++;
			DWORD now = GetTickCount();
			if (now - last_log > 3000) {
				last_log = now;
				log_printf("native: split draws %u, device %p, pushes %ld, ring %ld-%ld, bottom %d", draws, (void *)the_device, (long)pushes_total, (long)ring_tail, (long)ring_head, bottom);
				if (ring_tail == 0 && draws > 100) { // no frame has ever been composed: are our hooks still in the tables?
					for (int i = 0; i < nslots; i++)
						log_printf("native: slot %d (%p): in %d, now %p, hook %p", i, (void *)slots[i].slot, slots[i].in, *slots[i].slot, slots[i].hook);
				}
			}
		}
		if (the_device && ring_head - ring_tail < 6) { // the half this frame is drawn into: the frame's end composes it
			ring[ring_head & 7] = bottom;
			InterlockedIncrement(&ring_head);
			InterlockedIncrement(&pushes_total);
		}
		if (mode == 4 && bottom && ctx1_ready) { // mode 4: the odd frames run the cgame in context 1 (the client statics swapped), as local client 0
			ctx_swap(1);
			cg_draw_original(0, a, b);
			ctx_swap(0);
		} else {
			cg_draw_original(mode == 3 && bottom ? 1u : local_client, a, b); // mode 3: the odd frames run the cgame for LOCAL CLIENT 1
		}
		memcpy(sw, saved, sizeof(saved));
		if (ctx_on)
			ctx_after_draw();
		return;
	}
	sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 0.5f; // the top half
	cg_draw_original(local_client, a, b);
	sw[2] = 0.5f, sw[3] = 1; // the bottom half
	second_pass = 1;
	cg_draw_original(local_client, a, b);
	second_pass = 0;
	memcpy(sw, saved, sizeof(saved));
	static int logged;
	if (!logged++)
		log_printf("native: the first frame was drawn twice (top and bottom half)");
}

// ---- experiment 4: the engine's own local-players configuration (docs/native-splitscreen-plan.md, "Packet flow")
// G_InitGame stores the number of local players (rva 0x3c3eb0) as the immediate 1 of `mov dword [rip+x], 1` (c7 05 <rel32> 01 00 00 00 at rva 0x1714a6) when the co-op byte is 0.
// settings.ini [native] players=2 makes it 2: the server then makes two clients and two player entities, with no network and no co-op session. The client side has one
// element of each array, so this only shows what the server side does alone.
#define RVA_LOCAL_PLAYERS_STORE 0x1714a6
// The server's reliable command to a client (rva 0x232c30, SV_AddServerCommand): client_t +4 write offset, +8 sequence, +0xc acknowledge, text at +0x10 (0x1000 bytes), the
// offset of command n at +0x1010 + (n & 0x7f) * 4. Client 1 of the server has no client behind it yet (nothing acknowledges), so its buffer overflows within seconds
// and Com_Error ends the level ("Reliable command buffer overflow"); with players=2 and [native] skip_client1=1 its commands are dropped instead.
#define RVA_SV_ADD_COMMAND 0x232c30
#define RVA_COM_ERROR 0x1fb5f0 // void (int code, const char *format, ...)
#define RVA_SV_CLIENTS 0x19ada98
#define RVA_COOP_BYTE 0x52fb5c
#define RVA_LOCAL_PLAYERS 0x3c3eb0
#define RVA_COOP_SLOTS 0x540bb0 // slot p: type int at +p*0x2650
static void sv_add_command(uint8_t *cl, const char *text)
{
	int idx = (int)((cl - (base + RVA_SV_CLIENTS)) / 0x1678);
	int in_use = *(base + RVA_COOP_BYTE) ? *(int *)(base + RVA_COOP_SLOTS + idx * 0x2650) == 2 : idx < *(int *)(base + RVA_LOCAL_PLAYERS);
	if (!in_use || (idx == 1 && !*(base + RVA_COOP_BYTE)))
		return;
	void (*com_error)(int, const char *, ...) = (void (*)(int, const char *, ...))(base + RVA_COM_ERROR);
	int *p = (int *)cl;
	int seq = p[2];
	if (seq - p[3] == 0x80) {
		com_error(1, "Server command overflow");
		seq = p[2];
	}
	p[2] = seq + 1;
	int len = (int)strlen(text);
	int off = p[1];
	if (0x1000 < off + 1 + len) {
		com_error(1, "Reliable command buffer overflow");
		off = p[1];
	}
	*(int *)(cl + 0x1010 + ((seq + 1) & 0x7f) * 4) = off;
	char *d = (char *)(cl + 0x10 + p[1]);
	for (int i = 0; i < len; i++)
		*d++ = text[i] == '%' ? '.' : text[i];
	*d = 0;
	p[1] += len + 1;
}

// Debug aid ([native] logcbuf=1): every text the engine adds to the command buffer, in the log.
static const uint8_t cbuf_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xf9, 0x48, 0x8b, 0xda};
static void (*cbuf_original)(int, const char *);
static void cbuf_hook(int lc, const char *text)
{
	char line[200];
	size_t i = 0;
	for (; text && text[i] && text[i] != '\n' && i < sizeof(line) - 1; i++)
		line[i] = text[i];
	line[i] = 0;
	log_printf("cbuf(%d): %s", lc, line);
	cbuf_original(lc, text);
}

// ---- experiment 5: a second client context (docs/native-splitscreen-plan.md): the engine's client code runs for local client 1 as local client 0, with the client statics swapped
// The arrays of the client side (cl, clc, cls, the cgame's cg/cgs) have one element, so the loops over the local clients (i < number of local players) would write local client 1's
// state over unrelated statics. Each call site of such a loop is redirected to a wrapper: for client 1 it swaps the client statics to context 1's copy, calls the function as
// client 0, and swaps back. The ranges are the first guess (cgame block, snapshots, cl, clc, cls, input); the ones the experiments show missing are added.
typedef struct {
	uint32_t rva, size;
} range_t;
static const range_t ranges[] = {
    {0x404450, 0x44f000 - 0x404450}, // cgs, cg, cg_entities and neighbours
    {0x51b078, 0x51dcb0 - 0x51b078}, // cl snapshots and their pointers
    {0x52c400, 0x100},               // cleared by the cgame init for each client
    {0x52fbc0, 0x21c},               // input
    {0x533cb8, 0x68},                // cls state, the mouse state and the view angle accumulators (0x533cc0, 0x533d0c-0x533d18)
    {0x533d20, 0x540b60 - 0x533d20}, // cl: usercmd ring, cmdNumber, playerstate, packet history
    {0x545844, 0x3218},              // clc: reliable command ring
    {0x548ce8, 8},                   // the snapshot message's time and flag
};
#define NRANGES (sizeof(ranges) / sizeof(ranges[0]))
static uint8_t *ctx_store[2][NRANGES];
static int ctx_current;

static void ctx_ensure(void)
{
	if (ctx_store[0][0])
		return;
	for (size_t i = 0; i < NRANGES; i++) { // first use: both copies start as what the live memory holds now
		ctx_store[0][i] = VirtualAlloc(NULL, ranges[i].size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		ctx_store[1][i] = VirtualAlloc(NULL, ranges[i].size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		memcpy(ctx_store[1][i], base + ranges[i].rva, ranges[i].size);
	}
}

// `mask`: the ranges (bit i = ranges[i]) that are swapped; a function that touches only the cl block need not expose the other threads to a swapped cgame block. A masked swap
// must be undone with the same mask, and not nest with another.
#define CTX_ALL ((1u << NRANGES) - 1)
#define CTX_CL (1u << 5)
#define CTX_CL_CLC ((1u << 5) | (1u << 6))
static void ctx_swap_mask(int to, unsigned mask)
{
	if (to == ctx_current)
		return;
	ctx_ensure();
	for (size_t i = 0; i < NRANGES; i++) {
		if (!(mask & (1u << i)))
			continue;
		uint8_t *live = base + ranges[i].rva;
		memcpy(ctx_store[ctx_current][i], live, ranges[i].size);
		memcpy(live, ctx_store[to][i], ranges[i].size);
	}
	ctx_current = to;
}

static void ctx_swap(int to)
{
	ctx_swap_mask(to, CTX_ALL);
}

// The cgame block holds what the cgame registered for the whole process (the media handles the 2D drawing uses even while the cgame is still being initialised). Context 1 starts
// from context 0's block after its init, so its own init can draw the loading screen.
static void ctx_seed_cgame(void)
{
	static int seeded;
	if (seeded || ctx_current != 0)
		return;
	ctx_ensure();
	// Context 1 starts as a copy of context 0 as it is now (the cgame registered dvars and media for the whole process, the snapshot pointers point into their own block), except for
	// the playerstate the server published for client 1, which is its own.
	for (size_t i = 0; i < NRANGES; i++) {
		uint8_t *live = base + ranges[i].rva;
		if (i == 5) { // the cl block: everything but the playerstate at +0x2018
			memcpy(ctx_store[1][i], live, 0x2018);
			memcpy(ctx_store[1][i] + 0x2018 + 0xaca4, live + 0x2018 + 0xaca4, ranges[i].size - 0x2018 - 0xaca4);
		} else {
			memcpy(ctx_store[1][i], live, ranges[i].size);
		}
	}
	seeded = 1;
}

// The client code reads the number of local players (rva 0x3c3eb0) at these instructions (rva, length, offset of the rip-relative displacement). The server and the game module keep
// reading the real one (2); the client code is pointed at a copy that stays 1, so its loops over the local clients only ever touch client 0 and nothing of the arrays of one element.
static const struct {
	uint32_t rva;
	uint8_t length, disp;
} client_n_sites[] = {
	{0x6bae9, 6, 2},
	{0x85f44, 6, 2},
	{0x89e44, 6, 2},
	{0x93d94, 6, 2},
	{0xa08c2, 7, 3},
	{0xa6703, 7, 3},
	{0xa69cf, 6, 2},
	{0xa6a76, 6, 2},
	{0xa6b29, 7, 3},
	{0xa6c44, 6, 2},
	{0xa6c8c, 6, 2},
	{0xa8f25, 6, 2},
	{0xa905d, 6, 2},
	{0xa9248, 6, 2},
	{0xa936b, 6, 2},
	{0xb2154, 6, 2},
	{0xb60ec, 7, 3},
	{0xb629a, 6, 2},
	{0xb630a, 6, 2},
	{0xb71df, 6, 2},
	{0xb8e34, 6, 2},
	{0xbcb49, 6, 2},
	{0xc2835, 6, 2},
	{0xc3e5e, 6, 2},
	{0xc3eb8, 6, 2},
	{0xc9667, 6, 2},
	{0xc96f5, 6, 2},
	{0xc9754, 6, 2},
	{0xc9a04, 6, 2},
	{0xca135, 6, 2},
	{0xca184, 6, 2},
	{0xca607, 6, 2},
	{0xd22ac, 6, 2},
	{0xd2304, 6, 2},
	{0xd5595, 6, 2}
};

static void client_n_install(void)
{
	int32_t *n_client = (int32_t *)hook_page_near(base + RVA_LOCAL_PLAYERS_STORE);
	if (!n_client) {
		log_printf("native: no page near the image for the client's player count");
		return;
	}
	*n_client = 1;
	int done = 0;
	for (size_t i = 0; i < sizeof(client_n_sites) / sizeof(client_n_sites[0]); i++) {
		uint8_t *site = base + client_n_sites[i].rva;
		int32_t old;
		memcpy(&old, site + client_n_sites[i].disp, 4);
		if ((uint8_t *)(site + client_n_sites[i].length + old) != base + 0x3c3eb0)
			continue; // not the instruction the build has
		int64_t to = (int64_t)((uint8_t *)n_client - (site + client_n_sites[i].length));
		if (to > INT32_MAX || to < INT32_MIN)
			continue;
		int32_t rel = (int32_t)to;
		DWORD prot;
		VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &prot);
		memcpy(site + client_n_sites[i].disp, &rel, 4);
		VirtualProtect(site, 8, prot, &prot);
		done++;
	}
	FlushInstructionCache(GetCurrentProcess(), base, 0xe0000);
	log_printf("native: %d of %d client reads of the local player count point at a copy (1)", done, (int)(sizeof(client_n_sites) / sizeof(client_n_sites[0])));
}

// The server's entity list for the snapshot leaves out the local players' entities (they are fed through the playerstate); with two local players each context would never see the other
// one's body. The read of the player count at the start of the publisher (rva 0x23459a, `mov reg, [0x3c3eb0]`, used only for that exclusion) is pointed at a variable that is 0.
// [native] listplayers=1.
static void list_players_install(void)
{
	uint8_t *site = base + 0x23459a;
	int32_t old;
	memcpy(&old, site + 3, 4);
	int32_t *zero = (int32_t *)hook_page_near(base + RVA_LOCAL_PLAYERS_STORE);
	if (!zero || (uint8_t *)(site + 7 + old) != base + 0x3c3eb0) {
		log_printf("native: entity list patch: unexpected code");
		return;
	}
	*zero = 0;
	int32_t rel = (int32_t)((uint8_t *)zero - (site + 7));
	DWORD prot;
	VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &prot);
	memcpy(site + 3, &rel, 4);
	VirtualProtect(site, 8, prot, &prot);
	FlushInstructionCache(GetCurrentProcess(), site, 8);
	log_printf("native: the entity list keeps the local players' entities");
}

static void (*ps_publish_original)(unsigned, const void *);
static void (*server_cmds_original)(unsigned long long, int *);
static void (*usercmd_step_original)(int);
static void (*snapshot_msg_original)(void *, int);
static void (*cg_init_all_original)(void);
static void (*snapshot_take_original)(void);
#define RVA_LOCAL_PLAYERS_N 0x3c3eb0

// A client-side function that loops over the local clients (and calls others that touch the client statics without a client number): run for context 0 and, once context 1
// exists, for context 1, each time with the number of local players set to 1 so that every loop sees only client 0, which in context 1 is client 1.
static int cg_init_done;
static int draws_after_init;

static void run_both(void (*fn)(void))
{
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	fn();
	if (ctx1_ready) {
		ctx_swap(1);
		fn();
		ctx_swap(0);
	}
	if (was)
		ctx_swap(was);
}

static void cg_init_all_hook(void)
{
	log_printf("native: cgame init (context 0; context 1 follows after the first frames), dvar ptr 0x52bfc0 = %p", *(void **)(base + 0x52bfc0));
	cg_init_all_original();
	cg_init_done = 1;
	log_printf("native: cgame init done, dvar ptr 0x52bfc0 = %p", *(void **)(base + 0x52bfc0));
}

static void snapshot_take_hook(void)
{
	run_both(snapshot_take_original);
}

// The cgame registers its dvars and media while it runs the first frames, and the 2D drawing of every thread reads them: context 1 can only start from a cgame block that has them.
// So its init runs a few frames after context 0's, from its first frame's hook, from a copy of context 0's block.
static void ctx_dump(const char *what)
{
	static const uint32_t probes[] = {0x422434, 0x40caa0, 0x40cad0, 0x40cad8, 0x40cab8, 0x422440};
	uint8_t *r1_live = base + ranges[0].rva;
	uint8_t *r1 = ctx_current == 0 && ctx_store[1][0] ? ctx_store[1][0] : r1_live;
	char line[400];
	int n = snprintf(line, sizeof(line), "native: %s ctx1:", what);
	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
		n += snprintf(line + n, sizeof(line) - n, " %x=%llx", probes[i], *(unsigned long long *)(r1 + probes[i] - ranges[0].rva));
	n += snprintf(line + n, sizeof(line) - n, " | ctx0:");
	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
		n += snprintf(line + n, sizeof(line) - n, " %x=%llx", probes[i], *(unsigned long long *)(r1_live + probes[i] - ranges[0].rva));
	log_printf("%s", line);
}

static void ctx_jitter_log(void)
{
	static unsigned seen;
	if (ctx_current != 1 || !GetPrivateProfileIntA("native", "jitterlog", 0, settings_path))
		return;
	seen++;
	if (seen < 1500 || seen >= 1506)
		return;
	char line[1200];
	int n = snprintf(line, sizeof(line), "native: ctx1 ps frame %u:", seen);
	const float *f = (const float *)(base + 0x422440);
	for (int i = 0; i < 70; i++) {
		float v = f[i];
		if (v > -100000.0f && v < 100000.0f && (v > 0.0001f || v < -0.0001f))
			n += snprintf(line + n, sizeof(line) - n, " %x=%.1f", i * 4, v);
	}
	log_printf("%s", line);
}

// Diagnostics ([native] entlog=1): the entities of the live cgame near the camera (cg_entities at rva 0x441070, 0x1a8 bytes each: eType +0xe0, origin +0x18, clientNum +0x160)
static void ctx_entity_log(void)
{
	static unsigned seen[2];
	if (!GetPrivateProfileIntA("native", "entlog", 0, settings_path))
		return;
	if (++seen[ctx_current] != 600)
		return;
	{
		const float *f = (const float *)(base + 0x422440);
		char line[1500];
		int n = snprintf(line, sizeof(line), "native: ctx %d cg ps floats:", ctx_current);
		for (int i = 0; i < 100; i++)
			if (f[i] > -400.0f && f[i] < 400.0f && (f[i] > 0.01f || f[i] < -0.01f))
				n += snprintf(line + n, sizeof(line) - n, " %x=%.1f", i * 4, f[i]);
		log_printf("%s", line);
	}
	{
		const float *f = (const float *)(base + 0x43c200);
		char line[1500];
		int n = snprintf(line, sizeof(line), "native: ctx %d refdef-ish floats @43c200:", ctx_current);
		for (int i = 0; i < 90; i++)
			if (f[i] > -1000000.0f && f[i] < 1000000.0f && (f[i] > 0.001f || f[i] < -0.001f))
				n += snprintf(line + n, sizeof(line) - n, " %x=%.2f", i * 4, f[i]);
		log_printf("%s", line);
	}
	const float *cam = (const float *)(base + 0x42245c);
	log_printf("native: ctx %d camera %.0f %.0f %.0f, clientNum %d", ctx_current, cam[0], cam[1], cam[2], *(int *)(base + 0x40caa0));
	for (int i = 0; i < 1024; i++) {
		const uint8_t *e = base + 0x441070 + (size_t)i * 0x1a8;
		const float *o = (const float *)(e + 0x18);
		float dx = o[0] - cam[0], dy = o[1] - cam[1], dz = o[2] - cam[2];
		if (dx * dx + dy * dy + dz * dz < 200.0f * 200.0f && e[0xe0] != 0)
			log_printf("native:   ent %d type %d flags %08x clientNum %d at %.0f %.0f %.0f", i, e[0xe0], *(unsigned *)(e + 0xec), *(unsigned short *)(e + 0x160), o[0], o[1], o[2]);
	}
}

static void ctx_after_draw(void)
{
	static int texts_done;
	if (!texts_done) { // both players are on this machine: the co-op "waiting for the other player" texts have nothing to wait for (an asset lookup: not while the level is loading)
		texts_done = 1;
		localize_override("COOP_WAITINGFORPLAYER", " ");
		localize_override("SPECIAL_OPS_WAITING_OTHER_PLAYER", " ");
	}
	ctx_entity_log();
	ctx_jitter_log();
	static unsigned count;
	if (ctx1_ready && ++count % 240 == 0 && count < 1200)
		ctx_dump("state");
	static int logged;
	if (!logged++)
		log_printf("native: first draw after the hooks: cg_init_done %d ready %d", cg_init_done, ctx1_ready);
	if (ctx1_ready || !cg_init_done || ++draws_after_init < 5)
		return;
	log_printf("native: starting context 1");
	ctx_seed_cgame();
	if (GetPrivateProfileIntA("native", "p1cmd", 0, settings_path)) {
		// player 1's view: the view angle accumulators (pitch 0x533d14, yaw 0x533d18, degrees) of context 1's block; the server turns them into his view with the
		// angles it set at his spawn, so this is relative to that (player 1 stood at yaw -77 with 0): [native] p1yaw=<degrees>
		float *acc = (float *)(ctx_store[1][4] + (0x533d14 - 0x533cb8));
		acc[0] = 0;
		acc[1] = (float)GetPrivateProfileIntA("native", "p1yawdeg", 60, settings_path);
	}
	if (GetPrivateProfileIntA("native", "p1cmd", 0, settings_path)) {
		// and player 0's: turned once by [native] p0turn degrees (default 241: from the spawn yaw -78 to the direction of player 1, 163) through his own view angle accumulator
		float *acc0 = (float *)(base + 0x533d14);
		acc0[1] += (float)GetPrivateProfileIntA("native", "p0turn", 241, settings_path);
	}
	ctx_swap(1);
	// not the whole cgame init (it also resets what is shared: effects, media, the renderer's per-level state): only its two per-client parts, on the copied block
	((void (*)(unsigned long long))(base + 0xb8830))(0);
	((void (*)(unsigned))(base + 0xb87b0))(0);
	ctx_swap(0);
	// the cgame's own idea of its local client number (rva 0x40caa4): the draw hides "my" entity by it and answers per-client visibility masks by it; context 1 runs as local client 0
	// but is client 1
	*(int *)(ctx_store[1][0] + (0x40caa4 - ranges[0].rva)) = GetPrivateProfileIntA("native", "ctx1lc", 1, settings_path);
	ctx1_ready = 1;
	log_printf("native: context 1 initialised");
	ctx_dump("after init");
}

// The server's copy of a local player's playerstate into the cl block (rva 0x535d38 is +0x2018 into the block at 0x533d20): for client 1 straight into context 1's copy, so no other thread
// ever sees a swapped block for it.
#define CL_BLOCK_PS 0x2018
#define PS_SIZE 0xaca4
static void ps_publish_hook(unsigned lc, const void *ps)
{
	if (lc == 0 && ctx_current == 0)
		return ps_publish_original(0, ps);
	ctx_ensure();
	// the playerstate of client lc belongs to context lc: into the live block when that context is the live one, else into its stored copy
	uint8_t *dest = (int)lc == ctx_current ? base + 0x535d38 : ctx_store[lc][5] + CL_BLOCK_PS;
	memcpy(dest, ps, PS_SIZE);
}

static void server_cmds_hook(unsigned long long lc, int *msg)
{
	if ((int)lc != 0)
		return; // client 1 gets no server commands yet
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	server_cmds_original(0, msg);
	if (was)
		ctx_swap(was);
}

// The server's snapshot message (entities, as shared lists) is parsed into the client's snapshot ring: once for each context, the message read again from its start.
static void snapshot_msg_hook(void *msg, int time)
{
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	uint8_t copy[0x30];
	memcpy(copy, msg, sizeof(copy));
	snapshot_msg_original(msg, time);
	if (ctx1_ready) {
		memcpy(msg, copy, sizeof(copy));
		ctx_swap(1);
		snapshot_msg_original(msg, time);
		ctx_swap(0);
	}
	if (was)
		ctx_swap(was);
}

// ---- player 1's pad ([native] p1pad=<0-3>, -1 none): read in context 1's client frame, with context 1 live.
// Stick left: movement (analog, into the usercmd by make_cmd_hook), stick right: the view (the view angle accumulators of context 1's block, rva 0x533d14 pitch / 0x533d18 yaw,
// degrees), the buttons run the game's own key bindings for context 1's kbuttons (rva 0xd1810 name -> index, 0xce450 run it for the local client with a key number no keyboard sends).
typedef struct {
	WORD buttons;
	BYTE lt, rt;
	SHORT lx, ly, rx, ry;
} xin_gamepad_t;
typedef struct {
	DWORD packet;
	xin_gamepad_t pad;
} xin_state_t;
static DWORD(WINAPI *xinput_get)(DWORD, xin_state_t *);
static int p1pad = -1;
static int pad_forward, pad_right; // -127..127, for the usercmd of this frame
static int pad_active;

static void pad_binding(const char *name, int down)
{
	char full[48];
	snprintf(full, sizeof(full), "%c%s", down ? '+' : '-', name);
	int index = ((int (*)(const char *))(base + 0xd1810))(full);
	if (!index) {
		static int logged[64];
		static int count;
		int already = 0;
		for (int i = 0; i < count; i++)
			already |= logged[i] == (int)full[1] * 256 + (int)full[2];
		if (!already && count < 64) {
			logged[count++] = (int)full[1] * 256 + (int)full[2];
			log_printf("native: pad: no binding '%s'", full);
		}
		return;
	}
	((void (*)(int, int, int))(base + 0xce450))(0, index, 0x1ff);
}

static void pad_frame(void)
{
	if (p1pad < 0)
		return;
	if (!xinput_get) {
		HMODULE m = LoadLibraryA("xinput1_4.dll");
		if (!m)
			m = LoadLibraryA("xinput1_3.dll");
		if (m)
			xinput_get = (DWORD(WINAPI *)(DWORD, xin_state_t *))GetProcAddress(m, "XInputGetState");
		if (!xinput_get) {
			log_printf("native: no XInput, player 1 has no pad");
			p1pad = -1;
			return;
		}
	}
	xin_state_t st;
	memset(&st, 0, sizeof(st));
	static DWORD last_tick;
	DWORD now = GetTickCount();
	float dt = last_tick ? (now - last_tick) / 1000.0f : 0.016f;
	last_tick = now;
	if (dt > 0.1f)
		dt = 0.1f;
	int have = xinput_get((DWORD)p1pad, &st) == 0;
	{ // QA: mw2coopakepad.txt "lx ly rx ry buttons lt rt" (stick values -32767..32767, buttons as the XInput mask) stands in for a pad
		static DWORD checked;
		static int fake_have;
		static int f[7];
		if (now - checked > 250) {
			checked = now;
			char path[MAX_PATH];
			lstrcpyA(path, settings_path);
			char *slash = strrchr(path, '\\');
			if (slash) {
				lstrcpyA(slash + 1, "fakepad.txt");
				FILE *fp = fopen(path, "r");
				fake_have = 0;
				if (fp) {
					fake_have = fscanf(fp, "%d %d %d %d %d %d %d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) == 7;
					fclose(fp);
				}
			}
		}
		if (fake_have) {
			st.pad.lx = (SHORT)f[0], st.pad.ly = (SHORT)f[1], st.pad.rx = (SHORT)f[2], st.pad.ry = (SHORT)f[3];
			st.pad.buttons = (WORD)f[4], st.pad.lt = (BYTE)f[5], st.pad.rt = (BYTE)f[6];
			have = 1;
		}
	}
	if (!have) { // no pad: no input
		pad_forward = pad_right = 0;
		pad_active = 0;
		return;
	}
	pad_active = 1;
	const float dead = 0.2f;
	float lx = st.pad.lx / 32767.0f, ly = st.pad.ly / 32767.0f, rx = st.pad.rx / 32767.0f, ry = st.pad.ry / 32767.0f;
	if (lx * lx + ly * ly < dead * dead)
		lx = ly = 0;
	if (rx * rx + ry * ry < dead * dead)
		rx = ry = 0;
	pad_forward = (int)(ly * 127.0f);
	pad_right = (int)(lx * 127.0f);
	float *acc = (float *)(base + 0x533d14); // context 1 is live here
	acc[1] -= rx * 160.0f * dt; // yaw
	acc[0] -= ry * 110.0f * dt; // pitch (down is positive)
	if (acc[0] > 80.0f)
		acc[0] = 80.0f;
	if (acc[0] < -80.0f)
		acc[0] = -80.0f;
	static const struct {
		WORD mask;
		const char *binding;
		const char *second;
	} map[] = {
	    {0x1000, "gostand", NULL}, // A
	    {0x2000, "stance", NULL},  // B
	    {0x4000, "reload", "activate"}, // X
	    {0x0100, "smoke", NULL},   // left bumper
	    {0x0200, "frag", NULL},    // right bumper
	    {0x0040, "breath_sprint", NULL}, // left stick
	    {0x0080, "melee", NULL},   // right stick
	};
	static WORD held;
	static int rt_held, lt_held;
	WORD buttons = st.pad.buttons;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		int was = (held & map[i].mask) != 0, is = (buttons & map[i].mask) != 0;
		if (was == is)
			continue;
		pad_binding(map[i].binding, is);
		if (map[i].second)
			pad_binding(map[i].second, is);
	}
	held = buttons;
	int rt = st.pad.rt > 80, lt = st.pad.lt > 80;
	if (rt != rt_held)
		pad_binding("attack", rt);
	if (lt != lt_held)
		pad_binding("speed_throw", lt);
	rt_held = rt;
	lt_held = lt;
}

// Mode 9: the engine is told the screen is a half (width x height/2) while the client frame records its views and its 2D: its viewport, the 2D placement and the text are made for the
// half (nothing is squeezed afterwards), drawn into the top rows of the frame; the compositor copies those rows into the half of the picture. Video globals (rva): 0x4113724 height,
// 0x411372c and 0x4358e84 the window height again, 0x4113738 aspect, 0x411373c pixel aspect (= height * aspect / width), the cgame's own aspect 0x43c640.
static float video_saved_f[3];
static int video_saved_i[3];
static int video_pushed;
// Mode 10: the engine's 2D screen placements (rva 0x5e0b80, three of 0x70 bytes: scaleVirtualToReal, scaleVirtualToFull, scaleRealToVirtual, viewport position and size, the viewable and the
// adjustable rectangles; the console's split screen sets the per-client one to the half it draws into) are given the half: a 1920 x height/2 viewport with the scale of that height, so the
// client lays its 2D out for a half and the text keeps its shape (the virtual canvas becomes wider, as on a 16:9 screen).
#define PLACE_RVA 0x5e0b80
#define PLACE_SIZE 0x70
static float place_saved[3][28];
static int place_pushed;
static void place_push(void)
{
	if (!comp_w || !comp_h)
		return;
	float w = (float)comp_w, h = (float)(comp_h / 2), s = h / 480.0f, vw = w / s;
	for (int n = 0; n < 3; n++) {
		float *p = (float *)(base + PLACE_RVA + n * PLACE_SIZE);
		if (!place_pushed) {
			if (p[8] != w || p[9] != (float)comp_h) {
				log_printf("native: screen placement %d is not the one expected (viewport %g x %g)", n, p[8], p[9]);
				return;
			}
		}
	}
	for (int n = 0; n < 3; n++) {
		float *p = (float *)(base + PLACE_RVA + n * PLACE_SIZE);
		if (!place_pushed)
			memcpy(place_saved[n], p, sizeof(place_saved[n]));
		int safe = place_saved[n][10] > 0; // the two with a safe-area margin keep it (7.5 %)
		float m = safe ? 0.075f : 0.0f;
		p[0] = p[1] = s;
		p[2] = w / 640.0f, p[3] = s;
		p[4] = p[5] = 1.0f / s;
		p[6] = p[7] = 0;
		p[8] = w, p[9] = h;
		p[10] = m * vw, p[11] = m * 480.0f;
		p[12] = vw - m * vw, p[13] = 480.0f - m * 480.0f;
		p[14] = m * w, p[15] = m * h;
		p[16] = w - m * w, p[17] = h - m * h;
		p[18] = 0, p[19] = 0, p[20] = vw, p[21] = 480.0f;
		p[22] = 0, p[23] = 0, p[24] = w, p[25] = h;
	}
	if (!place_pushed)
		log_printf("native: the 2D placements are made for a %gx%g half (scale %g)", w, h, s);
	place_pushed = 1;
}
static void place_pop(void)
{
	if (!place_pushed)
		return;
	for (int n = 0; n < 3; n++)
		memcpy(base + PLACE_RVA + n * PLACE_SIZE, place_saved[n], sizeof(place_saved[n]));
	place_pushed = 0;
}

static void video_push(void)
{
	if (mode == 10) {
		place_push();
		return;
	}
	if (mode != 9 || !comp_w || !comp_h)
		return;
	int *h1 = (int *)(base + 0x4113724), *h2 = (int *)(base + 0x411372c), *h3 = (int *)(base + 0x4358e84);
	float *a = (float *)(base + 0x4113738), *pa = (float *)(base + 0x411373c), *ca = (float *)(base + 0x43c640);
	int half = (int)(comp_h / 2);
	if (!video_pushed) { // the first time: what the engine had, to give back when the picture is not split any more
		video_saved_i[0] = *h1, video_saved_i[1] = *h2, video_saved_i[2] = *h3;
		video_saved_f[0] = *a, video_saved_f[1] = *pa, video_saved_f[2] = *ca;
		video_pushed = 1;
		log_printf("native: the engine's screen is %ux%d while the picture is split (was %d)", comp_w, half, video_saved_i[0]);
	}
	// kept as long as the picture is split, every frame (the render thread reads them later than the client frame ends, and a video restart writes them again)
	*h1 = *h2 = *h3 = half;
	*a = (float)comp_w / (float)half;
	*pa = 1.0f;
	*ca = *a;
	if (ctx_current == 0 && ctx_store[1][0]) // context 1's cgame block holds its own copy of the cgame's aspect
		*(float *)(ctx_store[1][0] + (0x43c640 - ranges[0].rva)) = *a;
}
static void video_pop(void)
{
	place_pop();
	if (!video_pushed)
		return;
	*(int *)(base + 0x4113724) = video_saved_i[0];
	*(int *)(base + 0x411372c) = video_saved_i[1];
	*(int *)(base + 0x4358e84) = video_saved_i[2];
	*(float *)(base + 0x4113738) = video_saved_f[0];
	*(float *)(base + 0x411373c) = video_saved_f[1];
	*(float *)(base + 0x43c640) = video_saved_f[2];
	if (ctx_store[1][0])
		*(float *)(ctx_store[1][0] + (0x43c640 - ranges[0].rva)) = video_saved_f[2];
	video_pushed = 0;
}

// Mode 5: the engine's whole client frame (rva 0xd5b00, called once per engine frame from the main loop at rva 0x1fc331) runs for context 0 on even frames and for context 1 on odd ones, so
// everything the client does in a frame (time, snapshot use, usercmds, the cgame draw, the 2D) happens in the right context. One scene per frame, as the renderer needs.
static void (*client_frame_original)(void);
static void client_frame_hook(void)
{
	static unsigned frame;
	frame_bottom = 0;
	{ // the scripts' switch: the dvar mw2coop_split_view is "1" once the intro is over
		uint8_t *d = ((uint8_t * (*)(const char *))(base + 0x26bdf0))("mw2coop_split_view");
		const char *v = d ? *(const char **)(d + 0x10) : NULL;
		int on = v && v[0] == '1';
		if (on != split_on)
			log_printf("native: split view %s", on ? "on" : "off");
		split_on = on;
	}
	static int nopredict = -1;
	if (nopredict < 0)
		nopredict = GetPrivateProfileIntA("native", "nopredict", 0, settings_path);
	uint8_t *np_dvar = *(uint8_t **)(base + 0x51e028); // cg_nopredict: context 1 shows the server's playerstate as it is (its own prediction fought the server)
	{ // [native] fov=<degrees>: the cg_fov (rva 0x51e1a8 holds the dvar; the angle across a 4:3 picture, 65 by default) while the picture is split: each half is a band of the full
	  // view (mode 6), so the field is widened to make up for the half height
		static int fov = -1;
		static float fov_saved = -1;
		if (fov < 0)
			fov = GetPrivateProfileIntA("native", "fov", (mode >= 8) ? 49 : 85, settings_path); // 85 = the console split screen: cg_fovScale 0.75 (maps/_load.gsc) on a half of 16:9 is about 100 x 38 degrees; a band of half the height cut from a view with cg_fov 85 is the same
		uint8_t *d = *(uint8_t **)(base + 0x51e1a8);
		if (fov > 0 && d) {
			float *v = (float *)(d + 0x10);
			if (split_on) {
				if (fov_saved < 0)
					fov_saved = *v;
				*v = (float)fov;
			} else if (fov_saved >= 0) {
				*v = fov_saved;
				fov_saved = -1;
			}
		}
	}
	static int nogun = -1;
	if (nogun < 0)
		nogun = GetPrivateProfileIntA("native", "nogun", 0, settings_path);
	uint8_t *gun_dvar = *(uint8_t **)(base + 0x51e120); // cg_drawGun
	if (mode >= 5 && ctx1_ready && split_on && (frame++ & 1)) {
		frame_bottom = 1;
		if (nogun && gun_dvar)
			*(int *)(gun_dvar + 0x10) = 0; // test: context 1 without the view model
		if (nopredict && np_dvar)
			*(int *)(np_dvar + 0x10) = 1;
		ctx_swap(1);
		pad_frame();
		if (split_on)
			video_push();
		client_frame_original();
		native_unswap_entities();
		ctx_swap(0);
		return;
	}
	if (nopredict && np_dvar)
		*(int *)(np_dvar + 0x10) = 0;
	if (nogun && gun_dvar)
		*(int *)(gun_dvar + 0x10) = 1;
	if (split_on && ctx1_ready)
		video_push();
	else if (!split_on)
		video_pop();
	client_frame_original();
}

// Diagnostics: the alpha of the co-op "waiting for the other player" text (rva 0x22eb70, called from the cgame's 2D at 0x94039).
static float (*wait_alpha_original)(int);
static float wait_alpha_hook(int lc)
{
	float a = wait_alpha_original(lc);
	static int logged;
	if ((a != 0.0f || !logged) && logged < 5) {
		logged++;
		log_printf("native: waiting alpha for lc %d = %f (coop byte %d, cls %d)", lc, a, *(base + 0x52fb5c), *(int *)(base + 0x533cb8));
	}
	return a;
}

// Context 1 has no mouse (its player has no device yet): the engine's mouse and joystick reads (rva 0xcf4b0, called at 0xcfd23 and 0xd0061) return no movement for it, so the
// real cursor only turns player 0.
static void (*mouse_read_original)(long long, float *, float *);
static void mouse_read_hook(long long state, float *x, float *y)
{
	if (ctx_current == 1) {
		*x = 0;
		*y = 0;
		return;
	}
	mouse_read_original(state, x, y);
}
static void (*mouse_read2_original)(long long, float *, float *);
static void mouse_read2_hook(long long state, float *x, float *y)
{
	if (ctx_current == 1) {
		*x = 0;
		*y = 0;
		return;
	}
	mouse_read2_original(state, x, y);
}

// The keyboard and the pad reach context 1 too (the keys are read by the same input code): its usercmd, made by 0xd03a0 (called at 0xcf8b1), loses its buttons and movement,
// so that the player who has no device stands still and his prediction agrees with the server.
static uint8_t *(*make_cmd_original)(uint8_t *, int);
static uint8_t *make_cmd_hook(uint8_t *out, int a)
{
	uint8_t *cmd = make_cmd_original(out, a);
	if (ctx_current == 1 && cmd) {
		if (p1pad >= 0 && pad_active) {
			cmd[0x1a] = (uint8_t)(int8_t)pad_forward; // usercmd_s: forwardmove +0x1a, rightmove +0x1b, upmove +0x1c, downmove +0x1d
			cmd[0x1b] = (uint8_t)(int8_t)pad_right;
		} else {
			*(int *)(cmd + 4) = 0;
			cmd[0x1a] = cmd[0x1b] = cmd[0x1c] = cmd[0x1d] = 0;
		}
	}
	return cmd;
}

// The simulation step and the hand-over of its results (rva 0x2343a0) run from inside the client frame (called at 0xb8d50): in context 1's frame they would write context 0's data
// into the live (context 1's) client block. They run with context 0 live; the hand-over hooks above put each client's data where its context keeps it.
static void (*server_step_original)(void);
static void server_step_hook(void)
{
	if (ctx_current == 1) {
		ctx_swap(0);
		server_step_original();
		ctx_swap(1);
		return;
	}
	server_step_original();
}

// Player 1 (local client 1) has no input device yet: every simulation step he gets the usercmd of player 0 with the buttons and the movement cleared and the view turned by
// [native] p1yaw (a 16-bit angle, 0x8000 = half a turn), through the engine's own SV_ClientThink analogue (rva 0x22d000, client_t[n] + 0x1630 holds the last cmd).
#define RVA_SV_CLIENTS_CMD 0x19ada98
static int p1_yaw;
static void usercmd_step_inner(int lc)
{
	if (lc == 0) {
		usercmd_step_original(0);
		return;
	}
	if (!GetPrivateProfileIntA("native", "p1cmd", 0, settings_path))
		return;
	{
		static unsigned seen;
		if (++seen % 60 == 0 && seen < 3000) {
			const uint8_t *c0 = base + RVA_SV_CLIENTS_CMD + 0x1630, *c1 = base + RVA_SV_CLIENTS_CMD + 0x1678 + 0x1630;
			log_printf("native: step %u ctx %d: client 0 cmd t %d yaw %d pitch %d | client 1 cmd t %d yaw %d pitch %d | ctx1 acc yaw %.1f pitch %.1f", seen, ctx_current, *(int *)c0, *(int *)(c0 + 12),
				   *(int *)(c0 + 8), *(int *)c1, *(int *)(c1 + 12), *(int *)(c1 + 8), ctx_store[1][4] ? *(float *)(ctx_store[1][4] + 0x60) : -1.0f,
				   ctx_store[1][4] ? *(float *)(ctx_store[1][4] + 0x5c) : -1.0f);
		}
	}
	if (ctx1_ready && ctx_current == 0 && ctx_store[1][5]) {
		// context 1's own client frame makes its usercmds in its cl block (ring of 128 x 0x40 at +0, cmdNumber at +0x2000, the number sent at +0x2004): player 1 gets them
		// through the engine's SV_ClientThink analogue, as player 0 gets his through rva 0xd02a0
		uint8_t *cl = ctx_store[1][5];
		int number = *(int *)(cl + 0x2000);
		int *sent = (int *)(cl + 0x2004);
		if (number - *sent > 32)
			*sent = number - 32;
		for (int n = *sent + 1; n <= number; n++)
			((void (*)(int, void *))(base + 0x22d000))(1, cl + (n & 127) * 0x40);
		*sent = number;
		return;
	}
	static int logged;
	uint8_t cmd[0x40];
	memcpy(cmd, base + RVA_SV_CLIENTS_CMD + 0x1630, sizeof(cmd)); // player 0's last cmd
	if (!*(int *)cmd) // not set yet
		return;
	static unsigned steps;
	if (++steps % 400 == 0 && steps < 6000) {
		char hex[0x40 * 3 + 1];
		for (int i = 0; i < 0x40; i++)
			snprintf(hex + i * 3, 4, "%02x ", cmd[i]);
		log_printf("native: player 0's cmd: %s", hex);
	}
	*(int *)(cmd + 4) = 0; // no buttons
	cmd[0x1a] = cmd[0x1b] = cmd[0x1c] = cmd[0x1d] = 0; // no movement
	if (!logged) {
		p1_yaw = GetPrivateProfileIntA("native", "p1yaw", 0, settings_path);
		log_printf("native: player 1 cmd from player 0's: serverTime %d, angles %d %d %d, p1yaw %x", *(int *)cmd, *(int *)(cmd + 8), *(int *)(cmd + 12), *(int *)(cmd + 16), p1_yaw);
		logged = 1;
	}
	*(int *)(cmd + 8) = 0; // a fixed view (pitch 0, yaw p1yaw, roll 0): player 0's mouse must not turn player 1
	*(int *)(cmd + 12) = p1_yaw;
	*(int *)(cmd + 16) = 0;
	((void (*)(int, void *))(base + 0x22d000))(1, cmd);
}

// These server-side hand-overs can be reached while context 1 is the live one (they run from inside the client frame): they always run with context 0 live.
static void usercmd_step_hook(int lc)
{
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	usercmd_step_inner(lc);
	if (was)
		ctx_swap(was);
}

// Diagnostics ([native] entdraw=1, skipent=<n>): the entities the cgame adds to the scene (rva 0x9c960), logged for one frame of context 1; skipent drops one of them.
static const uint8_t add_entity_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00};
static void (*add_entity_original)(int);
static int skip_entity = 1; // context 1 is player 1: his own entity is not drawn (the cgame hides "my" entity by the local client number, which is 0 in context 1)
static int log_entities;
static int swap_entities = 1;
static int entities_swapped;
void native_unswap_entities(void)
{
	if (!entities_swapped)
		return;
	uint8_t *e0 = base + 0x441070, *e1 = e0 + 0x1a8, t[0x1a8];
	memcpy(t, e0, sizeof(t));
	memcpy(e0, e1, sizeof(t));
	memcpy(e1, t, sizeof(t));
	entities_swapped = 0;
}
static void add_entity_hook(int n)
{
	static unsigned frames;
	static int logging;
	if (ctx_current == 1) {
		if (swap_entities && n == 0 && !entities_swapped) {
			// the cgame hides the entity numbered like the local client (0) as "mine"; context 1 is local client 0 but player 1: his entity and player 0's trade places for the draw
			uint8_t *e0 = base + 0x441070, *e1 = e0 + 0x1a8, t[0x1a8];
			memcpy(t, e0, sizeof(t));
			memcpy(e0, e1, sizeof(t));
			memcpy(e1, t, sizeof(t));
			entities_swapped = 1;
		}
		if (log_entities && n == 0 && logging == 0 && ++frames == 700)
			logging = 1;
		if (logging == 1) {
			const uint8_t *e = base + 0x441070 + (size_t)n * 0x1a8;
			log_printf("native: ctx1 adds entity %d type %d flags %08x", n, e[0xe0], *(unsigned *)(e + 0xec));
		}
		if (n == skip_entity)
			return;
		// the per-client "hide from" bits of an entity (flags & 0x3f0, bit 4 + local client) are made for local client numbers: context 1 is local client 0 but is client 1
		// ([native] swapbits=0 turns it off)
		static int swapbits = -1;
		if (swapbits < 0)
			swapbits = GetPrivateProfileIntA("native", "swapbits", 1, settings_path);
		unsigned *flags = (unsigned *)(base + 0x441070 + (size_t)n * 0x1a8 + 0xec);
		unsigned saved = *flags;
		if (swapbits) {
			unsigned b0 = saved & 0x10, b1 = saved & 0x20;
			*flags = (saved & ~0x30u) | (b0 ? 0x20 : 0) | (b1 ? 0x10 : 0);
		}
		add_entity_original(n);
		*flags = saved;
		return;
	}
	add_entity_original(n);
}

// Diagnostics / fix for the 2D texts that both halves draw (the objective text, subtitles, "who stands in front" hints are global): [native] skiphud=<mask> drops, in context 1's
// frames, the HUD parts called at the end of the cgame draw: bit 0 rva 0xc6070, bit 1 rva 0xc14e0, bit 2 rva 0x94d70, bit 3 rva 0xc5b70.
static int skip_hud_mask;
static void (*hud_c6070_original)(unsigned);
static void (*hud_c14e0_original)(unsigned);
static void (*hud_94d70_original)(unsigned);
static void (*hud_c5b70_original)(unsigned);
static void hud_c6070_hook(unsigned lc) { if (!(ctx_current == 1 && (skip_hud_mask & 1))) hud_c6070_original(lc); }
static void hud_c14e0_hook(unsigned lc) { if (!(ctx_current == 1 && (skip_hud_mask & 2))) hud_c14e0_original(lc); }
static void hud_94d70_hook(unsigned lc) { if (!(ctx_current == 1 && (skip_hud_mask & 4))) hud_94d70_original(lc); }
static void hud_c5b70_hook(unsigned lc) { if (!(ctx_current == 1 && (skip_hud_mask & 8))) hud_c5b70_original(lc); }

static void contexts_install(void)
{
	static const struct {
		uint32_t site;
		void *hook;
		void **original;
	} sites[] = {
	    {0x2336c7, cg_init_all_hook, (void **)&cg_init_all_original},
	    {0x2343b8, snapshot_take_hook, (void **)&snapshot_take_original},
	    {0x2346ca, ps_publish_hook, (void **)&ps_publish_original},
	    {0x234a02, server_cmds_hook, (void **)&server_cmds_original},
	    {0x15ce84, usercmd_step_hook, (void **)&usercmd_step_original},
	    {0x234925, snapshot_msg_hook, (void **)&snapshot_msg_original},
	    {0x1fc331, client_frame_hook, (void **)&client_frame_original},
	    {0xbd093, hud_c6070_hook, (void **)&hud_c6070_original},
	    {0xbd09a, hud_c14e0_hook, (void **)&hud_c14e0_original},
	    {0xbd0a1, hud_94d70_hook, (void **)&hud_94d70_original},
	    {0xbd034, hud_c5b70_hook, (void **)&hud_c5b70_original},
	    {0x94039, wait_alpha_hook, (void **)&wait_alpha_original},
	    {0xcfd23, mouse_read_hook, (void **)&mouse_read_original},
	    {0xcf8b1, make_cmd_hook, (void **)&make_cmd_original},
	    {0xb8d50, server_step_hook, (void **)&server_step_original},
	    {0xd0061, mouse_read2_hook, (void **)&mouse_read2_original},
	};
	int enabled = GetPrivateProfileIntA("native", "sites", 0x7fffffff, settings_path);
	for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
		if ((enabled & (1 << i)) && !hook_call(base + sites[i].site, sites[i].hook, sites[i].original))
			log_printf("native: no call at rva 0x%x, context hook %zu off", sites[i].site, i);
	swap_entities = GetPrivateProfileIntA("native", "swapent", 1, settings_path);
	skip_entity = GetPrivateProfileIntA("native", "skipent", swap_entities ? -1 : 1, settings_path);
	log_entities = GetPrivateProfileIntA("native", "entdraw", 0, settings_path);
	{
		if (!hook_function(base + 0x9c960, add_entity_bytes, sizeof(add_entity_bytes), add_entity_hook, (void **)&add_entity_original))
			log_printf("native: add entity bytes differ");
	}
	p1pad = GetPrivateProfileIntA("native", "p1pad", -1, settings_path);
	skip_hud_mask = GetPrivateProfileIntA("native", "skiphud", 0, settings_path);
	ctx_on = 1;
	log_printf("native: client contexts on (call sites redirected)");
}

// The scripts decide "co-op" from the dvar `coop` (rva 0x1905308, a bool whose script-side writes are refused); the engine's own co-op byte (rva 0x52fb5c) stays 0 so that the engine takes its
// local-players branch. [native] coopdvar=1 keeps the dvar at 1 (written from a thread, every 20 ms) so that the level scripts, the mod's among them, run their two-player code.
static DWORD WINAPI coop_dvar_thread(LPVOID unused)
{
	(void)unused;
	for (;;) {
		uint8_t *dvar = *(uint8_t **)(base + 0x1905308);
		if (dvar)
			*(int *)(dvar + 0x10) = 1;
		Sleep(20);
	}
	return 0;
}

static void players_install(int n)
{
	if (n < 1 || n > 4)
		return;
	if (n == 1) { // diagnostics: the hooks without the second player
		if (GetPrivateProfileIntA("native", "ctx", 1, settings_path))
			contexts_install();
		return;
	}
	uint8_t *at = base + RVA_LOCAL_PLAYERS_STORE;
	if (at[0] != 0xc7 || at[1] != 0x05 || at[6] != 1 || at[7] || at[8] || at[9]) {
		log_printf("native: unexpected code at the local players store, players=%d ignored", n);
		return;
	}
	DWORD old;
	if (!VirtualProtect(at + 6, 1, PAGE_EXECUTE_READWRITE, &old)) {
		log_printf("native: VirtualProtect failed (%lu)", GetLastError());
		return;
	}
	at[6] = (uint8_t)n;
	VirtualProtect(at + 6, 1, old, &old);
	log_printf("native: the number of local players is %d (patched G_InitGame)", n);
	hook_jump(base + RVA_SV_ADD_COMMAND, 14, sv_add_command, 0xcc);
	client_n_install();
	if (GetPrivateProfileIntA("native", "listplayers", 0, settings_path))
		list_players_install();
	if (GetPrivateProfileIntA("native", "coopdvar", 0, settings_path))
		CreateThread(NULL, 0, coop_dvar_thread, NULL, 0, NULL);
	if (GetPrivateProfileIntA("native", "ctx", 1, settings_path))
		contexts_install();
	log_printf("native: server commands to client 1 are dropped (no client behind it yet)");
}

void native_install(void)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\settings.ini");
	lstrcpyA(settings_path, path);
	mode = GetPrivateProfileIntA("native", "experiment", 0, path);
	base = (uint8_t *)GetModuleHandleA(NULL);
	players_install(GetPrivateProfileIntA("native", "players", 0, path));
	if (GetPrivateProfileIntA("native", "logcbuf", 0, path) && !hook_function(base + 0x1ee890, cbuf_bytes, sizeof(cbuf_bytes), cbuf_hook, (void **)&cbuf_original))
		log_printf("native: Cbuf_AddText bytes differ");
	if (!mode && !ctx_on)
		return;
	if (!hook_function(base + RVA_BEGIN_FRAME, begin_frame_bytes, sizeof(begin_frame_bytes), begin_frame_hook, (void **)&begin_frame_original) ||
	    !hook_function(base + RVA_CG_DRAW, cg_draw_bytes, sizeof(cg_draw_bytes), cg_draw_hook, (void **)&cg_draw_original)) {
		log_printf("native: unexpected code at the cgame draw or begin frame, experiment off");
		mode = 0;
		return;
	}
	if (mode == 10) {
		if (*(base + 0x5c5bf) != 0xe8 || !hook_call(base + 0x5c5bf, (void *)scene2d_hook, (void **)&scene2d_original)) {
			log_printf("native: unexpected code at the scene's 2D pass, mode 10 off");
			mode = 0;
			return;
		}
	}
	log_printf("native: experiment %d on (the cgame draw runs twice per frame, one begin frame)", mode);
}
#else
void native_install(void)
{
}
#endif
