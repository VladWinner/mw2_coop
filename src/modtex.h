#pragma once
// The textures the mod makes or reads on the game's device (the logo, the gamepad pictures, the font atlas): Direct3D 9 textures in the game's own renderer, images of the native renderer when
// that is on (the same field of the game's image struct holds either). These functions are the Direct3D 9 calls the mod used (GetLevelDesc, LockRect, UnlockRect, CreateTexture ...) for both.
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>

// the description of level `level` (Width, Height, Format, Pool: the native renderer's images count as managed)
HRESULT modtex_get_level_desc(void *tex, UINT level, D3DSURFACE_DESC *desc);
UINT modtex_levels(void *tex);
// a texture of `like`'s renderer (like: an existing texture of the game); NULL on failure
void *modtex_create(void *like, UINT w, UINT h, UINT levels, D3DFORMAT format);
HRESULT modtex_lock(void *tex, UINT level, D3DLOCKED_RECT *lr, DWORD flags);
HRESULT modtex_unlock(void *tex, UINT level);
void modtex_release(void *tex);
// The Direct3D 9 device `tex` belongs to (only compared, no reference kept); NULL on the native renderers, whose images outlive their devices.
void *modtex_device_of(void *tex);

// Copies of game materials with a picture of the mod's own (the About page's icons, the gamepad pictures, the pad button pictures) are made from a template material:
// its bytes, its texture table and its image, with the mod's texture in the image. A vid_restart (a video setting changed in the menus) makes a new device and the game
// loads the template again: after one, the logo material's fields, its table and its image all differed from the copy's (iw4x-sp test 2026-11-20), so the copy pointed at
// what the game had let go, and its own texture was on the old device. The About page then showed a black box, and after a few video changes a player's About page
// "glitched and showed an error" (iw4x-sp, 2026-11-20). A builder keeps what it copied (modtex_snapshot_take) and makes its copy again, with a new texture, when the
// template is no longer that (modtex_snapshot_differs). The old texture is released then: safe after the game released the old device, because in Direct3D 9 every
// resource holds a reference to its device (the old device lived on for the rest of the run until 2026-11-20).
typedef struct {
	uint8_t mat[0x100], tab[0x40], img[0x80];
	void *device;
	int taken;
} modtex_snapshot_t;
void modtex_snapshot_take(modtex_snapshot_t *s, const void *material, const void *table, const void *image);
// 1 when nothing was taken yet, or the template's material, table or image bytes, or the device of its image's texture, are not what was taken
int modtex_snapshot_differs(const modtex_snapshot_t *s, const void *material, const void *table, const void *image);
// the largest texture the renderer takes (0 when it cannot say)
void modtex_max_size(void *tex, int *w, int *h);
