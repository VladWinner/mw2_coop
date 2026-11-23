// Co-op markers ("pings", owner 2026-11-23): one key marks what the player looks at, for both players; held, it opens a wheel of eight kinds of marker. The markers
// and the wheel are the script's (content/scripts/maps/_mw2coop_ping.gsc); this file takes the key, the pad's Back and the mouse / right stick of an open wheel to it.
#pragma once

// the console commands +mw2coop_ping / -mw2coop_ping (a key is bound to it: [input] ping_key, Z by default), mw2coop_ping (a tap at once) and the QA's mw2coop_ping_qa
void ping_install(void);
// the mouse read of CL_MouseMove (src/native.c), each frame, for the player the mouse turns (0 or 1): while his wheel is open the movement goes to it and x, y become 0
// (the view stays); returns 1 then
int ping_mouse(int player, float *x, float *y);
// a pad's frame (src/native.c, in a level): Back held or not and the look stick (-1..1, y up); returns 1 while his wheel is open (his view must not turn)
int ping_pad(int player, int back, float rx, float ry);
// main menu, main thread: binds [input] ping_key to +mw2coop_ping once, when the player's config has no binding for that key and none for the command (a key bound to
// the old tap-only mw2coop_ping is bound to +mw2coop_ping)
void ping_menu_tick(void);
