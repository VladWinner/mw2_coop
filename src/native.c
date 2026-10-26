// Native split screen, experiment 1 (docs/native-splitscreen-plan.md, phase 3 first): can the renderer take two views in one frame?
//
// The cgame's draw-active-frame (rva 0xbca00, local client number first) builds the view and queues the 3D scene; the renderer's view setup (rva 0x7e40, from
// rva 0x8150) reads the dvar r_subwindow, a vec4 (left, right, top, bottom: fractions of the picture), and corrects the projection for that rectangle. This
// hook runs the draw twice per frame, the first with the top half as r_subwindow and the second with the bottom half. settings.ini [native] experiment=1
// switches it on; with 2 the second view is turned around (yaw + 180) to show that the views are drawn separately. 64-bit only for now.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "hook.h"
#include "log.h"
#include "native.h"

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

void native_install(void)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\settings.ini");
	mode = GetPrivateProfileIntA("native", "experiment", 0, path);
	if (!mode)
		return;
	base = (uint8_t *)GetModuleHandleA(NULL);
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
