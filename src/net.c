// Packet logging for QA: iw4sp.exe imports sendto (#20) and recvfrom (#17) from WSOCK32.dll by
// ordinal; their IAT slots are pointed at wrappers that log the game's UDP traffic.
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#include "log.h"
#include "net.h"

typedef int(WINAPI *sendto_t)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int(WINAPI *recvfrom_t)(SOCKET, char *, int, int, struct sockaddr *, int *);

static sendto_t real_sendto;
static recvfrom_t real_recvfrom;
static volatile LONG budget = 150; // max logged packets per run

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

static int WINAPI my_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
	log_packet("send", to, buf, len);
	return real_sendto(s, buf, len, flags, to, tolen);
}

static int WINAPI my_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
	int r = real_recvfrom(s, buf, len, flags, from, fromlen);
	if (r > 0)
		log_packet("recv", from, buf, r);
	return r;
}

void net_install(void)
{
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
