#pragma once

// Windows Firewall check and in-game fix popup for the host (firewall.c).
void firewall_install(void);
// Menu tick: hosting = this PC holds a campaign lobby as host.
void firewall_tick(int hosting);
