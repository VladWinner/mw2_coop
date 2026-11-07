// Unit test of src/netguard.c: build and run with tools/test_netguard.sh. Real co-op packets from a session log, and the attacks.
#include <stdio.h>
#include <string.h>

#include "../src/netguard.h"

static int fails;

static void expect(const char *name, const char *buf, int len, int known, int from, int blocked)
{
	const char *why = netguard_check(buf, len, known, from);
	int ok = (why != NULL) == blocked;
	printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", name, why ? " -> " : "", why ? why : "");
	if (!ok)
		fails++;
}

#define PKT(name, known, from, blocked, ...) do { static const char b[] = __VA_ARGS__; expect(name, b, (int)sizeof(b) - 1, known, from, blocked); } while (0)

int main(void)
{
	// real packets (src/net.c log of a session): connect, launch, loaded, restart, accept, heartbeat
	PKT("coopConnect from a stranger before a partner", 0, 0, 0, "\xff\xff\xff\xffR\x00\x00" "coopConnect\x00\x1d-\xc8\x0e\x1b\x01\x00\x10\x01stxlvn\x00\x06");
	PKT("coopConnect and coopAcceptAck in one packet", 0, 0, 0, "\xff\xff\xff\xffR\x00\x00" "coopConnect\x00\x1d-\xc8\x0e\x1b\x01\x00\x10\x01stxlvn\x00\x06\x01" "coopAcceptAck\x00\x00\xb8\x0b");
	PKT("coopLaunch from the partner", 1, 1, 0, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending\x00\x00\x00\x00\x00\x00/N{\x0f\x00\x01\x01\x01\x02\x01");
	PKT("coopAccept from the partner", 1, 1, 0, "\xff\xff\xff\xffR\x00\x00" "coopAccept\x00\x01\x02" "ending\x00\x00\x00\x00\x00\x01\x01\x00\xb8\x0b");
	PKT("coopAccept with the slot 0", 1, 1, 0, "\xff\xff\xff\xffR\x00\x00" "coopAccept\x00\x00\x02" "ending\x00\x00\x00");
	PKT("coopLoaded from the partner", 1, 1, 0, "\xff\xff\xff\xffR\x00\x03" "coopLoaded\x00");
	PKT("coopRestart from the partner", 1, 1, 0, "\xff\xff\xff\xffR\x00\x02" "coopRestart\x00\x10\x00\x00\x00/N{\x0f");
	PKT("a lobby heartbeat of the host", 1, 1, 0, "\xff\xff\xff\xff" "hb\x00" "ending\x00\x00\x00\x01\x00" "0000000000000000000000000000000000000000000000000000\x00\x00\x00\x00\x00\x01\x01\x00\x00\x00\x00\x00\x02");
	PKT("a launch with a map with an underscore and digits", 1, 1, 0, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "mp_rust_2\x00\x00\x00\x00\x00\x00/N{\x0f\x00\x01\x02");
	PKT("the game's own ipdetect", 1, 0, 0, "\xff\xff\xff\xff" "ipdetect\x00\x00\x00\x01\x00\x14");
	PKT("connected traffic is not looked at", 1, 0, 0, "\x12\x34\x56\x78 coopLaunch;quit");
	// the attacks
	PKT("joinParty", 1, 1, 1, "\xff\xff\xff\xff" "joinParty\x00\x01\x02");
	PKT("rcon", 1, 1, 1, "\xff\xff\xff\xff" "rcon secret quit");
	PKT("coopLoaded from a stranger", 1, 0, 1, "\xff\xff\xff\xffR\x00\x03" "coopLoaded\x00");
	PKT("coopLaunch from a stranger", 1, 0, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending\x00");
	PKT("coopLaunch before any partner", 0, 0, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending\x00");
	PKT("a map name with a separator (launch)", 1, 1, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending;quit\x00");
	PKT("a map name with a quote (accept)", 1, 1, 1, "\xff\xff\xff\xffR\x00\x00" "coopAccept\x00\x01\x02" "x\"; quit\x00");
	PKT("a map name with a line break", 1, 1, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending\nquit\x00");
	PKT("a separator right after the count bytes", 1, 1, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02;quit\x00");
	// found by reading the game's handlers
	PKT("a lobby heartbeat without its terminator", 1, 1, 1, "\xff\xff\xff\xff" "hb\x00" "ending\x00\x00\x00\x01\x00" "00000000\x00\x00\x00\x00\x00\x01\x01\x00");
	PKT("coopAccept with the slot 255", 1, 1, 1, "\xff\xff\xff\xffR\x00\x00" "coopAccept\x00\xff\x02" "ending\x00\x00\x00");
	PKT("coopAccept with the slot 2", 1, 1, 1, "\xff\xff\xff\xffR\x00\x00" "coopAccept\x00\x02\x02" "ending\x00\x00\x00");
	PKT("coopAccept cut short after the name", 1, 1, 1, "\xff\xff\xff\xffR\x00\x00" "coopAccept\x00");
	PKT("coopLaunch with 255 players", 1, 1, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\xff" "ending\x00\x00\x00\x00\x00\x00/N{\x0f\x00\x01\x01\x01\x02\x01");
	PKT("coopLaunch without the key 2 (never ends)", 1, 1, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending\x00\x00\x00\x00\x00\x00/N{\x0f\x00\x01\x01\x01\x01\x01");
	PKT("coopLaunch cut inside the strings", 1, 1, 1, "\xff\xff\xff\xffR\x00\x01" "coopLaunch\x00\x02" "ending");
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
