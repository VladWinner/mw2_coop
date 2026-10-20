// Crash locations for QA: a vectored exception handler logs access violations and other fatal
// exceptions with the faulting instruction as an RVA of iw4sp.exe and the return addresses
// into the exe found on the stack, then lets the exception go on as before (the game's own
// handlers or Windows still decide what happens).
#include <windows.h>
#include <stdint.h>

#include "crash.h"
#include "log.h"

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
	uintptr_t rip = (uintptr_t)c->Rip;
	ULONG_PTR *ei = info->ExceptionRecord->ExceptionInformation;
	if (code == EXCEPTION_ACCESS_VIOLATION)
		log_printf("CRASH: access violation (%s 0x%llx) at %s 0x%llx", ei[0] == 1 ? "write" : ei[0] == 8 ? "execute" : "read",
			   (unsigned long long)ei[1], in_image(rip) ? "rva" : "address",
			   (unsigned long long)(in_image(rip) ? rip - image_base : rip));
	else
		log_printf("CRASH: exception 0x%lx at %s 0x%llx", code, in_image(rip) ? "rva" : "address",
			   (unsigned long long)(in_image(rip) ? rip - image_base : rip));
	log_printf("CRASH: rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx r8 %llx r9 %llx",
		   c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->R8, c->R9);
	// return addresses into the exe among the first stack slots (a rough call stack)
	const uintptr_t *sp = (const uintptr_t *)c->Rsp;
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

void crash_install(void)
{
	HMODULE exe = GetModuleHandleA(NULL);
	IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((uint8_t *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
	image_base = (uintptr_t)exe;
	image_end = image_base + nt->OptionalHeader.SizeOfImage;
	AddVectoredExceptionHandler(1, on_exception);
	log_printf("crash: exception logger installed");
}
