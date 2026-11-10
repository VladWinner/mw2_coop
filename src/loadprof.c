// QA tool ([native] loadprof=<seconds>, 64-bit): a sampling profile of the game's start and level load. A thread suspends every other thread of the process every few
// milliseconds, notes where it stands (the game's image: the address in 256-byte buckets; another module: its name) and, after the given time, writes the busiest places to the log.
// Waiting threads (ntdll, KERNELBASE, win32u) are left out, so what is listed is where the work is done: for judging what a level load spends its time on.
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "loadprof.h"
#include "game.h"

#ifdef _WIN64
#define MAX_KEYS 4096
static struct {
	char key[40];
	unsigned n;
} keys[MAX_KEYS];
static int nkeys;

static void note(const char *key)
{
	for (int i = 0; i < nkeys; i++)
		if (!strcmp(keys[i].key, key)) {
			keys[i].n++;
			return;
		}
	if (nkeys < MAX_KEYS) {
		lstrcpynA(keys[nkeys].key, key, sizeof(keys[0].key));
		keys[nkeys++].n = 1;
	}
}

static unsigned prof_delay;

static DWORD WINAPI prof_thread(LPVOID arg)
{
	DWORD seconds = (DWORD)(uintptr_t)arg, self = GetCurrentThreadId(), pid = GetCurrentProcessId();
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	HMODULE mod_self = NULL; // this DLL: its own buckets are told apart from the real winmm.dll
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)prof_thread, &mod_self);
	if (prof_delay) { // [native] loadprofat=<s>: the profile starts that many seconds after the game started (a level's fight, not its load)
		Sleep(prof_delay * 1000);
		log_printf("loadprof: sampling now");
	}
	DWORD start = GetTickCount();
	unsigned samples = 0;
	while (GetTickCount() - start < seconds * 1000) {
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		THREADENTRY32 te = {.dwSize = sizeof(te)};
		if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te))
			do {
				if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
					continue;
				HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
				if (!h)
					continue;
				if (SuspendThread(h) != (DWORD)-1) {
					CONTEXT c;
					memset(&c, 0, sizeof(c));
					c.ContextFlags = CONTEXT_CONTROL;
					if (GetThreadContext(h, &c)) {
						HMODULE m = NULL;
						char key[40];
						if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)c.Rip, &m) && m) {
							char name[MAX_PATH];
							DWORD n = GetModuleFileNameA(m, name, sizeof(name));
							const char *file = name;
							while (n > 0 && name[n - 1] != 0x5c)
								n--;
							file = name + n;
							if (m == (HMODULE)base)
								wsprintfA(key, "exe+%06x thread %lu", (unsigned)(((uint8_t *)c.Rip - base) & ~0xffull), (unsigned long)(te.th32ThreadID % 100000));
							else if (m == mod_self)
								wsprintfA(key, "mod+%06x thread %lu", (unsigned)(((uint8_t *)c.Rip - (uint8_t *)m) & ~0xffull), (unsigned long)(te.th32ThreadID % 100000));
							else if (lstrcmpiA(file, "ntdll.dll") && lstrcmpiA(file, "KERNELBASE.dll") && lstrcmpiA(file, "win32u.dll") && lstrcmpiA(file, "KERNEL32.DLL"))
								wsprintfA(key, "%s thread %lu", file, (unsigned long)(te.th32ThreadID % 100000));
							else
								key[0] = 0;
							if (key[0])
								note(key);
						}
						samples++;
					}
					ResumeThread(h);
				}
				CloseHandle(h);
			} while (Thread32Next(snap, &te));
		if (snap != INVALID_HANDLE_VALUE)
			CloseHandle(snap);
		Sleep(5);
	}
	// the busiest first
	for (int i = 0; i < nkeys; i++)
		for (int j = i + 1; j < nkeys; j++)
			if (keys[j].n > keys[i].n) {
				typeof(keys[0]) t = keys[i];
				keys[i] = keys[j];
				keys[j] = t;
			}
	log_printf("loadprof: %u thread samples in %lu s", samples, (unsigned long)seconds);
	{ // where the work was: the game, this mod, the rest (graphics driver, system)
		unsigned exe_n = 0, mod_n = 0, other_n = 0;
		for (int i = 0; i < nkeys; i++) {
			if (!strncmp(keys[i].key, "exe+", 4))
				exe_n += keys[i].n;
			else if (!strncmp(keys[i].key, "mod+", 4))
				mod_n += keys[i].n;
			else
				other_n += keys[i].n;
		}
		log_printf("loadprof: busy samples: game %u, this mod %u, other modules %u", exe_n, mod_n, other_n);
	}
	{ // what the process read and how long each thread worked (CPU time, user + kernel) in that time: a thread that worked less than the clock was waiting (disk, GPU, locks)
		IO_COUNTERS io;
		if (GetProcessIoCounters(GetCurrentProcess(), &io))
			log_printf("loadprof: process read %llu MB in %llu reads, wrote %llu MB", (unsigned long long)(io.ReadTransferCount >> 20), (unsigned long long)io.ReadOperationCount, (unsigned long long)(io.WriteTransferCount >> 20));
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		THREADENTRY32 te = {.dwSize = sizeof(te)};
		if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te))
			do {
				if (te.th32OwnerProcessID != pid)
					continue;
				HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
				FILETIME c, e, k, u;
				if (h && GetThreadTimes(h, &c, &e, &k, &u)) {
					unsigned long long ms = (((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime) + ((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime)) / 10000;
					if (ms > 300)
						log_printf("loadprof: thread %lu worked %llu ms (user %llu, kernel %llu)", (unsigned long)te.th32ThreadID, ms, (((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10000), (((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime) / 10000));
				}
				if (h)
					CloseHandle(h);
			} while (Thread32Next(snap, &te));
		if (snap != INVALID_HANDLE_VALUE)
			CloseHandle(snap);
	}
	for (int i = 0; i < nkeys && i < 40; i++)
		log_printf("loadprof: %5u  %s", keys[i].n, keys[i].key);
	return 0;
}

// QA: the engine's aspect numbers every 5 seconds (video aspect 0x4113738, pixel aspect 0x411373c, the cgame's aspect 0x43c640, the render side's 0x548c0c, window height 0x4113724)
static DWORD WINAPI aspect_thread(LPVOID unused)
{
	(void)unused;
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	for (;;) {
		Sleep(5000);
		log_printf("aspectlog: video %g pixel %g cgame %g render %g height %d", *(float *)(base + XR(0x4113738)), *(float *)(base + XR(0x411373c)), *(float *)(base + XR(0x43c640)), *(float *)(base + XR(0x548c0c)),
			   *(int *)(base + XR(0x4113724)));
		for (int n = 0; n < 6; n++) { // the screen placements (rva 0x5e0b80 + n * 0x70) and the flag the place lookup tests (0x5e0ccc)
			const float *q = (const float *)(base + XR(0x5e0b80) + n * 0x70);
			log_printf("aspectlog: placement %d: scale %g %g | full %g %g | real viewport %g %g %g %g | virt view %g %g %g %g | real view %g %g %g %g | last %g %g", n, q[0], q[1], q[2], q[3], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15], q[16], q[17], q[26], q[27]);
		}
		log_printf("aspectlog: place lookup flag (0x5e0ccc) = %d", *(int *)(base + XR(0x5e0ccc)));
	}
	return 0;
}

void aspectlog_start(void)
{
	HANDLE t = CreateThread(NULL, 0, aspect_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
}


// QA ([native] readlog=1): the engine's ReadFile calls (its import at rva 0x31c308): how many, how big, and how long the process waited in them, per size class, logged every 10 seconds
static BOOL (WINAPI *readfile_original)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static volatile LONG64 rf_calls[8], rf_bytes[8], rf_ticks[8];
static BOOL WINAPI readfile_hook(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov)
{
	LARGE_INTEGER a, b;
	QueryPerformanceCounter(&a);
	BOOL r = readfile_original(h, buf, n, got, ov);
	QueryPerformanceCounter(&b);
	int c = n <= 512 ? 0 : n <= 2048 ? 1 : n <= 8192 ? 2 : n <= 32768 ? 3 : n <= 131072 ? 4 : n <= 1048576 ? 5 : 6;
	InterlockedIncrement64(&rf_calls[c]);
	InterlockedAdd64(&rf_bytes[c], n);
	InterlockedAdd64(&rf_ticks[c], b.QuadPart - a.QuadPart);
	return r;
}
static DWORD WINAPI readlog_thread(LPVOID unused)
{
	(void)unused;
	static const char *const names[7] = {"<=512", "<=2K", "<=8K", "<=32K", "<=128K", "<=1M", ">1M"};
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	for (;;) {
		Sleep(10000);
		for (int c = 0; c < 7; c++)
			if (rf_calls[c])
				log_printf("readlog: %-6s %8lld calls %8lld KB %8.0f ms inside ReadFile", names[c], (long long)rf_calls[c], (long long)(rf_bytes[c] >> 10), 1000.0 * (double)rf_ticks[c] / (double)f.QuadPart);
	}
	return 0;
}

void readlog_start(void)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	void **slot = (void **)(base + XR(0x31c308));
	DWORD old;
	if (*slot != (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "ReadFile") && *slot != (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "ReadFile")) {
		log_printf("readlog: the ReadFile import is not where expected");
		return;
	}
	readfile_original = (BOOL(WINAPI *)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED))*slot;
	if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
		return;
	*slot = (void *)readfile_hook;
	VirtualProtect(slot, sizeof(void *), old, &old);
	HANDLE t = CreateThread(NULL, 0, readlog_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
	log_printf("readlog: ReadFile calls are counted");
}

void loadprof_start(unsigned seconds, unsigned delay)
{
	prof_delay = delay;
	HANDLE t = CreateThread(NULL, 0, prof_thread, (LPVOID)(uintptr_t)seconds, 0, NULL);
	if (t)
		CloseHandle(t);
}
#else
void readlog_start(void)
{
}
void aspectlog_start(void)
{
}
void loadprof_start(unsigned seconds, unsigned delay)
{
	(void)seconds;
	(void)delay;
}
#endif
