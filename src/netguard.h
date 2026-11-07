#pragma once

// Packet guard (netguard.c), applied to every packet before the game sees it (net.c). The idea comes from the security fixes of the
// MW2 Xbox 360 recompilation (docs/security.md there): the game's 2009 network code trusts what it receives. This guard drops the
// packets that no co-op session sends and that the game would act on:
//  - joinParty and rcon (the multiplayer's party join with its stack overflow, CVE-2019-20893, and remote console): never used by co-op
//  - a co-op command (coop...) from a machine that is not the partner, once the partner is known; and any co-op command but coopConnect
//    before it is known
//  - a map name in coopLaunch / coopAccept that holds a ';', a '"' or a line break: the game pastes it into a "map" command, so text
//    after a separator would run as console commands
// Returns NULL when the packet may pass, else a short reason (for the log). Connected game traffic (not 0xffffffff) is not looked at.
const char *netguard_check(const char *buf, int len, int partner_known, int from_partner);
