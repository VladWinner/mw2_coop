#pragma once
#define COBJMACROS
#include <d3d9.h>

// The output size of the upscalers FSR 3 and DLSS (upscale.c): settings.ini [display] upscale_width, upscale_height. (FSR 1, [display] upscale=1, was removed on 2026-11-18.)
int upscale_enabled(void);
// the native Direct3D 12 renderer: sets the sizes when FSR 3 / DLSS is on and the game's size is not the output size (see upscale.c)
int upscale_native_sizes(int game_w, int game_h, int *out_width, int *out_height);
// CreateDevice (the Direct3D 9 device on the Direct3D 12 layer): the back buffer gets the output size, the size the game asked for is kept as the internal one. 1 if changed.
int upscale_adjust_params(D3DPRESENT_PARAMETERS *pp);
// After the device (or the native swap chain) is made: the window's client area is made the output size.
void upscale_device_made(HWND window);
// The size the game renders at (for the mouse's scale and fsr3.c); 0 if nothing is upscaled.
int upscale_internal_size(int *w, int *h);
