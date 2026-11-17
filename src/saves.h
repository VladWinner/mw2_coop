#pragma once

// The game's own saves in a co-op session (saves.c): its checkpoints as in single player, and a failed mission loads the last one on both machines.
void saves_install(void);
// 1 when this build loads the game's checkpoints in a co-op session (the lobby tells the host the partner's: menus_partner_saves)
int saves_supported(void);
// A co-op checkpoint was committed with this tag (a network client tells the host)
void saves_note_checkpoint(const char *tag);
// net.c: "mw2coop_cp" / "mw2coop_retry" from the partner; 1 if it was one
int saves_on_packet(const char *buf, int len);
