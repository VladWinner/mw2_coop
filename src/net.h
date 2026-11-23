#pragma once
#include <stdint.h>

// QA: log the game's UDP packets by pointing its WSOCK32 sendto/recvfrom IAT slots at wrappers.
void net_install(void);

// Send an out-of-band packet "\xff\xff\xff\xff<text>" to the co-op partner; 0 if none is known yet. (Up to about 250 bytes.)
int net_send_mod(const char *text);
// The same for a packet given whole, "\xff\xff\xff\xff" included, binary or long (up to 1400 bytes): voice.c, report.c.
int net_send_mod_bin(const void *data, int len);

// QA: log the next <packets> co-op packets (the log stops after 150 per run).
void net_log_budget(int packets);

// Send "\xff\xff\xff\xff<text>" to <port> on every IPv4 network's broadcast address; the number
// of packets sent.
// Send "\xff\xff\xff\xff<text>" to one address (port in host order) on the game's socket.
int net_send_mod_to(const uint8_t ip[4], uint16_t port, const char *text);
int net_broadcast_mod(const char *text, uint16_t port);

// 1 once the co-op partner is known (his connect or the host's accept was seen).
int net_partner_known(void);
// The partner's address (port in host order) as the last packets to or from him had it; 0 if none is known.
int net_partner_address(uint8_t ip[4], uint16_t *port);
