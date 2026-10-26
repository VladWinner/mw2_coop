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
	if (pops == 2000 || pops == 2600) { // the composed picture, for the QA of the experiment
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

static int ctx_on;
static int frame_bottom; // mode 5: this client frame is context 1's (the bottom half)
static int ctx1_ready;
static void ctx_swap(int to);
static void ctx_after_draw(void);

static void cg_draw_hook(unsigned local_client, int a, int b)
{
	float *sw = subwindow();
	if (!mode || !sw || second_pass) {
		cg_draw_original(local_client, a, b);
		if (ctx_on)
			ctx_after_draw();
		return;
	}
	float saved[4];
	memcpy(saved, sw, sizeof(saved));
	if (mode == 2 || mode == 3 || mode == 4 || mode == 5) { // alternate frames: one scene per frame, the top half on even frames and the bottom half on odd ones
		static unsigned frame;
		int bottom = mode == 5 ? frame_bottom : (int)(frame++ & 1); // mode 5: the client frame (below) says whose frame it is
		sw[0] = 0, sw[1] = 1, sw[2] = bottom ? 0.5f : 0, sw[3] = bottom ? 1 : 0.5f;
		comp_hook();
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
static char settings_path[MAX_PATH];

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
	fn();
	if (ctx1_ready) {
		ctx_swap(1);
		fn();
		ctx_swap(0);
	}
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

static void ctx_after_draw(void)
{
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
	ctx_swap(1);
	// not the whole cgame init (it also resets what is shared: effects, media, the renderer's per-level state): only its two per-client parts, on the copied block
	((void (*)(unsigned long long))(base + 0xb8830))(0);
	((void (*)(unsigned))(base + 0xb87b0))(0);
	ctx_swap(0);
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
	if (lc == 0)
		return ps_publish_original(0, ps);
	ctx_ensure();
	memcpy(ctx_store[1][5] + CL_BLOCK_PS, ps, PS_SIZE);
}

static void server_cmds_hook(unsigned long long lc, int *msg)
{
	if ((int)lc == 0)
		return server_cmds_original(0, msg); // client 1 gets no server commands yet
}

// The server's snapshot message (entities, as shared lists) is parsed into the client's snapshot ring: once for each context, the message read again from its start.
static void snapshot_msg_hook(void *msg, int time)
{
	uint8_t copy[0x30];
	memcpy(copy, msg, sizeof(copy));
	snapshot_msg_original(msg, time);
	if (ctx1_ready) {
		memcpy(msg, copy, sizeof(copy));
		ctx_swap(1);
		snapshot_msg_original(msg, time);
		ctx_swap(0);
	}
}

// Mode 5: the engine's whole client frame (rva 0xd5b00, called once per engine frame from the main loop at rva 0x1fc331) runs for context 0 on even frames and for context 1 on odd ones, so
// everything the client does in a frame (time, snapshot use, usercmds, the cgame draw, the 2D) happens in the right context. One scene per frame, as the renderer needs.
static void (*client_frame_original)(void);
static void client_frame_hook(void)
{
	static unsigned frame;
	frame_bottom = 0;
	if (mode == 5 && ctx1_ready && (frame++ & 1)) {
		frame_bottom = 1;
		ctx_swap(1);
		client_frame_original();
		ctx_swap(0);
		return;
	}
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

// Player 1 (local client 1) has no input device yet: every simulation step he gets the usercmd of player 0 with the buttons and the movement cleared and the view turned by
// [native] p1yaw (a 16-bit angle, 0x8000 = half a turn), through the engine's own SV_ClientThink analogue (rva 0x22d000, client_t[n] + 0x1630 holds the last cmd).
#define RVA_SV_CLIENTS_CMD 0x19ada98
static int p1_yaw;
static void usercmd_step_hook(int lc)
{
	if (lc == 0) {
		usercmd_step_original(0);
		return;
	}
	if (!GetPrivateProfileIntA("native", "p1cmd", 0, settings_path))
		return;
	static int logged;
	uint8_t cmd[0x40];
	memcpy(cmd, base + RVA_SV_CLIENTS_CMD + 0x1630, sizeof(cmd)); // player 0's last cmd
	if (!*(int *)cmd) // not set yet
		return;
	*(int *)(cmd + 4) = 0; // no buttons
	cmd[0x1c] = cmd[0x1d] = 0; // no movement
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
	    {0x94039, wait_alpha_hook, (void **)&wait_alpha_original},
	};
	int enabled = GetPrivateProfileIntA("native", "sites", 511, settings_path);
	for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
		if ((enabled & (1 << i)) && !hook_call(base + sites[i].site, sites[i].hook, sites[i].original))
			log_printf("native: no call at rva 0x%x, context hook %zu off", sites[i].site, i);
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
	log_printf("native: experiment %d on (the cgame draw runs twice per frame, one begin frame)", mode);
}
#else
void native_install(void)
{
}
#endif
