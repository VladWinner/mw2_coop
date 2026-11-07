#pragma once
#define COBJMACROS
#include <d3d9.h>

// FSR 3 upscaling of the game's picture on the Direct3D 12 layer (fsr3.c). settings.ini [display] fsr3=1 (needs d3d9on12, which it switches on), fsr3_dir = the folder with AMD's
// amd_fidelityfx_loader_dx12.dll. Called at each Present, after the picture is composed and before overlays.
int fsr3_enabled(void);
void fsr3_present(IDirect3DDevice9 *dev);
