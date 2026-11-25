#pragma once
// A field of view of its own for the first-person weapon: the dvar cg_weaponFov (src/weaponfov.c, 64-bit). 65 is the console-like default; 0: the weapon follows
// the view's field of view. Its correction reads cg_fov * cg_fovScale, including the Enhanced page's direct cg_fov changes.
void weaponfov_install(void); // the hook on the weapon's projection (from the DLL's start, with the other code hooks)
void weaponfov_tick(void);    // the menu tick (main thread): the dvar once the game's dvars are there
