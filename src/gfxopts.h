#pragma once

// Graphics options (gfxopts.c): what the game's own option menus offer, and the limits raised where the engine allows more (the field of view slider).
// Menu tick (main thread, in the menus): finds the option pages once the UI has loaded them.
void gfxopts_tick(void);
