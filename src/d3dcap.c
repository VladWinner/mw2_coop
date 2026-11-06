// The game's Direct3D 9 device without knowing where the game stores it, and the extended Direct3D 9 for it ([display] d3d9ex).
// 32 bits: DllMain runs while the exe's imports are loaded (d3d9.dll is one of them, docs/32bit-plan.md), before the game's code runs: the first bytes of d3d9.dll's Direct3DCreate9 get a jump
// to our function, which calls the real one and replaces the CreateDevice entry of the IDirect3D9 object's vtable with ours; ours calls the real CreateDevice and keeps the device it returns.
// A hook installed later (the Steam overlay's) finds our entries where the originals were and calls them.
// The extended interface (Direct3D 9Ex): with it a window that loses the focus (Alt+Tab) does not lose its device, so nothing has to be reset and the render thread never waits for the window
// to come back. Without it, a game window in the background in exclusive fullscreen left the render thread blocked inside d3d9.dll: in co-op the partner got "no signal from the other
// participant for 10 s" and the game that had been left crashed afterwards, and in split screen the Reset on the way back ended in a stack overflow inside the Steam overlay (v0.6.0 reports,
// 2026-11-05). Our function then makes the object with Direct3DCreate9Ex (an IDirect3D9Ex is an IDirect3D9 with more methods) and the device with CreateDeviceEx; Direct3D 9Ex has no
// managed pool, so the vtable entries that create textures and buffers turn D3DPOOL_MANAGED into the default pool with dynamic usage (the same way the iw4x client does it, which has
// this as its option r_useD3D9Ex). Known limits (2026-11-05): a vid_restart with it on stalls (the new device is made, the menus never come back; the default pool resources of the old
// device are not the reason: the game makes a new device), a texture whose format the driver will not make dynamic is made without it and cannot be locked (not seen in any test run),
// and the 32-bit build is not tested. 64 bits: the exe's import table entry for Direct3DCreate9 is pointed at our function (the same code from there on).
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <string.h>

#include "d3dcap.h"
#include "hook.h"
#include "log.h"
#include "settings.h"

#define VT_CREATE_DEVICE         16 // IDirect3D9::CreateDevice
#define VT_CREATE_TEXTURE        23 // IDirect3DDevice9::CreateTexture ... CreateIndexBuffer
#define VT_CREATE_VOLUME_TEXTURE 24
#define VT_CREATE_CUBE_TEXTURE   25
#define VT_CREATE_VERTEX_BUFFER  26
#define VT_CREATE_INDEX_BUFFER   27

typedef IDirect3D9 *(WINAPI *create9_t)(UINT);
typedef HRESULT(WINAPI *create9ex_t)(UINT, IDirect3D9Ex **);
typedef HRESULT(STDMETHODCALLTYPE *create_device_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT(STDMETHODCALLTYPE *create_texture_t)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_volume_t)(IDirect3DDevice9 *, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_cube_t)(IDirect3DDevice9 *, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_vb_t)(IDirect3DDevice9 *, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_ib_t)(IDirect3DDevice9 *, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9 **, HANDLE *);

static create9_t create9_orig;
static create_device_t create_device_orig;
static void *volatile device;
static int use_ex;
static UINT ex_version;

static create_texture_t create_texture_orig;
static create_volume_t create_volume_orig;
static create_cube_t create_cube_orig;
static create_vb_t create_vb_orig;
static create_ib_t create_ib_orig;
static LONG converted[5]; // managed resources turned into default-pool ones: textures, volumes, cubes, vertex buffers, index buffers

void *d3dcap_device(void)
{
	return device;
}

static void convert_logged(int kind, const char *what, unsigned a, unsigned b, unsigned fmt)
{
	LONG n = InterlockedIncrement(&converted[kind]);
	if (n <= 2 || n == 1000 || n == 10000)
		log_printf("d3d9ex: managed %s (%u, %u, format %x) made a default-pool dynamic one (the %ld. of its kind)", what, a, b, fmt, (long)n);
}

static HRESULT STDMETHODCALLTYPE create_texture_hook(IDirect3DDevice9 *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9 **out, HANDLE *shared)
{
	if (pool != D3DPOOL_MANAGED)
		return create_texture_orig(dev, w, h, levels, usage, fmt, pool, out, shared);
	HRESULT hr = create_texture_orig(dev, w, h, levels, usage | D3DUSAGE_DYNAMIC, fmt, D3DPOOL_DEFAULT, out, shared);
	if (FAILED(hr)) { // a compressed format that this driver does not make dynamic: still a texture (without the lock it would need)
		static LONG logged;
		if (InterlockedIncrement(&logged) <= 3)
			log_printf("d3d9ex: texture %ux%u levels %u format %x: dynamic default pool failed (hr %lx), trying without dynamic", w, h, levels, (unsigned)fmt, (unsigned long)hr);
		hr = create_texture_orig(dev, w, h, levels, usage, fmt, D3DPOOL_DEFAULT, out, shared);
	}
	if (SUCCEEDED(hr))
		convert_logged(0, "texture", w, h, (unsigned)fmt);
	return hr;
}

static HRESULT STDMETHODCALLTYPE create_volume_hook(IDirect3DDevice9 *dev, UINT w, UINT h, UINT depth, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DVolumeTexture9 **out, HANDLE *shared)
{
	if (pool == D3DPOOL_MANAGED) {
		usage |= D3DUSAGE_DYNAMIC;
		pool = D3DPOOL_DEFAULT;
		convert_logged(1, "volume texture", w, h, (unsigned)fmt);
	}
	return create_volume_orig(dev, w, h, depth, levels, usage, fmt, pool, out, shared);
}

static HRESULT STDMETHODCALLTYPE create_cube_hook(IDirect3DDevice9 *dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9 **out, HANDLE *shared)
{
	if (pool == D3DPOOL_MANAGED) {
		usage |= D3DUSAGE_DYNAMIC;
		pool = D3DPOOL_DEFAULT;
		convert_logged(2, "cube texture", edge, levels, (unsigned)fmt);
	}
	return create_cube_orig(dev, edge, levels, usage, fmt, pool, out, shared);
}

static HRESULT STDMETHODCALLTYPE create_vb_hook(IDirect3DDevice9 *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **out, HANDLE *shared)
{
	if (pool == D3DPOOL_MANAGED) {
		usage |= D3DUSAGE_DYNAMIC;
		pool = D3DPOOL_DEFAULT;
		convert_logged(3, "vertex buffer", length, 0, (unsigned)fvf);
	}
	return create_vb_orig(dev, length, usage, fvf, pool, out, shared);
}

static HRESULT STDMETHODCALLTYPE create_ib_hook(IDirect3DDevice9 *dev, UINT length, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9 **out, HANDLE *shared)
{
	if (pool == D3DPOOL_MANAGED) {
		usage |= D3DUSAGE_DYNAMIC;
		pool = D3DPOOL_DEFAULT;
		convert_logged(4, "index buffer", length, 0, (unsigned)fmt);
	}
	return create_ib_orig(dev, length, usage, fmt, pool, out, shared);
}

static void patch_vtable(void **vt, int index, void *hook, void **orig)
{
	DWORD old;
	if (!VirtualProtect(&vt[index], sizeof(void *), PAGE_READWRITE, &old))
		return;
	*orig = vt[index];
	vt[index] = hook;
	VirtualProtect(&vt[index], sizeof(void *), old, &old);
}

// The device's vtable is the class's: every device of this process makes its resources through these entries from now on
static void hook_resource_creation(IDirect3DDevice9 *dev)
{
	if (create_texture_orig)
		return;
	void **vt = *(void ***)dev;
	patch_vtable(vt, VT_CREATE_TEXTURE, (void *)create_texture_hook, (void **)&create_texture_orig);
	patch_vtable(vt, VT_CREATE_VOLUME_TEXTURE, (void *)create_volume_hook, (void **)&create_volume_orig);
	patch_vtable(vt, VT_CREATE_CUBE_TEXTURE, (void *)create_cube_hook, (void **)&create_cube_orig);
	patch_vtable(vt, VT_CREATE_VERTEX_BUFFER, (void *)create_vb_hook, (void **)&create_vb_orig);
	patch_vtable(vt, VT_CREATE_INDEX_BUFFER, (void *)create_ib_hook, (void **)&create_ib_orig);
	log_printf("d3d9ex: texture and buffer creation hooked (managed pool -> default pool, dynamic)");
}

// IID_IDirect3D9Ex (the game makes more than one Direct3D object: the device is made through whichever of them it likes, so the object is asked, not compared)
static const GUID iid_d3d9ex = {0x02177241, 0x69fc, 0x400c, {0x8f, 0xf1, 0x93, 0xa4, 0x4d, 0xf6, 0x86, 0x1d}};

static HRESULT STDMETHODCALLTYPE create_device_hook(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *pp,
						     IDirect3DDevice9 **out)
{
	HRESULT hr;
	int ex = 0;
	IDirect3D9Ex *obj = NULL;
	if (settings_borderless() && pp && !pp->Windowed) { // exclusive full screen: a device that can be lost, with the render thread stuck in d3d9.dll and a Reset on the way back when the window loses the focus
		pp->Windowed = TRUE; // (the window the game made for it is a popup of the whole screen already)
		pp->FullScreen_RefreshRateInHz = 0;
		static LONG told;
		if (!InterlockedExchange(&told, 1))
			log_printf("d3dcap: [display] borderless: the full screen device is made a windowed one (%ux%u)", pp->BackBufferWidth, pp->BackBufferHeight);
	}
	if (use_ex && pp && out && SUCCEEDED(IDirect3D9_QueryInterface(d3d, &iid_d3d9ex, (void **)&obj)) && obj) {
		D3DPRESENT_PARAMETERS asked = *pp; // a failed CreateDeviceEx can leave the parameters rewritten: the plain attempt gets what the game asked for
		D3DDISPLAYMODEEX mode, *pmode = NULL;
		if (!pp->Windowed) { // exclusive fullscreen: the mode the game asked for
			memset(&mode, 0, sizeof(mode));
			mode.Size = sizeof(mode);
			mode.Width = pp->BackBufferWidth;
			mode.Height = pp->BackBufferHeight;
			mode.RefreshRate = pp->FullScreen_RefreshRateInHz;
			mode.Format = pp->BackBufferFormat;
			mode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
			pmode = &mode;
		}
		IDirect3DDevice9Ex *dx = NULL;
		hr = IDirect3D9Ex_CreateDeviceEx(obj, adapter, type, window, flags, pp, pmode, &dx);
		IDirect3D9Ex_Release(obj);
		if (SUCCEEDED(hr) && dx) {
			*out = (IDirect3DDevice9 *)dx;
			ex = 1;
		} else {
			log_printf("d3d9ex: CreateDeviceEx failed (hr %lx, %s, %ux%u at %u Hz format %d), the plain Direct3D 9 is used instead", (unsigned long)hr, pp->Windowed ? "windowed" : "fullscreen",
				   pp->BackBufferWidth, pp->BackBufferHeight, pp->FullScreen_RefreshRateInHz, (int)pp->BackBufferFormat);
			IDirect3D9 *plain = create9_orig ? create9_orig(ex_version) : NULL;
			*pp = asked;
			hr = plain ? IDirect3D9_CreateDevice(plain, adapter, type, window, flags, pp, out) : D3DERR_NOTAVAILABLE;
			if (plain)
				IDirect3D9_Release(plain); // a device keeps the object alive
		}
	} else {
		hr = create_device_orig(d3d, adapter, type, window, flags, pp, out);
	}
	if (SUCCEEDED(hr) && out && *out) {
		device = *out;
		log_printf("d3dcap: CreateDevice(adapter %u, type %d, window %p, flags %lx) -> device %p, vtable %p%s", adapter, (int)type, (void *)window,
			   (unsigned long)flags, (void *)*out, *(void **)*out, ex ? " (Direct3D 9Ex)" : "");
		if (ex)
			hook_resource_creation(*out);
	} else {
		log_printf("d3dcap: CreateDevice failed, hr %lx", (unsigned long)hr);
	}
	return hr;
}

static IDirect3D9 *WINAPI create9_hook(UINT version)
{
	IDirect3D9 *d3d = NULL;
	if (use_ex) {
		HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
		create9ex_t make = d3d9 ? (create9ex_t)(void *)GetProcAddress(d3d9, "Direct3DCreate9Ex") : NULL;
		IDirect3D9Ex *ex = NULL;
		if (make && SUCCEEDED(make(version, &ex)) && ex) {
			d3d = (IDirect3D9 *)ex;
			ex_version = version;
			log_printf("d3d9ex: Direct3DCreate9Ex(%u) -> %p", version, (void *)ex);
		} else {
			log_printf("d3d9ex: Direct3DCreate9Ex is not available, the plain Direct3D 9 is used");
		}
	}
	if (!d3d)
		d3d = create9_orig(version);
	if (d3d && !create_device_orig) {
		void **vt = *(void ***)d3d;
		DWORD old;
		if (VirtualProtect(&vt[VT_CREATE_DEVICE], sizeof(void *), PAGE_READWRITE, &old)) {
			create_device_orig = (create_device_t)vt[VT_CREATE_DEVICE];
			vt[VT_CREATE_DEVICE] = (void *)create_device_hook;
			VirtualProtect(&vt[VT_CREATE_DEVICE], sizeof(void *), old, &old);
			log_printf("d3dcap: Direct3DCreate9(%u) -> %p, CreateDevice hooked", version, (void *)d3d);
		}
	}
	return d3d;
}

#ifdef _WIN64
// The import table entry of the exe that points at d3d9.dll's function: found by name in the exe's import directory
static void **find_import_slot(HMODULE exe, const char *dll, const char *func)
{
	uint8_t *base = (uint8_t *)exe;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return NULL;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir->VirtualAddress)
		return NULL;
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++) {
		if (lstrcmpiA((const char *)(base + d->Name), dll))
			continue;
		IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
		IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
		for (; names->u1.AddressOfData; names++, thunk++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
			if (!lstrcmpA((const char *)by_name->Name, func))
				return (void **)&thunk->u1.Function;
		}
	}
	return NULL;
}
#else
static void hook_create9(HMODULE d3d9);

// A client that maps the game itself (iw4x-sp) loads d3d9.dll after our DllMain: watch for it, a millisecond at a time, until the game has its device
static DWORD WINAPI wait_for_d3d9(LPVOID unused)
{
	(void)unused;
	for (int i = 0; i < 120000 && !create9_orig; i++) {
		HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
		if (d3d9) {
			hook_create9(d3d9);
			return 0;
		}
		Sleep(1);
	}
	log_printf("d3dcap: d3d9.dll never loaded");
	return 0;
}

static void hook_create9(HMODULE d3d9)
{
	uint8_t *fn = (uint8_t *)GetProcAddress(d3d9, "Direct3DCreate9");
	if (!fn) {
		log_printf("d3dcap: no Direct3DCreate9 in d3d9.dll");
		return;
	}
	static const uint8_t prolog[5] = {0x8b, 0xff, 0x55, 0x8b, 0xec}; // mov edi,edi; push ebp; mov ebp,esp
	if (!hook_function(fn, prolog, sizeof(prolog), (void *)create9_hook, (void **)&create9_orig))
		log_printf("d3dcap: Direct3DCreate9 starts with %02x %02x %02x %02x %02x, not the expected prologue", fn[0], fn[1], fn[2], fn[3], fn[4]);
	else
		log_printf("d3dcap: Direct3DCreate9 hooked at %p", (void *)fn);
}
#endif

void d3dcap_install(void)
{
#ifdef MW2COOP_IW4X
	use_ex = 0; // the iw4x-sp client has its own r_useD3D9Ex
#else
	use_ex = settings_d3d9ex();
#endif
#ifdef _WIN64
	// the 64-bit build keeps its device in a global (native.c, shot.c): this file only has to make the extended one
	if (!use_ex && !settings_borderless())
		return;
	void **slot = find_import_slot(GetModuleHandleA(NULL), "d3d9.dll", "Direct3DCreate9");
	DWORD old;
	if (!slot) {
		log_printf("d3d9ex: the exe has no import of Direct3DCreate9 from d3d9.dll: the plain Direct3D 9 stays");
		use_ex = 0;
		return;
	}
	if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
		log_printf("d3d9ex: the import table entry cannot be written: the plain Direct3D 9 stays");
		use_ex = 0;
		return;
	}
	create9_orig = (create9_t)*slot;
	*slot = (void *)create9_hook;
	VirtualProtect(slot, sizeof(void *), old, &old);
	log_printf("d3d9ex: the import of Direct3DCreate9 (entry %p) points at the extended Direct3D 9", (void *)slot);
#else
	HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
	if (!d3d9) {
		log_printf("d3dcap: d3d9.dll is not loaded yet, waiting for it");
		CloseHandle(CreateThread(NULL, 0, wait_for_d3d9, NULL, 0, NULL));
		return;
	}
	hook_create9(d3d9);
#endif
}
