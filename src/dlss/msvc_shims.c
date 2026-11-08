// The 12 symbols NVIDIA's MSVC static library (nvsdk_ngx_s.lib) wants from the MSVC runtime, so it links into a MinGW build (llvm-mingw): the stack cookie, the thread-safe
// statics support, MSVC-mangled operator new / delete and two STL failure helpers. Our own code, MIT like the rest of the mod; it only provides what the library imports.
// (Found with a probe on 2026-11-08: with /nodefaultlib for the MSVC runtime these were the only unresolved symbols.)
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

// /GS stack cookie: a fixed value (the check only has to compare the same value)
uintptr_t __security_cookie = (uintptr_t)0x00002B992DDFA232ULL;
void __fastcall __security_check_cookie(uintptr_t cookie)
{
	if (cookie != __security_cookie)
		TerminateProcess(GetCurrentProcess(), 0xC0000409);
}
// SEH language-specific handlers named in the unwind data of /GS functions: nothing to handle, let the search go on (ExceptionContinueSearch = 1)
int __cdecl __GSHandlerCheck(void *a, void *b, void *c, void *d)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	return 1;
}
int __cdecl __GSHandlerCheck_EH(void *a, void *b, void *c, void *d)
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	return 1;
}
int _fltused = 0x9875;

// Thread-safe statics ("magic statics"): a guard is 0 = not initialised, -1 = in progress, otherwise the global epoch at the time it was initialised; a thread's own epoch tells it which
// guards it has seen initialised (the compiler's fast path: if (*guard > thread_epoch) _Init_thread_header(guard); if (*guard == -1) { init; _Init_thread_footer(guard); }).
// The epoch is thread-local: the library reaches it through the TLS index, so it must be a real thread-local variable (__thread).
static volatile LONG g_init_lock;
static volatile LONG g_global_epoch = INT_MIN;
__thread int _Init_thread_epoch = INT_MIN;
static void init_lock(void)
{
	while (InterlockedCompareExchange(&g_init_lock, 1, 0) != 0)
		Sleep(0);
}
static void init_unlock(void)
{
	InterlockedExchange(&g_init_lock, 0);
}
void __cdecl _Init_thread_header(int *once)
{
	for (;;) {
		init_lock();
		if (*once == 0) {
			*once = -1;
			init_unlock();
			return;
		}
		if (*once != -1) {
			_Init_thread_epoch = (int)g_global_epoch;
			init_unlock();
			return;
		}
		init_unlock();
		Sleep(1);
	}
}
void __cdecl _Init_thread_footer(int *once)
{
	init_lock();
	g_global_epoch++;
	*once = (int)g_global_epoch;
	_Init_thread_epoch = (int)g_global_epoch;
	init_unlock();
}

// MSVC-mangled operator new / delete and two STL failure helpers
void *__cdecl msvc_new(size_t n) __asm__("??2@YAPEAX_K@Z");
void *__cdecl msvc_new(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p)
		TerminateProcess(GetCurrentProcess(), 0xC0000017);
	return p;
}
void __cdecl msvc_delete(void *p) __asm__("??3@YAXPEAX@Z");
void __cdecl msvc_delete(void *p)
{
	free(p);
}
void __cdecl msvc_xbad_alloc(void) __asm__("?_Xbad_alloc@std@@YAXXZ");
void __cdecl msvc_xbad_alloc(void)
{
	TerminateProcess(GetCurrentProcess(), 0xC0000017);
}
void __cdecl msvc_xlength_error(const char *s) __asm__("?_Xlength_error@std@@YAXPEBD@Z");
void __cdecl msvc_xlength_error(const char *s)
{
	(void)s;
	TerminateProcess(GetCurrentProcess(), 0xC0000017);
}
