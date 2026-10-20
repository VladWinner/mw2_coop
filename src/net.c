// Packet logging for QA: iw4sp.exe imports sendto (#20) and recvfrom (#17) from WSOCK32.dll by
// ordinal; their IAT slots are pointed at wrappers that log the game's UDP traffic.
// They also carry mw2coop's own packets: the lobby's ready state (menus.c), and the start point,
// sent by the host just before each coopLaunch it sends (see progress.c).
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "net.h"
#include "menus.h"
#include "progress.h"

typedef int(WINAPI *sendto_t)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int(WINAPI *recvfrom_t)(SOCKET, char *, int, int, struct sockaddr *, int *);

static sendto_t real_sendto;
static recvfrom_t real_recvfrom;
static volatile LONG budget = 150; // max logged packets per run

// The co-op partner: the host learns it from the client's coopConnect, the client from its own.
// mw2coop's lobby packets (net_send_mod) go there on the game's socket.
static SOCKET peer_socket = INVALID_SOCKET;
static struct sockaddr_in peer;
static CRITICAL_SECTION peer_lock;

static void log_packet(const char *dir, const struct sockaddr *sa, const char *buf, int len)
{
	// skip DemonWare traffic (not 0xffffffff out-of-band) and the lobby bandwidth test ("bw")
	if (len < 6 || *(const unsigned int *)buf != 0xffffffffu || (buf[4] == 'b' && buf[5] == 'w'))
		return;
	if (InterlockedDecrement(&budget) < 0)
		return;
	char addr[64] = "?";
	if (sa && sa->sa_family == AF_INET) {
		const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
		const unsigned char *ip = (const unsigned char *)&in->sin_addr;
		snprintf(addr, sizeof(addr), "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(in->sin_port));
	}
	char text[200];
	int n = 0;
	for (int i = 0; i < len && n < (int)sizeof(text) - 5; i++) {
		unsigned char c = (unsigned char)buf[i];
		if (c >= 0x20 && c < 0x7f && c != '\\')
			text[n++] = (char)c;
		else
			n += snprintf(text + n, sizeof(text) - n, "\\x%02x", c);
	}
	text[n] = 0;
	log_printf("net: %s %s len %d: %s", dir, addr, len, text);
}

static int contains(const char *buf, int len, const char *word)
{
	int wl = (int)strlen(word);
	for (int i = 0; i + wl <= len; i++)
		if (memcmp(buf + i, word, wl) == 0)
			return 1;
	return 0;
}

static void remember_peer(SOCKET s, const struct sockaddr *sa)
{
	if (!sa || sa->sa_family != AF_INET)
		return;
	EnterCriticalSection(&peer_lock);
	if (peer_socket != s || memcmp(&peer, sa, sizeof(peer)) != 0) {
		peer_socket = s;
		memcpy(&peer, sa, sizeof(peer));
		const unsigned char *ip = (const unsigned char *)&peer.sin_addr;
		log_printf("net: co-op partner %u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(peer.sin_port));
	}
	LeaveCriticalSection(&peer_lock);
}

int net_send_mod(const char *text)
{
	char pkt[128];
	int n = snprintf(pkt, sizeof(pkt), "\xff\xff\xff\xff%s", text);
	if (n <= 0 || n >= (int)sizeof(pkt) || !real_sendto)
		return 0;
	EnterCriticalSection(&peer_lock);
	int ok = peer_socket != INVALID_SOCKET &&
		 real_sendto(peer_socket, pkt, n, 0, (const struct sockaddr *)&peer, sizeof(peer)) == n;
	LeaveCriticalSection(&peer_lock);
	return ok;
}

static int WINAPI my_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
	if (len > 4 && *(const unsigned int *)buf == 0xffffffffu && !game_peek(RVA_PARTY_IS_HOST, 1) &&
	    contains(buf, len, "coopConnect"))
		remember_peer(s, to);
	// The party resends coopLaunch until the client acks it; the start packet goes along each time.
	if (len > 4 && *(const unsigned int *)buf == 0xffffffffu && game_peek(RVA_PARTY_IS_HOST, 1) &&
	    contains(buf, len, "coopLaunch")) {
		char pkt[96];
		int n = progress_launch_packet(pkt, sizeof(pkt));
		log_packet("send", to, pkt, n);
		real_sendto(s, pkt, n, flags, to, tolen);
	}
	log_packet("send", to, buf, len);
	return real_sendto(s, buf, len, flags, to, tolen);
}

static int WINAPI my_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
	int r = real_recvfrom(s, buf, len, flags, from, fromlen);
	if (r > 0) {
		log_packet("recv", from, buf, r);
		if (r > 4 && *(const unsigned int *)buf == 0xffffffffu && game_peek(RVA_PARTY_IS_HOST, 1) &&
		    contains(buf, r, "coopConnect"))
			remember_peer(s, from);
		if (progress_on_packet(buf, r) || menus_on_packet(buf, r)) {
			WSASetLastError(WSAEWOULDBLOCK); // ours: the game sees "no packet this time"
			return SOCKET_ERROR;
		}
	}
	return r;
}

void net_install(void)
{
	InitializeCriticalSection(&peer_lock);
	BYTE *base = (BYTE *)GetModuleHandleA(NULL);
	IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
		if (lstrcmpiA((const char *)(base + d->Name), "WSOCK32.dll") != 0)
			continue;
		IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + d->OriginalFirstThunk);
		IMAGE_THUNK_DATA64 *iat = (IMAGE_THUNK_DATA64 *)(base + d->FirstThunk);
		for (; names->u1.Ordinal; names++, iat++) {
			if (!IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
				continue;
			WORD ord = (WORD)IMAGE_ORDINAL64(names->u1.Ordinal);
			void *wrapper = NULL;
			if (ord == 20) {
				real_sendto = (sendto_t)iat->u1.Function;
				wrapper = (void *)my_sendto;
			} else if (ord == 17) {
				real_recvfrom = (recvfrom_t)iat->u1.Function;
				wrapper = (void *)my_recvfrom;
			}
			if (!wrapper)
				continue;
			DWORD old;
			VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
			iat->u1.Function = (ULONGLONG)wrapper;
			VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
			log_printf("net: hooked WSOCK32 #%u", ord);
		}
	}
}
