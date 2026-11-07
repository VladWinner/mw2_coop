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
#include <tlhelp32.h>

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
typedef FARPROC(WINAPI *get_proc_address_t)(HMODULE, LPCSTR);
typedef HRESULT(STDMETHODCALLTYPE *create_device_ex_t)(IDirect3D9Ex *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *, IDirect3DDevice9Ex **);
typedef HRESULT(STDMETHODCALLTYPE *create_device_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT(STDMETHODCALLTYPE *create_texture_t)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_volume_t)(IDirect3DDevice9 *, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_cube_t)(IDirect3DDevice9 *, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_vb_t)(IDirect3DDevice9 *, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9 **, HANDLE *);
typedef HRESULT(STDMETHODCALLTYPE *create_ib_t)(IDirect3DDevice9 *, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9 **, HANDLE *);

static create9_t create9_orig;
static create9ex_t create9ex_orig;
static get_proc_address_t getproc_orig;
static create_device_t create_device_orig;
static create_device_ex_t create_device_ex_orig;
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
	return InterlockedCompareExchangePointer((PVOID volatile *)&device, NULL, NULL);
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

static IDirect3D9 *WINAPI create9_hook(UINT version);
static HRESULT WINAPI create9ex_hook(UINT version, IDirect3D9Ex **out);
static FARPROC create9_hook_proc(void)
{
	create9_t typed = create9_hook;
	FARPROC generic = NULL;
	_Static_assert(sizeof(generic) == sizeof(typed), "Win32 function pointer sizes differ");
	memcpy(&generic, &typed, sizeof(generic));
	return generic;
}
static FARPROC create9ex_hook_proc(void)
{
	create9ex_t typed = create9ex_hook;
	FARPROC generic = NULL;
	_Static_assert(sizeof(generic) == sizeof(typed), "Win32 function pointer sizes differ");
	memcpy(&generic, &typed, sizeof(generic));
	return generic;
}

// IID_IDirect3D9Ex (the game makes more than one Direct3D object: the device is made through whichever of them it likes, so the object is asked, not compared)
static const GUID iid_d3d9ex = {0x02177241, 0x69fc, 0x400c, {0x8f, 0xf1, 0x93, 0xa4, 0x4d, 0xf6, 0x86, 0x1d}};

static create9_t plain_create9_factory(void)
{
	create9_t make = create9_orig;
	if (!make) {
		HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
		FARPROC proc = d3d9 ? (getproc_orig ? getproc_orig(d3d9, "Direct3DCreate9") : GetProcAddress(d3d9, "Direct3DCreate9")) : NULL;
		if (proc)
			memcpy(&make, &proc, sizeof(make));
		if (make)
			(void)InterlockedCompareExchangePointer((PVOID volatile *)&create9_orig, (PVOID)make, NULL);
	}
	return create9_orig ? create9_orig : make;
}

static HRESULT STDMETHODCALLTYPE create_device_hook(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *pp,
						     IDirect3DDevice9 **out)
{
	HRESULT hr;
	int ex = 0;
	IDirect3D9Ex *obj = NULL;
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
			create9_t make_plain = plain_create9_factory();
			IDirect3D9 *plain = make_plain ? make_plain(ex_version) : NULL;
			*pp = asked;
			hr = plain ? IDirect3D9_CreateDevice(plain, adapter, type, window, flags, pp, out) : D3DERR_NOTAVAILABLE;
			if (plain)
				IDirect3D9_Release(plain); // a device keeps the object alive
		}
	} else {
		hr = create_device_orig(d3d, adapter, type, window, flags, pp, out);
	}
	if (SUCCEEDED(hr) && out && *out) {
		InterlockedExchangePointer((PVOID volatile *)&device, *out);
		log_printf("d3dcap: CreateDevice(adapter %u, type %d, window %p, flags %lx) -> device %p, vtable %p%s", adapter, (int)type, (void *)window,
			   (unsigned long)flags, (void *)*out, *(void **)*out, ex ? " (Direct3D 9Ex)" : "");
		if (ex)
			hook_resource_creation(*out);
	} else {
		log_printf("d3dcap: CreateDevice failed, hr %lx", (unsigned long)hr);
	}
	return hr;
}

static void patch_factory_slot(void **vt, int index, void *hook, void **original)
{
	DWORD old, ignored;
	if (!vt || !hook || !original || !VirtualProtect(&vt[index], sizeof(void *), PAGE_READWRITE, &old))
		return;
	void *current = InterlockedCompareExchangePointer((PVOID volatile *)&vt[index], NULL, NULL);
	if (current != hook) {
		if (!*original)
			*original = current;
		InterlockedExchangePointer((PVOID volatile *)&vt[index], hook);
	}
	VirtualProtect(&vt[index], sizeof(void *), old, &ignored);
}

static HRESULT STDMETHODCALLTYPE create_device_ex_hook(IDirect3D9Ex *d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
							D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *mode, IDirect3DDevice9Ex **out)
{
	HRESULT hr = create_device_ex_orig ? create_device_ex_orig(d3d, adapter, type, window, flags, pp, mode, out) : D3DERR_INVALIDCALL;
	if (SUCCEEDED(hr) && out && *out) {
		InterlockedExchangePointer((PVOID volatile *)&device, *out);
		if (use_ex)
			hook_resource_creation((IDirect3DDevice9 *)*out);
		log_printf("d3dcap: CreateDeviceEx(adapter %u, type %d, window %p, flags %lx) -> device %p, vtable %p",
			   adapter, (int)type, (void *)window, (unsigned long)flags, (void *)*out, *(void **)*out);
	} else {
		log_printf("d3dcap: CreateDeviceEx failed, hr %lx", (unsigned long)hr);
	}
	return hr;
}

static void hook_factory(IDirect3D9 *d3d)
{
	if (!d3d || !*(void ***)d3d)
		return;
	void **vt = *(void ***)d3d;
	patch_factory_slot(vt, VT_CREATE_DEVICE, (void *)create_device_hook, (void **)&create_device_orig);
	IDirect3D9Ex *ex = NULL;
	if (SUCCEEDED(IDirect3D9_QueryInterface(d3d, &iid_d3d9ex, (void **)&ex)) && ex) {
		void **exvt = *(void ***)ex;
		patch_factory_slot(exvt, 20, (void *)create_device_ex_hook, (void **)&create_device_ex_orig); // IDirect3D9Ex::CreateDeviceEx
		IDirect3D9Ex_Release(ex);
	}
}

static IDirect3D9 *WINAPI create9_hook(UINT version)
{
	IDirect3D9 *d3d = NULL;
	if (use_ex) {
		HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
		create9ex_t make = create9ex_orig;
		if (!make && d3d9) {
			FARPROC proc = getproc_orig ? getproc_orig(d3d9, "Direct3DCreate9Ex") : GetProcAddress(d3d9, "Direct3DCreate9Ex");
			if (proc)
				memcpy(&make, &proc, sizeof(make));
			if (make)
				InterlockedCompareExchangePointer((PVOID volatile *)&create9ex_orig, (PVOID)make, NULL);
		}
		IDirect3D9Ex *ex = NULL;
		if (make && SUCCEEDED(make(version, &ex)) && ex) {
			d3d = (IDirect3D9 *)ex;
			ex_version = version;
			log_printf("d3d9ex: Direct3DCreate9Ex(%u) -> %p", version, (void *)ex);
		} else {
			log_printf("d3d9ex: Direct3DCreate9Ex is not available, the plain Direct3D 9 is used");
		}
	}
	if (!d3d && create9_orig)
		d3d = create9_orig(version);
	if (d3d) {
		hook_factory(d3d);
		log_printf("d3dcap: Direct3DCreate9(%u) -> %p, CreateDevice%s hooked", version, (void *)d3d,
			   create_device_ex_orig ? "/CreateDeviceEx" : "");
	}
	return d3d;
}

static HRESULT WINAPI create9ex_hook(UINT version, IDirect3D9Ex **out)
{
	HRESULT hr = create9ex_orig ? create9ex_orig(version, out) : D3DERR_NOTAVAILABLE;
	if (SUCCEEDED(hr) && out && *out) {
		hook_factory((IDirect3D9 *)*out);
		log_printf("d3dcap: Direct3DCreate9Ex(%u) -> %p, CreateDeviceEx hooked", version, (void *)*out);
	}
	return hr;
}

static FARPROC WINAPI getproc_hook(HMODULE module, LPCSTR name)
{
	FARPROC result = getproc_orig ? getproc_orig(module, name) : NULL;
	if (!result || !name || (uintptr_t)name <= 0xffff || module != GetModuleHandleA("d3d9.dll"))
		return result;
	if (!lstrcmpA(name, "Direct3DCreate9")) {
		FARPROC wrapper = create9_hook_proc();
		if (result != wrapper) {
			(void)InterlockedCompareExchangePointer((PVOID volatile *)&create9_orig, (PVOID)result, NULL);
			return wrapper;
		}
	} else if (!lstrcmpA(name, "Direct3DCreate9Ex")) {
		FARPROC wrapper = create9ex_hook_proc();
		if (result != wrapper) {
			(void)InterlockedCompareExchangePointer((PVOID volatile *)&create9ex_orig, (PVOID)result, NULL);
			return wrapper;
		}
	}
	return result;
}

#ifdef _WIN64
// Find a named import in the host PE without relying on the executable's layout.
static void **find_import_slot(HMODULE image, const char *dll, const char *func)
{
	if (!image || !dll || !func)
		return NULL;
	uint8_t *base = (uint8_t *)image;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return NULL;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return NULL;
	IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir->VirtualAddress || !dir->Size)
		return NULL;
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++) {
		if (lstrcmpiA((const char *)(base + d->Name), dll) || !d->OriginalFirstThunk)
			continue;
		IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
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
#endif

static int d3dcap_prefix_equal_i(const char *value, const char *prefix)
{
	if (!value || !prefix)
		return 0;
	while (*prefix) {
		unsigned char a = (unsigned char)*value++, b = (unsigned char)*prefix++;
		if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
		if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
		if (a != b)
			return 0;
	}
	return 1;
}

static int patch_import_slot(void **slot, void *replacement, void **original)
{
	DWORD old, ignored;
	if (!slot || !replacement || !original || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
		return 0;
	void *current = InterlockedCompareExchangePointer((PVOID volatile *)slot, NULL, NULL);
	int patched = 0;
	if (current && current != replacement) {
		(void)InterlockedCompareExchangePointer((PVOID volatile *)original, current, NULL);
		InterlockedExchangePointer((PVOID volatile *)slot, replacement);
		patched = 1;
	}
	VirtualProtect(slot, sizeof(void *), old, &ignored);
	return patched;
}

static int patch_module_imports(HMODULE image)
{
	if (!image)
		return 0;
	uint8_t *base = (uint8_t *)image;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return 0;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return 0;
	IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir->VirtualAddress || !dir->Size)
		return 0;
	int patched = 0;
	for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
		const char *dll = (const char *)(base + imp->Name);
		if (!lstrcmpiA(dll, "d3d9.dll")) {
			if (!imp->OriginalFirstThunk)
				continue; // no import names: do not infer an ordinal from its current pointer
			IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
			IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
			for (; iat->u1.Function && names->u1.AddressOfData; iat++, names++) {
				if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
					continue;
				IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
				const char *name = (const char *)by_name->Name;
				if (!lstrcmpA(name, "Direct3DCreate9"))
					patched += patch_import_slot((void **)&iat->u1.Function, (void *)create9_hook_proc(), (void **)&create9_orig);
				else if (!lstrcmpA(name, "Direct3DCreate9Ex"))
					patched += patch_import_slot((void **)&iat->u1.Function, (void *)create9ex_hook_proc(), (void **)&create9ex_orig);
			}
		} else if (!lstrcmpiA(dll, "kernel32.dll") || !lstrcmpiA(dll, "kernelbase.dll") ||
			   d3dcap_prefix_equal_i(dll, "api-ms-win-core-libraryloader-")) {
			if (!imp->OriginalFirstThunk)
				continue;
			IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
			IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
			for (; iat->u1.Function && names->u1.AddressOfData; iat++, names++) {
				if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
					continue;
				IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
				if (!lstrcmpA((const char *)by_name->Name, "GetProcAddress"))
					patched += patch_import_slot((void **)&iat->u1.Function, (void *)getproc_hook, (void **)&getproc_orig);
			}
		}
	}
	return patched;
}

static int patch_loaded_module_imports(void)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if (snap == INVALID_HANDLE_VALUE)
		return 0;
	MODULEENTRY32 me;
	memset(&me, 0, sizeof(me));
	me.dwSize = sizeof(me);
	int patched = 0;
	if (Module32First(snap, &me)) {
		do {
			patched += patch_module_imports(me.hModule);
		} while (Module32Next(snap, &me));
	}
	CloseHandle(snap);
	return patched;
}

#ifndef _WIN64
static void hook_create9(HMODULE d3d9);
#endif

static DWORD WINAPI wait_for_d3d9_capture(LPVOID unused)
{
	(void)unused;
	for (int i = 0; i < 1200 && !d3dcap_device(); i++) {
		(void)patch_loaded_module_imports();
#ifndef _WIN64
		HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
		if (d3d9 && !create9_orig)
			hook_create9(d3d9); // preserve the known x86 export-detour fallback for late-loaded IW4x
#endif
		Sleep(100);
	}
	if (!d3dcap_device())
		log_printf("d3dcap: timed out waiting for Direct3DCreate9/Ex device capture");
	return 0;
}

#ifndef _WIN64
static void hook_create9(HMODULE d3d9)
{
	static LONG attempted;
	if (InterlockedCompareExchange(&attempted, 1, 0) != 0)
		return;
	FARPROC proc = getproc_orig ? getproc_orig(d3d9, "Direct3DCreate9") : GetProcAddress(d3d9, "Direct3DCreate9");
	uint8_t *fn = (uint8_t *)proc;
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
	use_ex = 0; // iw4x-sp keeps control of r_useD3D9Ex; we observe its factory without changing that setting
#else
	use_ex = settings_d3d9ex();
#endif
	HMODULE host = GetModuleHandleA(NULL);
	int host_patched = patch_module_imports(host);
	log_printf("d3dcap: host imports captured for cinematics: %d", host_patched);
#ifdef _WIN64
	// The original x64 path only installed this hook when d3d9ex was enabled. Cinematics
	// also need the device in ordinary D3D9 mode, so the same wrapper now captures both.
	void **slot = find_import_slot(host, "d3d9.dll", "Direct3DCreate9");
	if (slot && !create9_orig) {
		DWORD old;
		if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
			create9_orig = (create9_t)*slot;
			*slot = (void *)create9_hook_proc();
			DWORD ignored;
			VirtualProtect(slot, sizeof(void *), old, &ignored);
		}
	}
#else
	HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
	if (d3d9 && !create9_orig)
		hook_create9(d3d9);
#endif
	HANDLE thread = CreateThread(NULL, 0, wait_for_d3d9_capture, NULL, 0, NULL);
	if (thread)
		CloseHandle(thread);
	else
		log_printf("d3dcap: could not start the device-capture worker");
}
