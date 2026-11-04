#pragma once
#include <stdint.h>

// <game dir>\mw2coop\settings.ini; call from DllMain, before the game starts.
void settings_load(void);
// The game's network port (default 28960).
uint16_t settings_port(void);
// settings.ini [network] steam_relay (default 0).
int settings_steam_relay(void);
// settings.ini [display] fonts (default 1): the Russian game gets the mod's fonts (fonts.c); 0 keeps the game's own.
int settings_fonts(void);
// settings.ini [display] fontmip (default 1): the mod's font atlas is filtered between its mip levels (trilinear), so small text is smooth; 0 keeps the game's plain bilinear.
int settings_fontmip(void);
// settings.ini [display] darktitle (default 1): the game window gets the dark title bar and square corners of Windows 10/11 (window.c); 0 leaves it as the game makes it.
int settings_darktitle(void);
// settings.ini [display] hwgamma (default 1): the game sets the display's hardware gamma ramp from r_gamma (default 0.8, a darker picture than the windowed and borderless modes, which have no ramp) in exclusive full screen; 0 sets r_ignorehwgamma so that full screen looks like them (guards.c).
int settings_hwgamma(void);
// mw2coop\player.ini [player] id: 32 lowercase hex digits, made up once (main thread only).
const char *settings_player_id(void);
