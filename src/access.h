#pragma once
#include <stdint.h>

// Lobby access (access.c): open / password / Steam friends only, the text popup, and the
// host's check of each coopConnect.
void access_install(void);
// Menu tick (main thread): campaign_lobby = this PC is in a lobby holding a campaign mission.
void access_tick(int campaign_lobby, int host);
// The host's answer to a lobby query: 0 = do not answer (Steam friends only), 'p' = password, '-' = open.
char access_lobby_flag(void);
// Join a lobby found on the network (asks for the password first if it is locked).
void access_join(const uint8_t ip[4], uint16_t port, int locked);
// Start in the host's lobby: 0 (and the password popup opens) in password mode without one.
int access_can_start(void);
// net.c: 1 if the packet is ours or a refused coopConnect (the game must not see it); a reply to
// send back to the sender is put into reply.
int access_on_packet(const char *buf, int len, const uint8_t ip[4], uint16_t port, char *reply, int reply_size,
		     int *reply_len);
