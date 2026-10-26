// Native split screen, experiment 1 (docs/native-splitscreen-plan.md, phase 3 first): can the renderer take two views in one frame?
//
// The cgame's draw-active-frame (rva 0xbca00, local client number first) builds the view and queues the 3D scene; the renderer's view setup (rva 0x7e40, from
// rva 0x8150) reads the dvar r_subwindow, a vec4 (left, right, top, bottom: fractions of the picture), and corrects the projection for that rectangle. This
// hook runs the draw twice per frame, the first with the top half as r_subwindow and the second with the bottom half. settings.ini [native] experiment=1
// switches it on; with 2 the second view is turned around (yaw + 180) to show that the views are drawn separately. 64-bit only for now.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hook.h"
#include "log.h"
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
static volatile LONG ring_head, ring_tail; // frames queued by the main thread, and the halves they were drawn into, in order
static IDirect3DDevice9 *comp_device;
static IDirect3DTexture9 *comp_tex[2];
static IDirect3DSurface9 *comp_surf[2];
static int comp_have[2];
static UINT comp_w, comp_h;

static void comp_release(void)
{
	for (int i = 0; i < 2; i++) {
		if (comp_surf[i])
			IDirect3DSurface9_Release(comp_surf[i]);
		if (comp_tex[i])
			IDirect3DTexture9_Release(comp_tex[i]);
		comp_surf[i] = NULL, comp_tex[i] = NULL, comp_have[i] = 0;
	}
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
		log_printf("native: compositor on, %ux%u picture, two %ux%u halves", d.Width, d.Height, d.Width, d.Height / 2);
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
static void before_present(int via, IDirect3DDevice9 *dev)
{
	InterlockedIncrement(&present_hits[via]);
	if (compose_via < 0)
		compose_via = via;
	if (compose_via != via || ring_tail == ring_head)
		return;
	int half = (int)ring[ring_tail & 7];
	InterlockedIncrement(&ring_tail);
	LONG pops = InterlockedIncrement(&pops_total);
	compose(dev, half);
	if (pops == 600 || pops == 1200) { // the composed picture, for the QA of the experiment
		char name[32];
		snprintf(name, sizeof(name), "native-%ld", (long)pops);
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

static void patch_slot(void **slot, void *hook, void **orig)
{
	DWORD old;
	if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
		return;
	*orig = *slot;
	*slot = hook;
	VirtualProtect(slot, sizeof(void *), old, &old);
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
	IDirect3DDevice9Ex *ex = NULL;
	if (SUCCEEDED(IDirect3DDevice9_QueryInterface(dev, &iid_device9ex, (void **)&ex)) && ex) {
		patch_slot(&vt[121], (void *)present_ex_hook, (void **)&present_ex_orig); // PresentEx: the slot of the Ex interface
		IDirect3DDevice9Ex_Release(ex);
	}
	IDirect3DSwapChain9 *sc = NULL;
	if (SUCCEEDED(IDirect3DDevice9_GetSwapChain(dev, 0, &sc)) && sc) {
		void **svt = *(void ***)sc;
		patch_slot(&svt[3], (void *)sc_present_hook, (void **)&sc_present_orig);
		IDirect3DSwapChain9_Release(sc);
	}
	log_printf("native: Present hooks set (device %p, Ex %s)", (void *)dev, present_ex_orig ? "yes" : "no");
}

// the second draw of the frame must not start a new frame: it adds its view to the one the first draw began
static void begin_frame_hook(void)
{
	if (second_pass)
		return;
	begin_frame_original();
}

static void cg_draw_hook(unsigned local_client, int a, int b)
{
	float *sw = subwindow();
	if (!mode || !sw || second_pass) {
		cg_draw_original(local_client, a, b);
		return;
	}
	float saved[4];
	memcpy(saved, sw, sizeof(saved));
	if (mode == 2 || mode == 3) { // alternate frames: one scene per frame, the top half on even frames and the bottom half on odd ones
		static unsigned frame;
		int bottom = (int)(frame++ & 1);
		sw[0] = 0, sw[1] = 1, sw[2] = bottom ? 0.5f : 0, sw[3] = bottom ? 1 : 0.5f;
		comp_hook();
		if (the_device && ring_head - ring_tail < 6) { // the half this frame is drawn into: the frame's end composes it
			ring[ring_head & 7] = bottom;
			InterlockedIncrement(&ring_head);
			InterlockedIncrement(&pushes_total);
		}
		cg_draw_original(mode == 3 && bottom ? 1u : local_client, a, b); // mode 3: the odd frames run the cgame for LOCAL CLIENT 1
		memcpy(sw, saved, sizeof(saved));
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

static void players_install(int n)
{
	if (n < 2 || n > 4)
		return;
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
	mode = GetPrivateProfileIntA("native", "experiment", 0, path);
	base = (uint8_t *)GetModuleHandleA(NULL);
	players_install(GetPrivateProfileIntA("native", "players", 0, path));
	if (!mode)
		return;
	if (!hook_function(base + RVA_BEGIN_FRAME, begin_frame_bytes, sizeof(begin_frame_bytes), begin_frame_hook, (void **)&begin_frame_original) ||
	    !hook_function(base + RVA_CG_DRAW, cg_draw_bytes, sizeof(cg_draw_bytes), cg_draw_hook, (void **)&cg_draw_original)) {
		log_printf("native: unexpected code at the cgame draw or begin frame, experiment off");
		mode = 0;
		return;
	}
	log_printf("native: experiment %d on (the cgame draw runs twice per frame, one begin frame)", mode);
}
#else
void native_install(void)
{
}
#endif
