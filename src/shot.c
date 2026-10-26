// QA screenshots without touching the desktop: each machine saves its own game frame. Copying the
// game window from the screen (tools/shot_game.ps1) has to bring it to the front, which takes the
// desktop from whoever sits at the PC, and this build has no screenshot console command. A request
// is carried out at the next EndScene of the game's Direct3D 9 device (IDirect3DDevice9 * at rva
// 0x410fd20: the frame code calls its TestCooperativeLevel at rva 0x12180, and the frame's end at
// rva 0x12270 its EndScene; the device's own Present is not used, a hook there never ran): the back
// buffer, the finished frame, goes to system memory and into <game dir>\mw2coop\shots\<name>.bmp.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "shot.h"

#define RVA_D3D_DEVICE 0x410fd20
#define VT_END_SCENE   42 // IDirect3DDevice9::EndScene

typedef HRESULT(STDMETHODCALLTYPE *end_scene_t)(IDirect3DDevice9 *);
static end_scene_t end_scene_orig;
static CRITICAL_SECTION lock;
static char pending[96];
static char dir[MAX_PATH];

static int write_bmp(const char *path, const uint8_t *bits, int pitch, int w, int h)
{
	HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return 0;
	BITMAPINFOHEADER info = {sizeof(info), w, -h, 1, 32, BI_RGB, (DWORD)(w * h * 4), 0, 0, 0, 0};
	BITMAPFILEHEADER head = {0x4d42, (DWORD)(sizeof(head) + sizeof(info) + info.biSizeImage), 0, 0,
				 (DWORD)(sizeof(head) + sizeof(info))};
	DWORD n;
	WriteFile(f, &head, sizeof(head), &n, NULL);
	WriteFile(f, &info, sizeof(info), &n, NULL);
	for (int y = 0; y < h; y++)
		WriteFile(f, bits + (size_t)y * pitch, (DWORD)w * 4, &n, NULL);
	CloseHandle(f);
	return 1;
}

// on the render thread, at EndScene: the frame about to be shown
static void save(IDirect3DDevice9 *dev, const char *name)
{
	IDirect3DSurface9 *back = NULL, *flat = NULL, *sys = NULL, *src;
	D3DSURFACE_DESC desc;
	const char *why = "no back buffer";
	if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		goto done;
	IDirect3DSurface9_GetDesc(back, &desc);
	why = "format";
	if (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8)
		goto done;
	src = back;
	if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) { // resolve first
		why = "resolve";
		if (FAILED(IDirect3DDevice9_CreateRenderTarget(dev, desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE,
							       0, FALSE, &flat, NULL)) ||
		    FAILED(IDirect3DDevice9_StretchRect(dev, back, NULL, flat, NULL, D3DTEXF_NONE)))
			goto done;
		src = flat;
	}
	why = "copy";
	if (FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(dev, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
								 &sys, NULL)) ||
	    FAILED(IDirect3DDevice9_GetRenderTargetData(dev, src, sys)))
		goto done;
	D3DLOCKED_RECT rect;
	if (FAILED(IDirect3DSurface9_LockRect(sys, &rect, NULL, D3DLOCK_READONLY)))
		goto done;
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s%s.bmp", dir, name);
	int ok = write_bmp(path, rect.pBits, rect.Pitch, (int)desc.Width, (int)desc.Height);
	IDirect3DSurface9_UnlockRect(sys);
	why = ok ? NULL : "file";
done:
	if (why)
		log_printf("shot: %s not saved (%s)", name, why);
	else
		log_printf("shot: %s (%ux%u)", name, desc.Width, desc.Height);
	if (sys)
		IDirect3DSurface9_Release(sys);
	if (flat)
		IDirect3DSurface9_Release(flat);
	if (back)
		IDirect3DSurface9_Release(back);
}

// for src/native.c: the frame about to be shown, saved right now (the render thread, at EndScene)
void shot_save_now(void *dev, const char *name)
{
	save((IDirect3DDevice9 *)dev, name);
}

static HRESULT STDMETHODCALLTYPE end_scene_hook(IDirect3DDevice9 *dev)
{
	if (pending[0]) {
		char name[sizeof(pending)];
		EnterCriticalSection(&lock);
		lstrcpynA(name, pending, sizeof(name));
		pending[0] = 0;
		LeaveCriticalSection(&lock);
		if (name[0])
			save(dev, name);
	}
	return end_scene_orig(dev);
}

// The device exists once the game window is up; its vtable (shared by every device of the class)
// gets our EndScene the first time a shot is asked for.
static int hook(void)
{
	if (end_scene_orig)
		return 1;
	IDirect3DDevice9 *dev = *(IDirect3DDevice9 **)((uint8_t *)GetModuleHandleA(NULL) + RVA_D3D_DEVICE);
	if (!dev)
		return 0;
	void **vt = *(void ***)dev;
	DWORD old;
	if (!VirtualProtect(&vt[VT_END_SCENE], sizeof(void *), PAGE_READWRITE, &old))
		return 0;
	end_scene_orig = (end_scene_t)vt[VT_END_SCENE];
	vt[VT_END_SCENE] = (void *)end_scene_hook;
	VirtualProtect(&vt[VT_END_SCENE], sizeof(void *), old, &old);
	log_printf("shot: EndScene hooked");
	return 1;
}

void shot_request(const char *name)
{
	if (!hook()) {
		log_printf("shot: %s not saved (no Direct3D device yet)", name);
		return;
	}
	char clean[sizeof(pending)];
	int n = 0;
	for (const char *c = name; *c && n < (int)sizeof(clean) - 1; c++)
		clean[n++] = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '-' ? *c : '_';
	clean[n] = 0;
	EnterCriticalSection(&lock);
	lstrcpynA(pending, clean, sizeof(pending));
	LeaveCriticalSection(&lock);
}

void shot_install(void)
{
	InitializeCriticalSection(&lock);
	DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != '\\')
		n--;
	dir[n] = 0;
	lstrcatA(dir, "mw2coop\\shots\\");
	CreateDirectoryA(dir, NULL);
}
