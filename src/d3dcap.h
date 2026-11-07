#pragma once

// The game's Direct3D 9 device, caught when the game creates it (src/d3dcap.c). The 32-bit build keeps the device in no global a search finds,
// so d3d9.dll's Direct3DCreate9 is hooked before the game window exists and CreateDevice of the object it returns hands over the device.
void d3dcap_install(void); // from DllMain, before the game creates its device
void *d3dcap_device(void); // the newest device the game created, or NULL
// Direct3D 9 on Direct3D 12 (settings.ini [display] d3d9on12): logs once the back buffer and the depth buffer as Direct3D 12 resources; call at a Present.
void d3dcap_on12_probe(void *dev9);
// Direct3D 9 on Direct3D 12 (d3d9on12): the Direct3D 12 device Direct3D 9 runs on and the direct command queue made for it (NULL otherwise).
void *d3dcap_d3d12_device(void);
void *d3dcap_d3d12_queue(void);
