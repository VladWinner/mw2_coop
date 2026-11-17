#pragma once

// The game's own saves in a co-op session (saves.c): its checkpoints as in single player, and a failed mission loads the last one on both machines.
void saves_install(void);
// 1 when this build loads the game's checkpoints in a co-op session (the lobby tells the host the partner's: menus_partner_saves)
int saves_supported(void);
