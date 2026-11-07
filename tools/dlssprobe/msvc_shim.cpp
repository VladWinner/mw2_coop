// The few Microsoft C++ runtime symbols NVIDIA's static NGX library (nvsdk_ngx_d.lib, built with MSVC) needs, so that it links with llvm-mingw. Names are MSVC-mangled through asm labels.
// Stack cookies are not checked, the thread-safe static guards are single-threaded-safe only enough for NGX's own start-up, out-of-memory throws are turned into a process exit.
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {

int _fltused = 0x9875;
uintptr_t __security_cookie = 0x2B992DDFA232ull;
void __fastcall __security_check_cookie(uintptr_t) {}

EXCEPTION_DISPOSITION __cdecl __GSHandlerCheck(EXCEPTION_RECORD *, void *, CONTEXT *, DISPATCHER_CONTEXT *)
{
	return ExceptionContinueSearch;
}
EXCEPTION_DISPOSITION __cdecl __GSHandlerCheck_EH(EXCEPTION_RECORD *, void *, CONTEXT *, DISPATCHER_CONTEXT *)
{
	return ExceptionContinueSearch;
}

// thread-safe function-local statics: the guard is 0 (not made), -1 (being made), or any other value (made)
int _Init_thread_epoch = 0;
void __cdecl _Init_thread_header(int *guard)
{
	if (*guard == 0)
		*guard = -1;
}
void __cdecl _Init_thread_footer(int *guard)
{
	*guard = 0x7fffffff;
}

static void __cdecl bad_alloc_exit(void)
{
	ExitProcess(0xE0000001);
}
static void __cdecl length_error_exit(const char *)
{
	ExitProcess(0xE0000002);
}
void(__cdecl *imp_Xbad_alloc)(void) __asm__("__imp_?_Xbad_alloc@std@@YAXXZ") = bad_alloc_exit;
void(__cdecl *imp_Xlength_error)(const char *) __asm__("__imp_?_Xlength_error@std@@YAXPEBD@Z") = length_error_exit;

void *__cdecl msvc_operator_new(size_t n) __asm__("??2@YAPEAX_K@Z");
void *__cdecl msvc_operator_new(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p)
		ExitProcess(0xE0000003);
	return p;
}
void __cdecl msvc_operator_delete(void *p) __asm__("??3@YAXPEAX@Z");
void __cdecl msvc_operator_delete(void *p)
{
	free(p);
}

} // extern "C"
