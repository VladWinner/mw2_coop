// Script runtime errors in the 32-bit build. Scr_Error (0x457ae0) keeps its message (copied into a 1 KB buffer at 0x190ec18,
// pointed to by 0x18e3f90) and jumps to the error handler 0x612f50, which longjmps out of the failing script thread (or ends
// the game) without logging it. The hook logs the message first, then runs the handler as it is. The call stack (file::function of the
// live code position and of the callers' saved ones) comes from scrdebug_stack.c, as in the 64-bit build (not in the iw4x-sp profile,
// whose VM addresses are not mapped).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "scrdebug.h"
#include "scrdebug_stack.h"

#ifdef MW2COOP_IW4X // the 1.0.159 exe of iw4x-sp
#define RVA_ERROR_HANDLER 0x215790
#define RVA_ERROR_MESSAGE 0x14e7510
#define RVA_ERROR_FLAG_A  0x14e750f
#define RVA_ERROR_FLAG_B  0x118bfb4
#define ERROR_FLAG_ADDR   {0x0f, 0x75, 0x8e, 0x01}
// the script VM in the 159 image (tools/map159.py, every data address agrees on all its references): function_count 0x190ddf8, the frame pointer 0x190ddfc (frames of 0x18 bytes),
// the live code position 0x190ddd4, the program buffer pointer 0x18e7560, Scr_GetFunctionHandle VA 0x462750 (the same bytes as the official function, it reads 0x18e7560), Scr_EndLoadScripts VA 0x45dc30
#define RVA_END_LOAD_SCRIPTS 0x5dc30
#define RVA_MT_SIZE          0x2127e0
#define RVA_SL_FREE          0x213cd0 // the last release of a script string: edi = its handle, [esp+4] the string, [esp+8] its length; the hash function is 0x2f0 before it
#define RVA_SL_TABLE         0x128f380 // the string hash table (8-byte entries: next index u16, handle at +4)
#define RVA_GET_FUNC_HANDLE  0x62750
#define RVA_PROGRAM_BUFFER   0x14e7560
#define RVA_VM_POS           0x150ddd4
#define RVA_VM_DEPTH         0x150ddf8
#define RVA_VM_FRAME_PTR     0x150ddfc
#define RVA_VM_STACK_TOP     0x150de00
#define RVA_VM_STACK_BASE_VA 0x190e110u
#define VM_FRAME_SIZE        0x18
#else
#define RVA_ERROR_HANDLER 0x212f50 // void (void); starts with cmp byte [0x18e3f8f], 0
#define RVA_ERROR_MESSAGE 0x14e3f90 // const char * (0x18e3f90)
#define RVA_ERROR_FLAG_A  0x14e3f8f // uint8
#define RVA_ERROR_FLAG_B  0x1188a34 // uint8 (0x1588a34)
#define ERROR_FLAG_ADDR   {0x8f, 0x3f, 0x8e, 0x01}
// the script VM (docs/32bit-native-map): function_count 0x190a878, the pointer one past the top call frame 0x190a87c (frames of 0x18 bytes, the saved code position first),
// the live code position 0x190a854, the program buffer pointer 0x18e3fe0, Scr_GetFunctionHandle(file, name) 0x5018f0 (cdecl), Scr_EndLoadScripts 0x490180
#define RVA_END_LOAD_SCRIPTS 0x90180
#define RVA_MT_SIZE          0x20ffa0 // MT_GetSize(esi = bytes) -> eax: the size class of a script memory request
#define RVA_SL_FREE          0x211490
#define RVA_SL_TABLE         0x128be00
#define RVA_GET_FUNC_HANDLE  0x1018f0
#define RVA_PROGRAM_BUFFER   0x14e3fe0
#define RVA_VM_POS           0x150a854
#define RVA_VM_DEPTH         0x150a878
#define RVA_VM_FRAME_PTR     0x150a87c
#define RVA_VM_STACK_TOP     0x150a900 // (not known for the official build: reads the frame pointer's neighbour, only for the log line)
#define RVA_VM_STACK_BASE_VA 0x190a000u
#define VM_FRAME_SIZE        0x18
#endif

static uint8_t *base;
static void (*handler_original)(void);

// A script that keeps failing raises the same error from the same place thousands of times a second (the official 32-bit build wrote 522,000 of them, 125 MB, in two minutes while
// a squad's thread read fields of a struct that was gone). As in the 64-bit build, per place (the message and the code position) the first three hits are logged, then the 10th, 100th,
// 1000th...
#define MAX_SITES 128
static uint32_t site_key[MAX_SITES];
static unsigned site_hits[MAX_SITES];
static int site_count;

static unsigned site_hit(const char *message, const void *pos)
{
	uint32_t key = (uint32_t)(uintptr_t)pos * 2654435761u;
	for (const char *c = message; c && *c && c < message + 400; c++)
		key = (key ^ (uint8_t)*c) * 16777619u;
	int i;
	for (i = 0; i < site_count && site_key[i] != key; i++)
		;
	if (i == site_count) {
		if (site_count >= MAX_SITES)
			return 0;
		site_key[site_count++] = key;
	}
	return ++site_hits[i];
}

static void error_hook(void)
{
	if (!base[RVA_ERROR_FLAG_A] && !base[RVA_ERROR_FLAG_B]) {
		const char *message = *(const char **)(base + RVA_ERROR_MESSAGE);
		MEMORY_BASIC_INFORMATION mbi;
		const int readable = message && VirtualQuery(message, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT;
		unsigned n = site_hit(readable ? message : "", *(const void *const *)(base + RVA_VM_POS));
		int show = n != 0 && n <= 3;
		for (unsigned p = 10; !show && n >= p && p < 1000000000u; p *= 10)
			show = n == p;
		if (n == 0)
			show = 1; // (more places than the table holds: no counting)
		if (show) {
			if (readable)
				log_printf("script error #%u: %.400s", n, message);
			else
				log_printf("script error #%u (no message)", n);
			scrdebug_stack_log();
		}
	}
	handler_original();
}

// Scr_EndLoadScripts (VA 0x490180; 159: VA 0x45dc30, the same first bytes) starts with  53 / 6a 02 / e8 rel32 (call 0x4b3150)  and is entered by a call (rva 0xf921) and by a tail jump (rva 0x171dd), so the function
// itself is patched: its first 8 bytes (up to the end of the call) are replaced by a jump to end_load_scripts, whose trampoline runs them (the call made absolute) and goes on at +8.
static void (*end_load_original)(void);

static void end_load_scripts(void)
{
	scrdebug_stack_build();
	end_load_original();
}

static void hook_end_load(void)
{
	uint8_t *target = base + RVA_END_LOAD_SCRIPTS;
	if (target[0] != 0x53 || target[1] != 0x6a || target[2] != 0x02 || target[3] != 0xe8) {
		log_printf("scrdebug: unexpected bytes at Scr_EndLoadScripts, no script stacks");
		return;
	}
	int32_t rel;
	memcpy(&rel, target + 4, 4);
	uintptr_t callee = (uintptr_t)(target + 8) + (intptr_t)rel;
	uintptr_t resume = (uintptr_t)(target + 8);
	uint8_t code[32];
	uint8_t *p = code;
	*p++ = 0x53; // push ebx
	*p++ = 0x6a; // push 2
	*p++ = 0x02;
	*p++ = 0xb8; // mov eax, callee; call eax
	memcpy(p, &callee, 4);
	p += 4;
	*p++ = 0xff;
	*p++ = 0xd0;
	*p++ = 0xb8; // mov eax, resume; jmp eax (eax is loaded from memory right after, at +8)
	memcpy(p, &resume, 4);
	p += 4;
	*p++ = 0xff;
	*p++ = 0xe0;
	end_load_original = (void (*)(void))hook_code(code, (size_t)(p - code));
	if (!end_load_original)
		return;
	hook_jump(target, 8, end_load_scripts, 0x90);
}

// The script memory (strings, vectors, the saved stacks of waiting threads) hands out blocks of at most 65535 bytes: MT_GetSize (ESI = the request) ends the game with "failed memory
// allocation for script usage" for more. This stub (entered with the function's registers as they are) logs every request of 16 KB or more with the script code position and the
// call stack, so the thread that asks for the huge block, or the one whose saved stack keeps growing, is named.
extern char mt_size_stub[];
void *mt_size_next; // (not static: the stub below names it)
void mt_size_log(unsigned bytes, const uintptr_t *entry_sp);
extern volatile DWORD mt_main_tid;
__asm__(
	".text\n"
	".globl _mt_size_stub\n"
	"_mt_size_stub:\n"
	"\tpushl %eax\n"
	"\tmovl %fs:0x24, %eax\n"
	"\tcmpl _mt_main_tid, %eax\n"
	"\tpopl %eax\n"
	"\tjne 2f\n"
	"\tcmpl $0x4000, %esi\n"
	"\tjb 1f\n"
	"2:\n"
	"\tpushfl\n"
	"\tpushal\n"
	"\tleal 36(%esp), %eax\n"
	"\tpushl %eax\n"
	"\tpushl %esi\n"
	"\tcall _mt_size_log\n"
	"\taddl $8, %esp\n"
	"\tpopal\n"
	"\tpopfl\n"
	"1:\n"
	"\tjmp *_mt_size_next\n");

volatile DWORD mt_main_tid; // the thread of the server frames (set by the native split screen code): the script VM runs there
void mt_size_log(unsigned bytes, const uintptr_t *entry_sp)
{
	const uintptr_t caller = entry_sp[0];
	static volatile LONG logged, other;
	const DWORD tid = GetCurrentThreadId();
	if (mt_main_tid && tid != mt_main_tid) {
		// the script memory is used by a thread that is not the script thread: its allocator is not safe for that unless it is locked
		static DWORD seen[8];
		static int seen_n;
		for (int k = 0; k < seen_n; k++)
			if (seen[k] == tid)
				return;
		if (seen_n < 8) {
			seen[seen_n++] = tid;
			log_printf("script memory: thread %lu (script thread %lu) asks for %u bytes from rva 0x%x", (unsigned long)tid, (unsigned long)mt_main_tid, bytes, (unsigned)(caller - (uintptr_t)base));
		}
		return;
	}
	if (bytes < 0x4000 || !mt_main_tid || InterlockedIncrement(&logged) > 80)
		return;
	log_printf("script memory: request of %d bytes (%u as unsigned; the limit is 65535) from rva 0x%x, VM stack top index %d, call depth %d, GSC position:", (int)bytes, bytes, (unsigned)(caller - (uintptr_t)base),
		   (int)((*(const uint32_t *)(base + RVA_VM_STACK_TOP) - RVA_VM_STACK_BASE_VA) / 8), *(const int *)(base + RVA_VM_DEPTH));
	{ // the native callers: return addresses (a call right before them) among the next 80 stack words
		char chain[400];
		int n = 0;
		for (int k = 1; k < 80 && n < (int)sizeof(chain) - 12; k++) {
			const uintptr_t v = entry_sp[k];
			if (v > (uintptr_t)base + 0x1000 && v < (uintptr_t)base + 0x2a00000 && !IsBadReadPtr((void *)(v - 5), 5) && ((const uint8_t *)v)[-5] == 0xe8)
				n += snprintf(chain + n, sizeof(chain) - n, " %x", (unsigned)(v - (uintptr_t)base));
		}
		chain[n] = 0;
		log_printf("script memory: native callers (rva):%s", chain);
	}
	scrdebug_stack_log();
	(void)other;
}

// The last release of a script string (edi = its handle, [esp+4] the string, [esp+8] its length) unlinks the string from its hash chain by walking the chain until the handle is
// found. A string that is released twice is no longer in its chain, the walk never ends (the chain's last entry points at itself) and the game freezes. This stub checks the chain
// first: a string that is not in it is logged, with the script position and the native callers, and the second release is skipped (the first one already freed it).
extern char sl_free_stub[];
void *sl_free_next;
volatile int sl_free_ok;
int sl_free_check(unsigned handle, const uint8_t *str, int len, const uintptr_t *entry_sp);
__asm__(
	".text\n"
	".globl _sl_free_stub\n"
	"_sl_free_stub:\n"
	"\tpushfl\n"
	"\tpushal\n"
	"\tleal 36(%esp), %eax\n"
	"\tpushl %eax\n"
	"\tpushl 48(%esp)\n"
	"\tpushl 48(%esp)\n"
	"\tpushl 12(%esp)\n"
	"\tcall _sl_free_check\n"
	"\taddl $16, %esp\n"
	"\tmovl %eax, _sl_free_ok\n"
	"\tpopal\n"
	"\tpopfl\n"
	"\tcmpl $0, _sl_free_ok\n"
	"\tje 1f\n"
	"\tjmp *_sl_free_next\n"
	"1:\n"
	"\tret\n");

int sl_free_check(unsigned handle, const uint8_t *str, int len, const uintptr_t *entry_sp)
{
	static volatile LONG reported;
	if (*(const uint16_t *)str != 0) // (more references: the function does nothing)
		return 1;
	void *hash_fn = base + RVA_SL_FREE - 0x2f0;
	const uint8_t *tbl = base + RVA_SL_TABLE;
	unsigned bucket = (unsigned)len; // (in: the length, out: the bucket)
	const uint8_t *text = str + 4;
	__asm__ volatile("call *%2" : "+a"(bucket), "+d"(text) : "r"(hash_fn) : "ecx", "ebx", "esi", "memory", "cc");
	if (bucket == 0 || bucket > 0x6fff)
		return 1; // (not what was expected of the hash: leave it to the game)
	const uint8_t *e = tbl + (size_t)bucket * 8;
	int found = *(const uint32_t *)(e + 4) == handle;
	unsigned prev = bucket, idx = *(const uint16_t *)e;
	for (int k = 0; !found && k < 4096 && idx != prev && idx < 0x7000; k++) {
		const uint8_t *n = tbl + (size_t)idx * 8;
		if (*(const uint32_t *)(n + 4) == handle) {
			found = 1;
			break;
		}
		prev = idx;
		idx = *(const uint16_t *)n;
	}
	if (found)
		return 1;
	if (InterlockedIncrement(&reported) <= 12) {
		char chain[300];
		int n = 0;
		for (int k = 0; k < 80 && n < (int)sizeof(chain) - 12; k++) {
			const uintptr_t v = entry_sp[k];
			if (v > (uintptr_t)base + 0x1000 && v < (uintptr_t)base + 0x2a00000 && !IsBadReadPtr((void *)(v - 5), 5) && ((const uint8_t *)v)[-5] == 0xe8)
				n += snprintf(chain + n, sizeof(chain) - n, " %x", (unsigned)(v - (uintptr_t)base));
		}
		chain[n] = 0;
		log_printf("script strings: string %p (handle 0x%x, %d bytes, text '%.24s') is released but is not in its hash chain (released twice, or changed after it was made); the release is skipped. Native callers (rva):%s",
			   str, handle, len, (const char *)(str + 4), chain);
		scrdebug_stack_log();
	}
	return 0;
}

#ifdef MW2COOP_IW4X
// Diagnostics (a file mw2coop\\xferlog next to the exe): the animation transfer queue (159 image: entries of 36 bytes at VA 0x1a4ade0, their count at VA 0x1a53de4). A DObj that is
// freed saves its animation state into it (VA 0x486930, the tree in [esp+8]); VA 0x42b260 applies every entry (and gives back the strings the entries hold) at the start of VA 0x4bbe50;
// VA 0x4dfe40 empties it. A string that is given back twice is what the string guard catches, so the order of these three calls with the live context is logged.
extern int native_log_ctx(void);
static volatile LONG xfer_events;
static DWORD xfer_t0;
static volatile LONG xfer_saves; // the saves since the last other event (a block of hundreds is logged as one line)
static volatile ULONG_PTR xfer_save_caller, xfer_save_ctx;
static void xfer_note(const char *what, unsigned a, uintptr_t caller)
{
	const int ctx = native_log_ctx();
	if (GetTickCount() - xfer_t0 < 45000)
		return; // (the level load saves hundreds of trees; the split screen starts after it)
	if (!strcmp(what, "save")) {
		if (!xfer_saves++) {
			xfer_save_caller = caller;
			xfer_save_ctx = (ULONG_PTR)ctx;
		}
		return;
	}
	LONG n = InterlockedIncrement(&xfer_events);
	if (n < 4000) {
		LONG saves = InterlockedExchange(&xfer_saves, 0);
		log_printf("xfer #%ld ctx %d: %s from rva %x (queue count %u); %ld saves before it (first from rva %x in ctx %d)", (long)n, ctx, what, (unsigned)(caller - (uintptr_t)base),
			   *(const unsigned *)(base + 0x1a53de4 - 0x400000), (long)saves, (unsigned)(xfer_save_caller - (uintptr_t)base), (int)xfer_save_ctx);
	}
}
static void (*xfer_apply_next)(void);
static void (*xfer_reset_next)(void);
void *xfer_save_tramp;
extern char xfer_save_hook[];
void xfer_save_log(unsigned tree, uintptr_t caller)
{
	xfer_note("save", tree, caller);
}
static void xfer_apply_hook(void)
{
	xfer_note("apply begin", 0, (uintptr_t)__builtin_return_address(0));
	xfer_apply_next();
	xfer_note("apply end", 0, (uintptr_t)__builtin_return_address(0));
}
static void xfer_reset_hook(void)
{
	xfer_note("reset", 0, (uintptr_t)__builtin_return_address(0));
	xfer_reset_next();
}
// save: the tree is the function's argument at [esp+4] of its entry; its first instruction reads a global, so a stub that keeps every register is used
__asm__(
	".text\n"
	".globl _xfer_save_hook\n"
	"_xfer_save_hook:\n"
	"\tpushfl\n"
	"\tpushal\n"
	"\tpushl 36(%esp)\n"
	"\tpushl 48(%esp)\n"
	"\tcall _xfer_save_log\n"
	"\taddl $8, %esp\n"
	"\tpopal\n"
	"\tpopfl\n"
	"\tjmp *_xfer_save_tramp\n");

static void xfer_install(void)
{
	if (!game_qa_flag("xferlog"))
		return;
	xfer_t0 = GetTickCount();
	static const uint8_t save_bytes[] = {0xa1, 0xe4, 0x3d, 0xa5, 0x01}; // mov eax, [0x1a53de4]
	static const uint8_t apply_bytes[] = {0x56, 0x33, 0xf6, 0x39, 0x35, 0xe4, 0x3d, 0xa5, 0x01}; // push esi / xor esi, esi / cmp [0x1a53de4], esi
	static const uint8_t reset_bytes[] = {0x33, 0xc0, 0xa3, 0xe0, 0x3d, 0xa5, 0x01}; // xor eax, eax / mov [0x1a53de0], eax
	int ok = hook_function(base + 0x486930 - 0x400000, save_bytes, sizeof(save_bytes), xfer_save_hook, &xfer_save_tramp) &&
		 hook_function(base + 0x42b260 - 0x400000, apply_bytes, sizeof(apply_bytes), xfer_apply_hook, (void **)&xfer_apply_next) &&
		 hook_function(base + 0x4dfe40 - 0x400000, reset_bytes, sizeof(reset_bytes), xfer_reset_hook, (void **)&xfer_reset_next);
	log_printf("scrdebug: animation transfer queue events are logged: %s", ok ? "on" : "bytes did not match");
}
#endif

void scrdebug_install(void)
{
	static const uint8_t flag_addr[4] = ERROR_FLAG_ADDR;
	const uint8_t expected[] = {0x80, 0x3d, flag_addr[0], flag_addr[1], flag_addr[2], flag_addr[3], 0x00};
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!hook_function(base + RVA_ERROR_HANDLER, expected, sizeof(expected), error_hook, (void **)&handler_original)) {
		log_printf("scrdebug: unexpected bytes at the script error handler, script errors are not logged");
		return;
	}
	log_printf("scrdebug: script errors are logged");
#ifdef MW2COOP_IW4X
	xfer_install();
#endif
	{
		static const uint8_t mt_bytes[] = {0x51, 0x81, 0xfe, 0x00, 0x00, 0x01, 0x00}; // push ecx / cmp esi, 0x10000 (the next instruction, jge, stays whole)
		if (hook_function(base + RVA_MT_SIZE, mt_bytes, sizeof(mt_bytes), mt_size_stub, &mt_size_next))
			log_printf("scrdebug: script memory requests of 16 KB and more are logged");
		else
			log_printf("scrdebug: unexpected bytes at MT_GetSize, big script memory requests are not logged");
	}
	{
		static const uint8_t sl_bytes[] = {0x8b, 0x44, 0x24, 0x08, 0x53}; // mov eax, [esp+8] / push ebx
		if (hook_function(base + RVA_SL_FREE, sl_bytes, sizeof(sl_bytes), sl_free_stub, &sl_free_next))
			log_printf("scrdebug: releases of strings that are not in the string table are caught");
		else
			log_printf("scrdebug: unexpected bytes at the string release, not caught");
	}
	static const scrdebug_vm_t vm = {RVA_GET_FUNC_HANDLE, RVA_PROGRAM_BUFFER, RVA_VM_POS, RVA_VM_DEPTH, 0, RVA_VM_FRAME_PTR, VM_FRAME_SIZE};
	static const uint8_t handle_bytes[] = {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x08, 0x56, 0x57, 0x50};
	if (memcmp(base + RVA_GET_FUNC_HANDLE, handle_bytes, sizeof(handle_bytes)) == 0) {
		scrdebug_stack_init(base, &vm);
		hook_end_load();
	} else {
		log_printf("scrdebug: unexpected bytes at Scr_GetFunctionHandle, no script stacks");
	}
}
