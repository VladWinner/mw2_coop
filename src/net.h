#pragma once
#include <stdint.h>

// QA: log the game's UDP packets by pointing its WSOCK32 sendto/recvfrom IAT slots at wrappers.
void net_install(void);

// Send an out-of-band packet "\xff\xff\xff\xff<text>" to the co-op partner; 0 if none is known yet.
int net_send_mod(const char *text);

// QA: log the next <packets> co-op packets (the log stops after 150 per run).
void net_log_budget(int packets);

// Send "\xff\xff\xff\xff<text>" to <port> on every IPv4 network's broadcast address; the number
// of packets sent.
int net_broadcast_mod(const char *text, uint16_t port);
