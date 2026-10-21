#pragma once
struct sockaddr;

// Game traffic through Steam P2P for Steam joins (steamnet.c); off unless settings.ini says so.
void steamnet_install(void);
// net.c: 1 if <to> is a Steam peer's virtual address (the packet went through Steam).
int steamnet_send(const char *buf, int len, const struct sockaddr *to);
// net.c: a packet from Steam (its length, <from> = the peer's virtual address), or 0.
int steamnet_receive(char *buf, int len, struct sockaddr *from, int *fromlen);
