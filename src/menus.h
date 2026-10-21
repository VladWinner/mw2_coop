#pragma once
#include <stdint.h>

void menus_install(void);
// Ask one address for its lobby (an address typed in), then join it like a row of the list.
void menus_find_at(const uint8_t ip[4], uint16_t port);
void menus_poll(void); // control thread, every 250 ms: queues the menu work for the main thread

// net.c: a packet from the partner; 1 if it was mw2coop's lobby packet (the game must not see it).
int menus_on_packet(const char *buf, int len);

// net.c: a lobby query ("mw2coop_find") or a host's answer ("mw2coop_here <map>"); 1 if it was
// one (the game must not see it). An answer to send back to the sender is put into reply.
int menus_on_lobby_packet(const char *buf, int len, const uint8_t *from_ip, uint16_t from_port, char *reply,
			  int reply_size, int *reply_len);
