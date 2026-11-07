# Upscalers (DLSS, FSR 3) for MW2 Enhanced: what is known, what is missing

Status 2026-11-07. Everything under "Verified" was run; everything under "Missing" is not built.

## The constraint

The game draws with Direct3D 9. DLSS (NVIDIA NGX, DX11 / DX12 / Vulkan) and FSR 3 (AMD FidelityFX SDK, DX12 only today) do not take Direct3D 9. They are temporal upscalers: each needs the
frame's color, its depth, per-pixel motion vectors, the camera's sub-pixel jitter, and the frame time. The 2009 renderer produces none of the last two and its depth is not readable by shaders.

## Verified (settings.ini `[display] d3d9on12=1`, src/d3dcap.c, tools/on12test)

- Windows' own `Direct3DCreate9On12Ex` runs the game's Direct3D 9 on a Direct3D 12 device. No maintained Direct3D 9 to 11 translator exists (DXUP, d3d9-to-11: archived; DXVK: Vulkan).
- The mod makes the Direct3D 12 device itself, on the adapter that has the primary monitor, with its own direct command queue, and hands both to Direct3D 9. The device the game's
  `IDirect3DDevice9On12::GetD3D12Device` returns is the mod's (log line `d3d9on12 probe`).
- On the laptop tested (NVIDIA RTX 3060 rendering, an AMD chip in the display path) a windowed swap chain of the plain model shows a white or black picture above about 800x600. Fixed by the
  flip model of Direct3D 9Ex: `D3DSWAPEFFECT_FLIPEX`, two back buffers, no multisampling. Exclusive full screen also works. Checked by screen captures of the game, native split screen included.
- `UnwrapUnderlyingResource` gives the Direct3D 12 resource under a Direct3D 9 surface. At a Present: the back buffer is a 1920x1080 `DXGI_FORMAT_B8G8R8A8_UNORM` (render target) and the depth
  buffer a 1920x1080 `DXGI_FORMAT_D24_UNORM_S8_UINT` with **2 samples** (the game's 2x multisampling) and `DENY_SHADER_RESOURCE`.

## Also done: FSR 1 spatial upscaler (src/upscale.c, `[display] upscale=1`)

EASU + RCAS ported from AMD's MIT FSR 1 header to shader model 3 pixel shaders, run at Present (plain Direct3D 9 and the Direct3D 12 layer both checked by screen capture). Lesson of the work: the
game keeps its own copy of the device state, so any pass drawn at Present must save the state (a D3DSBT_ALL state block), the render target and the depth surface and put them back; without that
the game's next frames were black. Not done: split screen with it on, mouse scaling in the menus.

## Downloaded (2026-11-07, owner's word; outside this repository, see local/third-party/PINS.md for commits, hashes and licenses)

- AMD FidelityFX SDK (official; headers carry an MIT notice, the binaries fall under the SDK's license document: binary-only redistribution, no reverse engineering, so the repository ships the headers only, third_party/fidelityfx): signed prebuilt `amd_fidelityfx_loader_dx12.dll` (C API entry), `amd_fidelityfx_upscaler_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll`, and the `ffx_api.h` /
  `ffx_upscale.h` / `ffx_api_dx12.h` headers: callable from the mod's compiler through LoadLibrary, no MSVC build.
- NVIDIA DLSS SDK (official, NVIDIA RTX SDK license): `nvngx_dlss.dll`, headers (`nvsdk_ngx*.h`), static libraries (MSVC) and the programming guides.

## Verified: both vendors' APIs run from the mod's toolchain on the RTX 3060 (2026-11-07)

- `tools/ffxprobe` (build.sh, ffxprobe.exe): loads AMD's `amd_fidelityfx_loader_dx12.dll`, makes a Direct3D 12 device and creates an FSR upscaler context (1280x720 to 1920x1080) through the C API:
  `ffxCreateContext` returns OK on the NVIDIA and the AMD adapter, `ffxDestroyContext` OK. No MSVC.
- `tools/dlssprobe` (build.sh, dlssprobe.exe): NVIDIA's NGX static library (`nvsdk_ngx_d.lib`, built with MSVC, C linkage) links with llvm-mingw once its two default-library requests (OLDNAMES,
  msvcprt) are answered by empty archives and `msvc_shim.cpp` supplies the few MSVC runtime symbols it needs (stack cookie, thread-safe static guards, operator new/delete, two STL error hooks).
  `NVSDK_NGX_D3D12_Init_with_ProjectID` succeeds on the RTX 3060 and `SuperSampling_Available` is 1 (minimum driver 470).
- Done (src/fsr3.c, `[display] fsr3=1` with `upscale_width/height`): FSR 3 as a real upscaler on the Direct3D 12 layer, checked by screen captures in the Trainer level (1280x720 render, 1920x1080 output).
  The game's frame, as traced (draw counts per pass): shadow maps (1024x2048), the scene into a 1280x720 render target, a pass into the back buffer corner (tonemap), a StretchRect of that corner into
  a 1280x720 target, the bloom chain (320x180 targets), then the last pass into the back buffer: bloom and HUD (this pass assumes the whole back buffer is the picture). FSR runs from the
  SetRenderTarget hook, just before that last pass: so the bloom is stretched over the upscaled picture correctly and the HUD, with its shadows, is drawn on top of it (the first version upscaled after the HUD
  and kept the HUD from the game's frame: wrong bloom scale and no subtitle shadows). Inputs per 3D frame: the depth of the world pass (the single-sample depth surface bound at the world viewport, which is
  not the 2-sample one bound at Present), read through a resolve compute pass (reconstructed with the viewport's MinZ/MaxZ, 0.015625..1); the world view-projection matrix caught from the vertex shader
  constants (rows c0..c3, c3.w about 5636; camera-relative and 2D matrices are told apart by c3) and camera motion vectors built from depth and the previous and current matrices (a compute pass, unjittered,
  in render pixels); a sub-pixel jitter (FSR's phase count and offsets, 18 phases at 1.5x) added to every matrix of the world pass; a texture LOD bias of log2(render/output) - 1 on the world pass's samplers;
  FSR's sharpening (`fsr3_sharpness`). Frames without a 3D view pass through untouched. The game's StretchRect of the whole enlarged back buffer into its 1280x720 target is redirected to copy the corner only.
  QA: FSR3_SHAKE sways every 3D matrix sideways with a depth-dependent shift (a static scene with known camera motion): correct vectors gave a clean sharp picture, none serrated edges, negated ones a blurry picture.
  Not done: a real moving camera in a controllable level, vectors for moving characters and the weapon (camera motion only); the 2x multisampled back buffer is off in window mode;
  mouse scaling in menus, native split screen with it on, and the FSR 1 path (same StretchRect and 3D-frame issues) are unchecked. DLSS: probed only (dlssprobe), not wired to frames.

## Missing

Readable depth, jitter, camera motion vectors, the library, the wiring, the render scale, HUD-free color (the upscale runs before the HUD) and sharpening are done for FSR 3 (see "Done" above). What is left:

1. **Own motion vectors for moving characters and the weapon** (camera motion only today: they ghost).
2. **A real moving-camera test** in a controllable level (the QA sway is a stand-in).
3. **Multisampled back buffer in window mode** (the flip model cannot have it).
4. **DLSS** (NGX: probed, not wired), native split screen with an upscaler, menu mouse scaling, and the FSR 1 path in levels.

## Order

1. A spatial upscaler first (FSR 1: EASU + RCAS from AMD's MIT shader code), in the Direct3D 9 Present hook: no jitter, no motion vectors, works without the Direct3D 12 layer.
2. Depth copy and a HUD-free color target on the Direct3D 12 layer.
3. Jitter, rebuilt camera motion vectors.
4. FSR 3 upscaler, then DLSS, behind the same inputs.
