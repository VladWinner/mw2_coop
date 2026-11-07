// Does Direct3DCreate9On12 present on this PC? A window, a device on Direct3D 12, green clears, Present, then the screen's pixel in the window's middle.
// Usage: on12test.exe <0|1: 1 = Direct3DCreate9On12, 0 = the plain Direct3DCreate9> [multisample type, 2 = the game's]
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <dxgi.h>
#include <d3d12.h>
#include <stdio.h>

typedef struct { BOOL Enable9On12; IUnknown *pD3D12Device; IUnknown *pD3D12Queues[2]; UINT NumQueues; UINT NodeMask; } args_t;
typedef IDirect3D9 *(WINAPI *make12_t)(UINT, args_t *, UINT);

IDirect3DDevice9 *g_dev;
static DWORD WINAPI render_thread(LPVOID u)
{
	(void)u;
	for (int i = 0; i < 180; i++) {
		IDirect3DDevice9_Clear(g_dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 200, 0), 1.0f, 0);
		IDirect3DDevice9_BeginScene(g_dev);
		IDirect3DDevice9_EndScene(g_dev);
		HRESULT ph = IDirect3DDevice9_Present(g_dev, NULL, NULL, NULL, NULL);
		if (i == 0 || i == 179)
			printf("thread frame %d present hr %lx\n", i, (unsigned long)ph);
		Sleep(16);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int on12 = argc > 1 ? atoi(argv[1]) : 1;
	WNDCLASSA wc = {0};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "on12test";
	RegisterClassA(&wc);
	int hidden = getenv("ON12_HIDDEN") != NULL; // the game makes its window hidden, makes the device, then shows the window
	int big = getenv("ON12_BIG") != NULL;       // the game's window: style 0x04c80000, 1936x1100 at 0,0
	int gstyle = big || getenv("ON12_STYLE"), gsize = big || getenv("ON12_SIZE");
	int wx = getenv("ON12_X0") ? 0 : (gsize ? 0 : 100), ww = getenv("ON12_W") ? atoi(getenv("ON12_W")) : (gsize ? 1936 : 800), wh = getenv("ON12_H") ? atoi(getenv("ON12_H")) : (gsize ? 1100 : 600);
	DWORD style = gstyle ? 0x04C80000 : WS_OVERLAPPEDWINDOW;
	if (getenv("ON12_POPUP")) // a borderless window of the whole screen
		style = WS_POPUP;
	if (!hidden)
		style |= WS_VISIBLE;
	HWND w = CreateWindowA("on12test", "on12test", style, wx, wx, ww, wh, NULL, NULL, wc.hInstance, NULL);
	if (getenv("ON12_OVERLAY")) { // a graphics overlay (the Steam one) hooks Present: does it black the window?
		HMODULE o = LoadLibraryA(getenv("ON12_OVERLAY"));
		printf("overlay %s -> %p\n", getenv("ON12_OVERLAY"), (void *)o);
	}
	HMODULE d3d9 = LoadLibraryA("d3d9.dll");
	IDirect3D9 *d3d = NULL;
	if (on12) {
		make12_t make = (make12_t)(void *)GetProcAddress(d3d9, "Direct3DCreate9On12");
		args_t a = {0};
		a.Enable9On12 = TRUE;
		if (getenv("ON12_ADAPTER")) { // our own Direct3D 12 device on a chosen adapter (0, 1, ...), handed to Direct3D 9
			int want = atoi(getenv("ON12_ADAPTER"));
			typedef HRESULT(WINAPI *mkfac_t)(REFIID, void **);
			typedef HRESULT(WINAPI *mkdev_t)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
			mkfac_t mkfac = (mkfac_t)(void *)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory1");
			mkdev_t mkdev = (mkdev_t)(void *)GetProcAddress(LoadLibraryA("d3d12.dll"), "D3D12CreateDevice");
			IDXGIFactory1 *fac = NULL;
			if (mkfac && SUCCEEDED(mkfac(&IID_IDXGIFactory1, (void **)&fac))) {
				IDXGIAdapter1 *ad = NULL;
				for (UINT i = 0; SUCCEEDED(IDXGIFactory1_EnumAdapters1(fac, i, &ad)); i++) {
					DXGI_ADAPTER_DESC1 dd;
					IDXGIAdapter1_GetDesc1(ad, &dd);
					printf("adapter %u: %ls%s\n", i, dd.Description, i == (UINT)want ? " <- used" : "");
					if (i == (UINT)want) {
						ID3D12Device *dev12 = NULL;
						HRESULT h12 = mkdev((IUnknown *)ad, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev12);
						printf("D3D12CreateDevice hr %lx dev %p\n", (unsigned long)h12, (void *)dev12);
						a.pD3D12Device = (IUnknown *)dev12;
					}
					IDXGIAdapter1_Release(ad);
				}
			}
		}
		d3d = make ? make(D3D_SDK_VERSION, &a, 1) : NULL;
	} else {
		d3d = Direct3DCreate9(D3D_SDK_VERSION);
	}
	IDirect3D9Ex *d3dex = NULL;
	if (on12 && getenv("ON12_EX")) {
		typedef HRESULT(WINAPI * make12ex_t)(UINT, args_t *, UINT, IDirect3D9Ex **);
		make12ex_t mk = (make12ex_t)(void *)GetProcAddress(d3d9, "Direct3DCreate9On12Ex");
		args_t a2 = {0};
		a2.Enable9On12 = TRUE;
		HRESULT eh = mk ? mk(D3D_SDK_VERSION, &a2, 1, &d3dex) : E_FAIL;
		printf("Direct3DCreate9On12Ex hr %lx\n", (unsigned long)eh);
		if (d3dex)
			d3d = (IDirect3D9 *)d3dex;
	}
	printf("d3d %p (on12 %d)\n", (void *)d3d, on12);
	if (!d3d)
		return 1;
	D3DPRESENT_PARAMETERS pp = {0};
	pp.Windowed = getenv("ON12_FULL") ? FALSE : TRUE;
	if (getenv("ON12_FULL")) {
		pp.BackBufferWidth = 1920;
		pp.BackBufferHeight = 1080;
		pp.FullScreen_RefreshRateInHz = 60;
	}
	pp.SwapEffect = getenv("ON12_SWAP") ? (D3DSWAPEFFECT)atoi(getenv("ON12_SWAP")) : D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_UNKNOWN;
	pp.hDeviceWindow = w;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferCount = getenv("ON12_COUNT") ? atoi(getenv("ON12_COUNT")) : 1;
	pp.PresentationInterval = getenv("ON12_IMMEDIATE") ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE;
	if (getenv("ON12_BB")) { // a back buffer of another size than the window's client area (the game: 1920x1080 in a window with a title bar)
		pp.BackBufferWidth = 1920;
		pp.BackBufferHeight = 1080;
	}
	pp.MultiSampleType = (D3DMULTISAMPLE_TYPE)(argc > 2 ? atoi(argv[2]) : 0);
	SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	IDirect3DDevice9 *dev = NULL;
	HRESULT hr;
	if (d3dex) {
		IDirect3DDevice9Ex *dx = NULL;
		hr = IDirect3D9Ex_CreateDeviceEx(d3dex, 0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &pp, NULL, &dx);
		dev = (IDirect3DDevice9 *)dx;
	} else {
		hr = IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &pp, &dev);
	}
	if (hidden)
		ShowWindow(w, SW_SHOW);
	printf("CreateDevice hr %lx dev %p\n", (unsigned long)hr, (void *)dev);
	if (FAILED(hr))
		return 1;
	int threaded = argc > 3 && atoi(argv[3]);
	if (threaded) { // Present from another thread than the one that made the device and the window, as the game's render thread does
		extern IDirect3DDevice9 *g_dev;
		g_dev = dev;
		HANDLE t = CreateThread(NULL, 0, render_thread, NULL, 0, NULL);
		for (int i = 0; i < 200; i++) {
			MSG m;
			while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
			Sleep(16);
			if (i == 120) {
				RECT r;
				GetWindowRect(w, &r);
				HDC dc = GetDC(NULL);
				HDC cap = CreateCompatibleDC(dc);
				HBITMAP cb = CreateCompatibleBitmap(dc, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
				SelectObject(cap, cb);
				BitBlt(cap, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), dc, 0, 0, SRCCOPY | CAPTUREBLT);
				COLORREF c = GetPixel(cap, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
				DeleteObject(cb);
				DeleteDC(cap);
				printf("screen pixel at the window's middle: R%d G%d B%d\n", GetRValue(c), GetGValue(c), GetBValue(c));
				{
					RECT cr;
					GetClientRect(w, &cr);
					HDC wd = GetDC(NULL), md = CreateCompatibleDC(wd);
					HBITMAP bm = CreateCompatibleBitmap(wd, 4000, 2000);
					SelectObject(md, bm);
					PrintWindow(w, md, 2);
					COLORREF pc = GetPixel(md, (r.right - r.left) / 2, (r.bottom - r.top) / 2);
					printf("printwindow pixel: R%d G%d B%d\n", GetRValue(pc), GetGValue(pc), GetBValue(pc));
					{
						int ww2 = r.right - r.left, hh2 = r.bottom - r.top, k;
						for (k = 0; k < 6; k++) {
							int px = (k % 3 + 1) * ww2 / 4, py = (k / 3 + 1) * hh2 / 3;
							COLORREF q = GetPixel(md, px, py);
							printf("  point %d,%d: %d %d %d\n", px, py, GetRValue(q), GetGValue(q), GetBValue(q));
						}
					}
					DeleteObject(bm);
					DeleteDC(md);
					ReleaseDC(NULL, wd);
				}
				ReleaseDC(NULL, dc);
			}
		}
		(void)t;
		return 0;
	}
	for (int i = 0; i < 180; i++) {
		MSG m;
		while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
		hr = IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 200, 0), 1.0f, 0);
		IDirect3DDevice9_BeginScene(dev);
		IDirect3DDevice9_EndScene(dev);
		HRESULT ph = IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
		if (i == 0 || i == 179)
			printf("frame %d clear hr %lx present hr %lx\n", i, (unsigned long)hr, (unsigned long)ph);
		Sleep(16);
		if (i == 120) {
			RECT r;
			GetWindowRect(w, &r);
			HDC dc = GetDC(NULL);
			COLORREF c = GetPixel(dc, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
			printf("screen pixel at the window's middle: R%d G%d B%d\n", GetRValue(c), GetGValue(c), GetBValue(c));
			ReleaseDC(NULL, dc);
		}
	}
	return 0;
}
