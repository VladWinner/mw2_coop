#pragma once

// The game's own saves in a co-op session (saves.c): its checkpoints as in single player, and a failed mission loads the last one on both machines.
void saves_install(void);
// 1 when this build loads the game's checkpoints in a co-op session (the lobby tells the host the partner's: menus_partner_saves)
int saves_supported(void);
// A co-op checkpoint was committed with this tag (a network client tells the host)
void saves_note_checkpoint(const char *tag);
// net.c: "mw2coop_cp" / "mw2coop_retry" from the partner; 1 if it was one
int saves_on_packet(const char *buf, int len);
// control.c: the scripts asked for the restart after a failed mission at this game time (maps\_mw2coop::mission_failed, "restart @<time>")
void saves_note_restart_request(int game_time);
// The host, after the client found the two games apart from level time <diverged_at> on (netwatch.c): both PCs load the last checkpoint they hold in common, if
// it is older than that. 1 started, 0 one is on its way, below 0 not possible (<why> says why).
int saves_resync(int diverged_at, char *why, int why_size);
