// Crash locations for QA: a vectored exception handler logs access violations and other fatal
// exceptions with the faulting instruction as an RVA of iw4sp.exe and the return addresses
// into the exe found on the stack, then lets the exception go on as before (the game's own
// handlers or Windows still decide what happens).
#include <windows.h>
#include <stdint.h>
#include <tlhelp32.h>

#include "crash.h"
#include "log.h"

// the registers of the exception context: 64 bits Rip/Rsp..., 32 bits Eip/Esp...
#ifdef _WIN64
#define CTX_IP(c) ((c)->Rip)
#define CTX_SP(c) ((c)->Rsp)
#else
#define CTX_IP(c) ((c)->Eip)
#define CTX_SP(c) ((c)->Esp)
#endif

static uintptr_t image_base, image_end;
static volatile LONG logged;

static int in_image(uintptr_t p)
{
	return p >= image_base && p < image_end;
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *info)
{
	DWORD code = info->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
	    code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW)
		return EXCEPTION_CONTINUE_SEARCH;
	if (InterlockedIncrement(&logged) > 3)
		return EXCEPTION_CONTINUE_SEARCH;
	CONTEXT *c = info->ContextRecord;
	uintptr_t rip = (uintptr_t)CTX_IP(c);
	ULONG_PTR *ei = info->ExceptionRecord->ExceptionInformation;
	if (code == EXCEPTION_ACCESS_VIOLATION)
		log_printf("CRASH: access violation (%s 0x%llx) at %s 0x%llx", ei[0] == 1 ? "write" : ei[0] == 8 ? "execute" : "read",
			   (unsigned long long)ei[1], in_image(rip) ? "rva" : "address",
			   (unsigned long long)(in_image(rip) ? rip - image_base : rip));
	else
		log_printf("CRASH: exception 0x%lx at %s 0x%llx", code, in_image(rip) ? "rva" : "address",
			   (unsigned long long)(in_image(rip) ? rip - image_base : rip));
#ifdef _WIN64
	log_printf("CRASH: rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx r8 %llx r9 %llx",
		   c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->R8, c->R9);
#else
	log_printf("CRASH: eax %lx ebx %lx ecx %lx edx %lx esi %lx edi %lx ebp %lx", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi,
		   c->Ebp);
#endif
	// return addresses into the exe among the first stack slots (a rough call stack)
	const uintptr_t *sp = (const uintptr_t *)CTX_SP(c);
	char line[512];
	int n = 0, found = 0;
	n += wsprintfA(line, "CRASH: stack rvas:");
	for (int i = 0; i < 512 && found < 16; i++) {
		MEMORY_BASIC_INFORMATION mbi;
		if (i % 64 == 0 && (!VirtualQuery(sp + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT))
			break;
		uintptr_t v = sp[i];
		if (in_image(v) && n < (int)sizeof(line) - 20) {
			n += wsprintfA(line + n, " %lx", (unsigned long)(v - image_base));
			found++;
		}
	}
	log_printf("%s", line);
	return EXCEPTION_CONTINUE_SEARCH;
}

// Return addresses into the exe among the first stack slots from sp (a rough call stack).
static void stack_rvas(HANDLE out, const char *what, uintptr_t rip, const uintptr_t *sp, int slots)
{
	char line[512];
	int n = wsprintfA(line, "%s at %s %lx, stack rvas:", what, in_image(rip) ? "rva" : "address",
			  (unsigned long)(in_image(rip) ? rip - image_base : rip));
	int found = 0;
	for (int i = 0; i < slots && found < 24; i++) {
		uintptr_t v = sp[i];
		if (in_image(v) && n < (int)sizeof(line) - 20) {
			n += wsprintfA(line + n, " %lx", (unsigned long)(v - image_base));
			found++;
		}
	}
	lstrcatA(line, "\r\n");
	DWORD wrote;
	WriteFile(out, line, lstrlenA(line), &wrote, NULL);
}

// QA, for a hang: every other thread of the game, where it is and what called it, into
// <game dir>\mw2coop\stacks.txt. A thread of its own runs it when <game dir>\mw2coop\dumpstacks
// appears (and deletes that file); it takes no lock of the game's or the log's, which a hung game
// may hold (the control thread stopped with it, 2026-10-24).
static void dump_threads(HANDLE out)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;
	THREADENTRY32 te = {.dwSize = sizeof(te)};
	DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
			continue;
		HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
		if (!t)
			continue;
		// copy what is needed while it is suspended, write only after it runs again (it may hold a
		// lock the writing needs)
		static uintptr_t stack[1024];
		uintptr_t rip = 0;
		int got = 0;
		if (SuspendThread(t) != (DWORD)-1) {
			CONTEXT c = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
			if (GetThreadContext(t, &c)) {
				rip = (uintptr_t)CTX_IP(&c);
				SIZE_T read = 0;
				ReadProcessMemory(GetCurrentProcess(), (void *)CTX_SP(&c), stack, sizeof(stack), &read);
				if (!read) // the stack's end is near: read what is there
					for (size_t want = sizeof(stack) / 2; want >= 64 && !read; want /= 2)
						ReadProcessMemory(GetCurrentProcess(), (void *)CTX_SP(&c), stack, want, &read);
				got = (int)(read / sizeof(uintptr_t));
			}
			ResumeThread(t);
		}
		if (rip) {
			char what[64];
			wsprintfA(what, "stacks: thread %lu", te.th32ThreadID);
			stack_rvas(out, what, rip, stack, got);
		}
		CloseHandle(t);
	}
	CloseHandle(snap);
}

static char flag_path[MAX_PATH], out_path[MAX_PATH];

static DWORD WINAPI watchdog(LPVOID arg)
{
	(void)arg;
	for (;;) {
		Sleep(500);
		if (!DeleteFileA(flag_path))
			continue;
		HANDLE out = CreateFileA(out_path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		if (out == INVALID_HANDLE_VALUE)
			continue;
		dump_threads(out);
		CloseHandle(out);
	}
	return 0;
}

void crash_install(void)
{
	HMODULE exe = GetModuleHandleA(NULL);
#ifdef _WIN64
	IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((uint8_t *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
#else
	IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)((uint8_t *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
#endif
	image_base = (uintptr_t)exe;
	image_end = image_base + nt->OptionalHeader.SizeOfImage;
	AddVectoredExceptionHandler(1, on_exception);
	DWORD n = GetModuleFileNameA(NULL, flag_path, MAX_PATH);
	while (n > 0 && flag_path[n - 1] != '\\')
		n--;
	flag_path[n] = 0;
	lstrcpynA(out_path, flag_path, MAX_PATH);
	lstrcatA(flag_path, "mw2coop\\dumpstacks");
	lstrcatA(out_path, "mw2coop\\stacks.txt");
	CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
	log_printf("crash: exception logger installed");
}
