#include <string.h>

#include "netguard.h"

static int is_oob(const char *buf, int len)
{
	return len >= 8 && (unsigned char)buf[0] == 0xff && (unsigned char)buf[1] == 0xff && (unsigned char)buf[2] == 0xff && (unsigned char)buf[3] == 0xff;
}

// the first place in [buf, buf+len) where the text word (followed by a NUL) starts, or -1
static int find_word(const char *buf, int len, const char *word)
{
	int n = (int)strlen(word) + 1; // with its NUL
	for (int i = 0; i + n <= len; i++)
		if (memcmp(buf + i, word, n) == 0)
			return i;
	return -1;
}

// a map name after a co-op command: skip the few small count bytes, then the text up to its NUL must hold no command separator
static int map_name_bad(const char *buf, int len, const char *word)
{
	int at = find_word(buf, len, word);
	if (at < 0)
		return 0;
	int i = at + (int)strlen(word) + 1;
	int skipped = 0;
	while (i < len && skipped < 4 && (unsigned char)buf[i] < 0x20 && buf[i] != '\n' && buf[i] != '\r' && buf[i]) {
		i++;
		skipped++;
	}
	for (int n = 0; i < len && n < 64 && buf[i]; i++, n++)
		if (buf[i] == ';' || buf[i] == '"' || buf[i] == '\n' || buf[i] == '\r')
			return 1;
	return 0;
}

// Checks found by reading the game's own handlers (Ghidra, iw4sp.exe 64-bit): the layouts the handlers read.
//  coopAccept (0x22d460, client): byte slot, byte players, string map, ...   the slot indexes the two member records (stride 0x2650) unchecked: 0 or 1
//  coopLaunch (0x22d2e0, client): byte players, string map (0x100), long, string start, long, then (key byte, value byte) pairs until a key byte 2, byte
//      the players byte is the count of a loop over member records (two exist), and without the key byte 2 the pair loop never ends (a read past the end gives the same value forever)
//  hb (lobby heartbeat, 0xd68b0, client): the record list ends with an owner byte 2 and the owner indexes the member records unchecked; a real packet ends with that 2
static int coop_accept_bad(const char *cmd, int cmd_len)
{
	int at = find_word(cmd, cmd_len, "coopAccept");
	if (at < 0)
		return 0;
	int i = at + 11;
	if (i + 2 > cmd_len)
		return 1; // cut short: the handler would read past the end
	if ((unsigned char)cmd[i] > 1 || (unsigned char)cmd[i + 1] > 2)
		return 1; // the slot (0 or 1) and the number of players (at most 2)
	return 0;
}

static int coop_launch_bad(const char *cmd, int cmd_len)
{
	int at = find_word(cmd, cmd_len, "coopLaunch");
	if (at < 0)
		return 0;
	int i = at + 11;
	if (i >= cmd_len || (unsigned char)cmd[i] > 2)
		return 1; // the number of players
	i++;
	for (int s = 0; s < 2; s++) { // the map and the start point, strings
		int n = 0;
		while (i < cmd_len && cmd[i] && n < 0x100) {
			i++;
			n++;
		}
		if (i >= cmd_len || cmd[i])
			return 1; // no end of string
		i += 1 + 4; // its NUL and the long after it
	}
	for (int pairs = 0; pairs < 64; pairs++) { // (key, value) until the key 2
		if (i >= cmd_len)
			return 1; // no terminator
		if ((unsigned char)cmd[i] == 2)
			return 0;
		i += 2;
	}
	return 1;
}

static int lobby_hb_bad(const char *p, int n)
{
	// "hb" (a lobby heartbeat of the host): its record list ends with an owner byte 2, the last byte of the packet
	if (n >= 3 && memcmp(p, "hb", 3) == 0)
		return n > 1200 || (unsigned char)p[n - 1] != 2;
	return 0;
}

const char *netguard_check(const char *buf, int len, int partner_known, int from_partner)
{
	if (!is_oob(buf, len))
		return NULL;
	const char *p = buf + 4;
	int n = len - 4;
	if (n >= 9 && memcmp(p, "joinParty", 9) == 0)
		return "joinParty (the multiplayer's party join, not used by co-op; the game also refuses it)";
	if (n >= 4 && memcmp(p, "rcon", 4) == 0)
		return "rcon (remote console)";
	if (lobby_hb_bad(p, n))
		return "a lobby heartbeat without its terminator";
	// a co-op frame: 'R', 0, a sequence byte, then the command (one or more messages in a packet)
	if (n < 4 || p[0] != 'R' || p[1] != 0)
		return NULL;
	const char *cmd = p + 3;
	int cmd_len = n - 3;
	if (cmd_len < 5 || memcmp(cmd, "coop", 4) != 0)
		return NULL;
	int connect = find_word(cmd, cmd_len, "coopConnect") == 0;
	if (partner_known && !from_partner && !connect)
		return "a co-op command from an address that is not the partner's";
	if (!partner_known && !connect)
		return "a co-op command before the partner connected";
	if (map_name_bad(cmd, cmd_len, "coopLaunch") || map_name_bad(cmd, cmd_len, "coopAccept"))
		return "a map name with a command separator in a co-op launch";
	if (coop_accept_bad(cmd, cmd_len))
		return "a coopAccept with a slot or player count outside the two players";
	if (coop_launch_bad(cmd, cmd_len))
		return "a coopLaunch with a player count too high or without its terminator";
	return NULL;
}
