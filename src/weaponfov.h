#pragma once
#include <d3d9.h>

// A field of view of its own for the first-person weapon (src/weaponfov.c). Called when the device hooks are put in (native.c comp_hook): patch is native.c's vtable patcher,
// view_tan() gives tan(fov / 2) of the view being drawn in the game's own unit, weapon_fov_degrees is [native] weaponfov (0: off, only the log is made when log is set).
void weaponfov_hook(IDirect3DDevice9 *dev, void (*patch)(void **slot, void *hook, void **orig), float (*view_tan)(void), float weapon_fov_degrees, int log);
