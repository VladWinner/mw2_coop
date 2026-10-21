#pragma once

// UPnP port forward of the game's UDP port while this PC hosts (upnp.c).
void upnp_install(void);
// Control thread: 1 while this PC hosts a co-op lobby or game.
void upnp_want(int hosting);
