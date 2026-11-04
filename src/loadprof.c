// QA tool ([native] loadprof=<seconds>, 64-bit): a sampling profile of the game's start and level load. A thread suspends every other thread of the process every few
// milliseconds, notes where it stands (the game's image: the address in 256-byte buckets; another module: its name) and, after the given time, writes the busiest places to the log.
// Waiting threads (ntdll, KERNELBASE, win32u) are left out, so what is listed is where the work is done: for judging what a level load spends its time on.
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "loadprof.h"

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

static DWORD WINAPI prof_thread(LPVOID arg)
{
	DWORD seconds = (DWORD)(uintptr_t)arg, self = GetCurrentThreadId(), pid = GetCurrentProcessId();
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	DWORD start = GetTickCount();
	unsigned samples = 0;
	while (GetTickCount() - start < seconds * 1000) {
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		THREADENTRY32 te = {sizeof(te)};
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
		log_printf("aspectlog: video %g pixel %g cgame %g render %g height %d", *(float *)(base + 0x4113738), *(float *)(base + 0x411373c), *(float *)(base + 0x43c640), *(float *)(base + 0x548c0c),
			   *(int *)(base + 0x4113724));
		for (int n = 0; n < 6; n++) { // the screen placements (rva 0x5e0b80 + n * 0x70) and the flag the place lookup tests (0x5e0ccc)
			const float *q = (const float *)(base + 0x5e0b80 + n * 0x70);
			log_printf("aspectlog: placement %d: scale %g %g | full %g %g | real viewport %g %g %g %g | virt view %g %g %g %g | real view %g %g %g %g | last %g %g", n, q[0], q[1], q[2], q[3], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15], q[16], q[17], q[26], q[27]);
		}
		log_printf("aspectlog: place lookup flag (0x5e0ccc) = %d", *(int *)(base + 0x5e0ccc));
	}
	return 0;
}

void aspectlog_start(void)
{
	HANDLE t = CreateThread(NULL, 0, aspect_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
}

void loadprof_start(unsigned seconds)
{
	HANDLE t = CreateThread(NULL, 0, prof_thread, (LPVOID)(uintptr_t)seconds, 0, NULL);
	if (t)
		CloseHandle(t);
}
#else
void aspectlog_start(void)
{
}
void loadprof_start(unsigned seconds)
{
	(void)seconds;
}
#endif
