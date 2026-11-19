#pragma once
// Network co-op watch (netwatch.c): the partner's silence and the two PCs' games going apart, shown in the game's message window.
void netwatch_install(void);
void netwatch_poll(void);			// control.c, every 250 ms
void netwatch_partner_packet(void);		// net.c: a packet from the partner came in
void netwatch_on_desync_packet(int level_time); // net.c: the partner's PC found the games apart (packet mw2coop_desync)
int netwatch_send_paused(void);			// net.c: QA mw2coop_netpause holds the packets to the partner
void netwatch_on_packet(const char *text);	// net.c: "mw2coop_nw <word> [n]" from the partner
void netwatch_resync_started(void);		// saves.c: the client got the host's mw2coop_resync
