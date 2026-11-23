#pragma once
struct sockaddr;

// Game traffic through Steam P2P for Steam joins (steamnet.c); off unless settings.ini says so.
#include <stdint.h>
void steamnet_install(void);
// 1 if <steam_id> is a Steam friend of this PC's user (ISteamFriends::GetFriendRelationship == friend); 0 without Steam.
int steam_friend(uint64_t steam_id);
// net.c: 1 if <to> is a Steam peer's virtual address (the packet went through Steam).
int steamnet_send(const char *buf, int len, const struct sockaddr *to);
// net.c: a packet from Steam (its length, <from> = the peer's virtual address), or 0.
int steamnet_receive(char *buf, int len, struct sockaddr *from, int *fromlen);
// rejoin.c: the Steam ID behind a peer's virtual address (0 when <ip> is none, or the relay is off), and the virtual address of a Steam ID (0 when the relay is off).
uint64_t steamnet_peer_id(const uint8_t ip[4]);
int steamnet_address_of(uint64_t id, uint8_t ip[4]);
