// Does AMD's FidelityFX C API run from this toolchain on this PC? Loads amd_fidelityfx_loader_dx12.dll, makes a Direct3D 12 device on the chosen adapter and creates an FSR upscaler
// context (render size -> output size), then destroys it. QA tool for the upscaler work (docs/upscaling-plan.md), not part of the mod.
// Build: tools/ffxprobe/build.sh. Usage: ffxprobe.exe <folder with amd_fidelityfx_loader_dx12.dll> [adapter index, default 0] [render WxH] [output WxH]
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>

#include "ffx_api.h"
#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"

static void message(uint32_t type, const wchar_t *text)
{
	printf("  ffx %s: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: ffxprobe <folder with amd_fidelityfx_loader_dx12.dll> [adapter] [WxH render] [WxH output]\n");
		return 2;
	}
	int adapter = argc > 2 ? atoi(argv[2]) : 0;
	int rw = 1280, rh = 720, ow = 1920, oh = 1080;
	if (argc > 3)
		sscanf(argv[3], "%dx%d", &rw, &rh);
	if (argc > 4)
		sscanf(argv[4], "%dx%d", &ow, &oh);

	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s\\amd_fidelityfx_loader_dx12.dll", argv[1]);
	SetDllDirectoryA(argv[1]);
	HMODULE ffx = LoadLibraryA(path);
	printf("loader dll %p (error %lu)\n", (void *)ffx, ffx ? 0UL : GetLastError());
	if (!ffx)
		return 1;
	PfnFfxCreateContext create = (PfnFfxCreateContext)(void *)GetProcAddress(ffx, "ffxCreateContext");
	PfnFfxDestroyContext destroy = (PfnFfxDestroyContext)(void *)GetProcAddress(ffx, "ffxDestroyContext");
	PfnFfxQuery query = (PfnFfxQuery)(void *)GetProcAddress(ffx, "ffxQuery");
	printf("ffxCreateContext %p, ffxDestroyContext %p, ffxQuery %p\n", (void *)create, (void *)destroy, (void *)query);
	if (!create || !destroy)
		return 1;

	typedef HRESULT(WINAPI * mkfac_t)(const GUID *, void **);
	typedef HRESULT(WINAPI * mkdev_t)(IUnknown *, int, const GUID *, void **);
	static const GUID iid_factory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
	static const GUID iid_device = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
	mkfac_t mkfac = (mkfac_t)(void *)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory1");
	mkdev_t mkdev = (mkdev_t)(void *)GetProcAddress(LoadLibraryA("d3d12.dll"), "D3D12CreateDevice");
	IDXGIFactory1 *fac = NULL;
	if (!mkfac || FAILED(mkfac(&iid_factory1, (void **)&fac)))
		return 1;
	IDXGIAdapter1 *ad = NULL;
	if (FAILED(IDXGIFactory1_EnumAdapters1(fac, (UINT)adapter, &ad)))
		return 1;
	DXGI_ADAPTER_DESC1 desc;
	IDXGIAdapter1_GetDesc1(ad, &desc);
	printf("adapter %d: %ls\n", adapter, desc.Description);
	ID3D12Device *dev = NULL;
	HRESULT hr = mkdev((IUnknown *)ad, 0xb000, &iid_device, (void **)&dev);
	printf("D3D12CreateDevice hr %lx device %p\n", (unsigned long)hr, (void *)dev);
	if (FAILED(hr))
		return 1;

	struct ffxCreateBackendDX12Desc backend;
	memset(&backend, 0, sizeof(backend));
	backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
	backend.device = dev;
	struct ffxCreateContextDescUpscale up;
	memset(&up, 0, sizeof(up));
	up.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	up.header.pNext = &backend.header;
	up.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE | FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
	up.maxRenderSize.width = (uint32_t)rw;
	up.maxRenderSize.height = (uint32_t)rh;
	up.maxUpscaleSize.width = (uint32_t)ow;
	up.maxUpscaleSize.height = (uint32_t)oh;
	up.fpMessage = message;
	ffxContext ctx = NULL;
	ffxReturnCode_t rc = create(&ctx, &up.header, NULL);
	printf("ffxCreateContext(upscale %dx%d -> %dx%d) -> %u (0 = OK), context %p\n", rw, rh, ow, oh, (unsigned)rc, ctx);
	if (rc == FFX_API_RETURN_OK && ctx) {
		rc = destroy(&ctx, NULL);
		printf("ffxDestroyContext -> %u\n", (unsigned)rc);
	}
	return rc == FFX_API_RETURN_OK ? 0 : 1;
}
