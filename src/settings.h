#pragma once
#include <stdint.h>

// <game dir>\mw2coop\settings.ini; call from DllMain, before the game starts.
void settings_load(void);
// The game's network port (default 28960).
uint16_t settings_port(void);
