#include <string.h>

#include "netguard.h"

// An out-of-band packet: four 0xff bytes, then the text. (Any length from 4: a 7-byte heartbeat is checked too, audit 2026-11-18.)
static int is_oob(const char *buf, int len)
{
	return len >= 4 && (unsigned char)buf[0] == 0xff && (unsigned char)buf[1] == 0xff && (unsigned char)buf[2] == 0xff && (unsigned char)buf[3] == 0xff;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

// the n bytes at a are the text b, letter case aside (the game may compare its commands without regard to case)
static int same_text(const char *a, const char *b, int n)
{
	for (int i = 0; i < n; i++)
		if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
			return 0;
	return 1;
}

// the next place at or after `from` in [buf, buf+len) where the text word (followed by a NUL) starts, or -1
static int find_word_from(const char *buf, int len, const char *word, int from)
{
	int n = (int)strlen(word) + 1; // with its NUL
	for (int i = from < 0 ? 0 : from; i + n <= len; i++)
		if (buf[i + n - 1] == 0 && same_text(buf + i, word, n - 1))
			return i;
	return -1;
}

static int find_word(const char *buf, int len, const char *word)
{
	return find_word_from(buf, len, word, 0);
}

// A string field at i: at most `max` characters up to its NUL, none of them a command separator. Returns the index after its NUL, -1 when it holds a separator,
// -2 when it does not end inside the packet.
static int string_field(const char *buf, int len, int i, int max)
{
	for (int n = 0; i < len && buf[i]; i++, n++) {
		if (n >= max)
			return -2;
		if (buf[i] == ';' || buf[i] == '"' || buf[i] == '\n' || buf[i] == '\r')
			return -1;
	}
	return i < len ? i + 1 : -2;
}

// Checks found by reading the game's own handlers (Ghidra, iw4sp.exe 64-bit): the layouts the handlers read.
//  coopAccept (0x22d460, client): byte slot, byte players, string map, ...   the slot indexes the two member records (stride 0x2650) unchecked: 0 or 1
//  coopLaunch (0x22d2e0, client): byte players, string map (0x100), long, string start, long, then (key byte, value byte) pairs until a key byte 2, byte
//      the players byte is the count of a loop over member records (two exist), and without the key byte 2 the pair loop never ends (a read past the end gives the same value forever)
//  hb (lobby heartbeat, 0xd68b0, client): the record list ends with an owner byte 2 and the owner indexes the member records unchecked; a real packet ends with that 2
// One packet can carry several messages: every one of these commands in it is checked, not only the first (audit 2026-11-18).
static const char *coop_accept_bad(const char *cmd, int cmd_len)
{
	for (int at = find_word(cmd, cmd_len, "coopAccept"); at >= 0; at = find_word_from(cmd, cmd_len, "coopAccept", at + 1)) {
		int i = at + 11;
		if (i + 2 > cmd_len)
			return "a coopAccept cut short"; // the handler would read past the end
		if ((unsigned char)cmd[i] > 1 || (unsigned char)cmd[i + 1] > 2)
			return "a coopAccept with a slot or player count outside the two players"; // the slot (0 or 1) and the number of players (at most 2)
		if (string_field(cmd, cmd_len, i + 2, 0x100) == -1)
			return "a map name with a command separator in a co-op launch";
	}
	return NULL;
}

static const char *coop_launch_bad(const char *cmd, int cmd_len)
{
	for (int at = find_word(cmd, cmd_len, "coopLaunch"); at >= 0; at = find_word_from(cmd, cmd_len, "coopLaunch", at + 1)) {
		int i = at + 11;
		if (i >= cmd_len || (unsigned char)cmd[i] > 2)
			return "a coopLaunch with a player count too high"; // the number of players
		i++;
		for (int s = 0; s < 2; s++) { // the map and the start point, strings, each followed by a long
			int next = string_field(cmd, cmd_len, i, 0x100);
			if (next == -1)
				return "a map name with a command separator in a co-op launch";
			if (next < 0)
				return "a coopLaunch without the end of a string";
			i = next + 4;
		}
		int ended = 0;
		for (int pairs = 0; pairs < 64 && !ended; pairs++) { // (key, value) until the key 2
			if (i >= cmd_len)
				break;
			if ((unsigned char)cmd[i] == 2)
				ended = 1;
			else
				i += 2;
		}
		if (!ended)
			return "a coopLaunch without its terminator";
	}
	return NULL;
}

static int lobby_hb_bad(const char *p, int n)
{
	// "hb" (a lobby heartbeat of the host): its record list ends with an owner byte 2, the last byte of the packet
	if (n >= 3 && same_text(p, "hb", 2) && p[2] == 0)
		return n > 1200 || (unsigned char)p[n - 1] != 2;
	return 0;
}

// The co-op commands that make a client act (load a map, take a slot, restart): never from an address that is not the partner's, also when the packet starts
// with a coopConnect (a stranger's connect is let through for the host to answer; what rides along in the same packet is not).
static int has_session_command(const char *cmd, int cmd_len)
{
	static const char *const words[] = {"coopLaunch", "coopAccept", "coopRestart", "coopLoaded"};
	for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++)
		if (find_word(cmd, cmd_len, words[w]) >= 0)
			return 1;
	return 0;
}

const char *netguard_check(const char *buf, int len, int partner_known, int from_partner)
{
	if (!is_oob(buf, len))
		return NULL;
	const char *p = buf + 4;
	int n = len - 4;
	if (n >= 9 && same_text(p, "joinParty", 9))
		return "joinParty (the multiplayer's party join, not used by co-op; the game also refuses it)";
	if (n >= 4 && same_text(p, "rcon", 4))
		return "rcon (remote console)";
	if (lobby_hb_bad(p, n))
		return "a lobby heartbeat without its terminator";
	// a co-op frame: 'R', 0, a sequence byte, then the command (one or more messages in a packet)
	if (n < 4 || p[0] != 'R' || p[1] != 0)
		return NULL;
	const char *cmd = p + 3;
	int cmd_len = n - 3;
	if (cmd_len < 5 || !same_text(cmd, "coop", 4))
		return NULL;
	int connect = find_word(cmd, cmd_len, "coopConnect") == 0;
	if (partner_known && !from_partner && (!connect || has_session_command(cmd, cmd_len)))
		return "a co-op command from an address that is not the partner's";
	if (!partner_known && (!connect || has_session_command(cmd, cmd_len)))
		return "a co-op command before the partner connected";
	const char *why = coop_accept_bad(cmd, cmd_len);
	if (!why)
		why = coop_launch_bad(cmd, cmd_len);
	return why;
}
