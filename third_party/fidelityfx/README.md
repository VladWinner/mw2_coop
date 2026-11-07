AMD FidelityFX SDK C API headers, copied unchanged from the official repository https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK at commit
7539ae8b6568ad29ed78a753404351daf3b5cca7:
 - Kits/FidelityFX/api/include/ffx_api.h, ffx_api_types.h, dx12/ffx_api_dx12.h
 - Kits/FidelityFX/upscalers/include/ffx_upscale.h
Each header carries AMD's MIT notice at its top. LICENSE.md is the SDK's own license document (Kits/FidelityFX/docs/license.md): it applies to all files "except as noted", and for the
binaries it allows redistribution in binary form only, with the notice, and no reverse engineering. This repository therefore ships only the headers: the mod loads AMD's signed
`amd_fidelityfx_loader_dx12.dll` (and the `amd_fidelityfx_upscaler_dx12.dll` it loads) at run time from the folder named in settings.ini [display] fsr3_dir; the player or the packager
takes the DLLs from AMD's SDK release.
