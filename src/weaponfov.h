#pragma once
// A field of view of its own for the first-person weapon: the dvar cg_weaponFov (src/weaponfov.c, 64-bit). 0: the game's way (the weapon follows the view's
// field of view); 65: the console game's.
void weaponfov_install(void); // the hook on the weapon's projection (from the DLL's start, with the other code hooks)
void weaponfov_tick(void);    // the menu tick (main thread): the dvar once the game's dvars are there
