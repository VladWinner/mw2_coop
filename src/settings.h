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
// mw2coop\player.ini [player] id: 32 lowercase hex digits, made up once (main thread only).
const char *settings_player_id(void);
