// Can NVIDIA's NGX (DLSS) be driven from this toolchain on this PC? Makes a Direct3D 12 device on the chosen adapter, starts NGX with it (nvngx_dlss.dll from the SDK folder given) and asks
// whether DLSS super sampling is available. QA tool for the upscaler work (docs/upscaling-plan.md), not part of the mod.
// Build: tools/dlssprobe/build.sh. Usage: dlssprobe.exe <folder with nvngx_dlss.dll> [adapter index, default 0]
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: dlssprobe <folder with nvngx_dlss.dll> [adapter]\n");
		return 2;
	}
	int adapter = argc > 2 ? atoi(argv[2]) : 0;
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
	if (FAILED(fac->EnumAdapters1((UINT)adapter, &ad)))
		return 1;
	DXGI_ADAPTER_DESC1 desc;
	ad->GetDesc1(&desc);
	printf("adapter %d: %ls\n", adapter, desc.Description);
	ID3D12Device *dev = NULL;
	HRESULT hr = mkdev((IUnknown *)ad, 0xb000, &iid_device, (void **)&dev);
	printf("D3D12CreateDevice hr %lx\n", (unsigned long)hr);
	if (FAILED(hr))
		return 1;

	static wchar_t dll_dir[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dll_dir, MAX_PATH);
	const wchar_t *paths[1] = {dll_dir};
	NVSDK_NGX_FeatureCommonInfo info = {};
	info.PathListInfo.Path = paths;
	info.PathListInfo.Length = 1;
	wchar_t data[MAX_PATH];
	GetTempPathW(MAX_PATH, data);
	NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init_with_ProjectID("a0b1c2d3-1111-4222-8333-444455556666", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", data, dev, &info);
	printf("NVSDK_NGX_D3D12_Init_with_ProjectID -> 0x%x (%ls)\n", (unsigned)r, GetNGXResultAsString(r));
	if (NVSDK_NGX_FAILED(r))
		return 1;
	NVSDK_NGX_Parameter *params = NULL;
	r = NVSDK_NGX_D3D12_GetCapabilityParameters(&params);
	printf("GetCapabilityParameters -> 0x%x\n", (unsigned)r);
	if (NVSDK_NGX_SUCCEED(r) && params) {
		int needs_update = 0, available = 0;
		unsigned min_driver_major = 0, min_driver_minor = 0;
		params->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_update);
		params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &min_driver_major);
		params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &min_driver_minor);
		NVSDK_NGX_Result ra = params->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
		printf("DLSS super sampling: available %d (query 0x%x), needs updated driver %d (minimum %u.%u)\n", available, (unsigned)ra, needs_update, min_driver_major, min_driver_minor);
	}
	NVSDK_NGX_D3D12_Shutdown1(dev);
	return 0;
}
