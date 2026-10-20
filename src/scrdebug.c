// Script runtime errors. The retail Scr_Error (rva 0x2226a0) drops the message and only
// longjmps out of the failing script thread:
//   if (!flag_a && !flag_b && depth) longjmp(&jmpbufs[index], -1);
// The message was formatted with va() just before, into one of two 1 KB slots of a
// thread-local block (va @0x273440 -> block = 0x203700(1); slot index at block+0x800).
// Our replacement logs both slots, then does exactly what the original does, with the game's
// own longjmp (rva 0x2e485c) on the game's own jmp_buf.
//
// It also logs where in GSC the error happened. The VM keeps its live code position at
// rva 0x1900b78 and the callers' saved positions in function_frame_start[] (rva 0x18f44c0,
// 0x28 bytes each, pos first; function_count at 0x18f44a0). Positions point into the program
// buffer (rva 0x15a03e0). Scr_GetFunctionHandle (rva 0x216920) returns a function's offset in
// that buffer, so the function names taken from each script's source text (scrdebug_note_source)
// map a position back to file::function. Functions are compiled one after another, so a position
// belongs to the function with the greatest start offset at or below it.
// Scr_GetFunctionHandle only works while the loaded-scripts table exists, so the offsets are
// read in a hook on Scr_EndLoadScripts (rva 0x2166d0), right before it frees that table.
#include <windows.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "scrdebug.h"

#define RVA_SCR_ERROR        0x2226a0
#define RVA_SCR_ERR_FLAG_A   0x15a038b // uint8
#define RVA_SCR_ERR_FLAG_B   0x1463bce // uint8
#define RVA_SCR_ERR_DEPTH    0x18f44a0 // int32, scrVmPub.function_count
#define RVA_SCR_JMPBUF_INDEX 0x18fca44 // int32
#define RVA_SCR_JMPBUFS      0x18fca50 // jmp_buf[], 0x100 bytes each
#define RVA_LONGJMP          0x2e485c  // the game's CRT longjmp
#define RVA_TLS_BLOCK        0x203700  // void *Sys_GetValue(int)
#define RVA_VM_POS           0x1900b78 // const char *, live code position of the running thread
#define RVA_VM_FRAMES        0x18f44c0 // function_frame_t[], saved code position at +0
#define VM_FRAME_SIZE        0x28
#define RVA_PROGRAM_BUFFER   0x15a03e0 // const char *
#define RVA_GET_FUNC_HANDLE  0x216920  // int Scr_GetFunctionHandle(const char *file, const char *name)
#define RVA_END_LOAD_SCRIPTS 0x2166d0  // void Scr_EndLoadScripts(void)
#define RVA_END_LOAD_CALLEE  0x21a3b0  // its first call

static uint8_t *base;

// Function names per script file, from the source text the game compiled.
#define MAX_FILES 512
static struct {
	char *name;  // "maps/_coop", as Scr_GetFunctionHandle wants it
	char **funcs;
	int count;
} files[MAX_FILES];
static int file_count;

// Built at the end of each script load: start offset -> file/function.
static struct func_start {
	uint32_t off;
	uint16_t file;
	uint16_t func;
} *starts;
static int start_count;

static int is_ident(int c)
{
	return isalnum(c) || c == '_';
}

void scrdebug_note_source(const char *ext, const char *text, int len)
{
	char name[MAX_PATH];
	lstrcpynA(name, ext, sizeof(name));
	int n = lstrlenA(name);
	if (n > 4 && !lstrcmpiA(name + n - 4, ".gsc"))
		name[n - 4] = 0;
	int f;
	for (f = 0; f < file_count && lstrcmpiA(files[f].name, name); f++)
		;
	if (f == file_count) {
		if (file_count >= MAX_FILES)
			return;
		files[f].name = _strdup(name);
		file_count++;
	}
	for (int i = 0; i < files[f].count; i++)
		free(files[f].funcs[i]);
	files[f].count = 0;
	int cap = 0;

	// Top-level "name(" is a function definition: GSC has nothing else at brace depth 0 but
	// #include and #using_animtree lines.
	int depth = 0;
	for (int i = 0; i < len && text[i]; i++) {
		char c = text[i];
		if (c == '/' && i + 1 < len && text[i + 1] == '/') {
			while (i < len && text[i] && text[i] != '\n')
				i++;
		} else if (c == '/' && i + 1 < len && text[i + 1] == '*') {
			for (i += 2; i + 1 < len && text[i] && !(text[i] == '*' && text[i + 1] == '/'); i++)
				;
			i++;
		} else if (c == '"') {
			for (i++; i < len && text[i] && text[i] != '"'; i++)
				if (text[i] == '\\')
					i++;
		} else if (c == '{') {
			depth++;
		} else if (c == '}') {
			depth--;
		} else if (depth == 0 && c == '#') {
			while (i < len && text[i] && text[i] != '\n')
				i++;
		} else if (depth == 0 && (isalpha((unsigned char)c) || c == '_') && (i == 0 || !is_ident((unsigned char)text[i - 1]))) {
			int s = i;
			while (i < len && is_ident((unsigned char)text[i]))
				i++;
			int e = i;
			while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n'))
				i++;
			if (i < len && text[i] == '(') {
				if (files[f].count == cap) {
					cap = cap ? cap * 2 : 64;
					files[f].funcs = realloc(files[f].funcs, cap * sizeof(char *));
				}
				char *fn = malloc(e - s + 1);
				for (int k = 0; k < e - s; k++)
					fn[k] = (char)tolower((unsigned char)text[s + k]);
				fn[e - s] = 0;
				files[f].funcs[files[f].count++] = fn;
			}
			i--;
		}
	}
}

static int cmp_start(const void *a, const void *b)
{
	uint32_t x = ((const struct func_start *)a)->off, y = ((const struct func_start *)b)->off;
	return x < y ? -1 : x > y;
}

static void build_starts(void)
{
	typedef int (*get_handle_t)(const char *file, const char *name);
	get_handle_t get_handle = (get_handle_t)(base + RVA_GET_FUNC_HANDLE);
	int total = 0;
	for (int f = 0; f < file_count; f++)
		total += files[f].count;
	free(starts);
	starts = malloc((total ? total : 1) * sizeof(*starts));
	start_count = 0;
	for (int f = 0; f < file_count; f++)
		for (int i = 0; i < files[f].count; i++) {
			int off = get_handle(files[f].name, files[f].funcs[i]);
			if (off > 0)
				starts[start_count++] = (struct func_start){(uint32_t)off, (uint16_t)f, (uint16_t)i};
		}
	qsort(starts, start_count, sizeof(*starts), cmp_start);
	log_printf("scrdebug: %d script functions mapped", start_count);
}

static void describe_pos(const char *what, const char *pos)
{
	const char *program = *(const char **)(base + RVA_PROGRAM_BUFFER);
	if (!pos || !program || pos < program) {
		log_printf("    %s: (no position)", what);
		return;
	}
	uint32_t off = (uint32_t)(pos - program);
	int lo = 0, hi = start_count - 1, best = -1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (starts[mid].off <= off) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	if (best < 0) {
		log_printf("    %s: program+0x%x", what, off);
		return;
	}
	log_printf("    %s: %s::%s +0x%x", what, files[starts[best].file].name,
		   files[starts[best].file].funcs[starts[best].func], off - starts[best].off);
}

static void (*end_load_original)(void);

static void end_load_scripts(void)
{
	build_starts();
	end_load_original();
}

// Scr_EndLoadScripts starts with (16 bytes, cut on an instruction boundary):
//   40 53 / 48 83 ec 20 / e8 rel32 (call 0x21a3b0) / b9 02 00 00 00
// The trampoline runs them with the call made absolute, then jumps back to +16.
static const uint8_t end_load_expected[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0xe8, 0xd5, 0x3c, 0x00, 0x00,
					    0xb9, 0x02, 0x00, 0x00, 0x00};

static void hook_end_load(void)
{
	uint8_t *target = base + RVA_END_LOAD_SCRIPTS;
	if (memcmp(target, end_load_expected, sizeof(end_load_expected)) != 0) {
		log_printf("scrdebug: unexpected bytes at Scr_EndLoadScripts, no script stacks");
		return;
	}
	uint8_t *t = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!t)
		return;
	uint8_t *p = t;
	memcpy(p, target, 6); // push rbx; sub rsp, 0x20
	p += 6;
	uint64_t callee = (uint64_t)(uintptr_t)(base + RVA_END_LOAD_CALLEE);
	*p++ = 0x48; // mov rax, imm64; call rax
	*p++ = 0xb8;
	memcpy(p, &callee, 8);
	p += 8;
	*p++ = 0xff;
	*p++ = 0xd0;
	memcpy(p, target + 11, 5); // mov ecx, 2
	p += 5;
	uint64_t back = (uint64_t)(uintptr_t)(target + 16);
	uint8_t jmp_back[6] = {0xff, 0x25, 0, 0, 0, 0};
	memcpy(p, jmp_back, 6);
	memcpy(p + 6, &back, 8);
	FlushInstructionCache(GetCurrentProcess(), t, 64);
	end_load_original = (void (*)(void))t;

	uint8_t jmp[16] = {0xff, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x90, 0x90};
	uint64_t to = (uint64_t)(uintptr_t)end_load_scripts;
	memcpy(jmp + 6, &to, 8);
	DWORD old;
	VirtualProtect(target, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, jmp, sizeof(jmp));
	VirtualProtect(target, sizeof(jmp), old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, sizeof(jmp));
}

static void log_stack(void)
{
	describe_pos("at", *(const char **)(base + RVA_VM_POS));
	int count = *(const int32_t *)(base + RVA_SCR_ERR_DEPTH);
	for (int i = count - 1, shown = 0; i >= 0 && shown < 8; i--, shown++)
		describe_pos("from", *(const char **)(base + RVA_VM_FRAMES + (int64_t)i * VM_FRAME_SIZE));
}

// Per calling site (return address = the builtin or VM code that raised the error), log the
// first few hits and then every 100th, so a noisy site does not drown the rest.
#define MAX_SITES 64
static uintptr_t site_rva[MAX_SITES];
static volatile LONG site_hits[MAX_SITES];
static volatile LONG site_count;

static void log_error(uintptr_t rva)
{
	int i;
	for (i = 0; i < site_count && site_rva[i] != rva; i++)
		;
	if (i == site_count) {
		if (site_count >= MAX_SITES)
			return;
		site_rva[i] = rva;
		InterlockedIncrement(&site_count);
	}
	LONG n = InterlockedIncrement(&site_hits[i]);
	if (n > 3 && n % 100 != 0)
		return;
	const char *block = ((const char *(*)(int))(base + RVA_TLS_BLOCK))(1);
	int slot = *(const int32_t *)(block + 0x800);
	char last[160], prev[160];
	lstrcpynA(last, block + (slot ^ 1) * 0x400, sizeof(last));
	lstrcpynA(prev, block + slot * 0x400, sizeof(prev));
	log_printf("SCRIPT ERROR #%ld from rva 0x%llx (va slots: '%s' / '%s')", n, (unsigned long long)rva, last, prev);
	log_stack();
}

static void scr_error(void)
{
	if (base[RVA_SCR_ERR_FLAG_A] || base[RVA_SCR_ERR_FLAG_B] || !*(int32_t *)(base + RVA_SCR_ERR_DEPTH))
		return;
	log_error((uintptr_t)__builtin_return_address(0) - (uintptr_t)base);
	int index = *(int32_t *)(base + RVA_SCR_JMPBUF_INDEX);
	((void (*)(void *, int))(base + RVA_LONGJMP))(base + RVA_SCR_JMPBUFS + (int64_t)index * 0x100, -1);
}

static const uint8_t expected[] = {0x48, 0x83, 0xec, 0x28, 0x80, 0x3d, 0xe0, 0xdc, 0x37, 0x01, 0x00};

void scrdebug_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *target = base + RVA_SCR_ERROR;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("scrdebug: unexpected bytes at Scr_Error, not hooking");
		return;
	}
	uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t to = (uint64_t)(uintptr_t)scr_error;
	memcpy(jmp + 6, &to, 8);
	DWORD old;
	VirtualProtect(target, sizeof(jmp), PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, jmp, sizeof(jmp));
	VirtualProtect(target, sizeof(jmp), old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, sizeof(jmp));
	log_printf("scrdebug: Scr_Error replaced (runtime script errors are logged)");
	hook_end_load();
}

// Method tracing (QA): "!trace <rva>" in commands.txt points a builtin method table entry
// ({const char *name; void (*func)(scr_entref_t); int developer}, e.g. unlink at rva 0x342c88)
// at a wrapper that logs the entity and the GSC call stack, then calls the original. Scripts
// compiled after that call the wrapper.
#define TRACE_SLOTS 4
#define TRACE_MAX_HITS 40
typedef void (*method_t)(uintptr_t entref);
static method_t trace_orig[TRACE_SLOTS];
static const char *trace_name[TRACE_SLOTS];
static volatile LONG trace_hits[TRACE_SLOTS];
static int trace_count;

static void trace_log(int i, uintptr_t entref)
{
	LONG n = InterlockedIncrement(&trace_hits[i]);
	if (n > TRACE_MAX_HITS)
		return;
	log_printf("TRACE %s #%ld on entity %u (class %u)", trace_name[i], n, (unsigned)(entref & 0xffff),
		   (unsigned)((entref >> 16) & 0xffff));
	log_stack();
}

#define TRACE_WRAP(i)                                    \
	static void trace_wrap##i(uintptr_t entref)      \
	{                                                \
		trace_log(i, entref);                    \
		trace_orig[i](entref);                   \
	}
TRACE_WRAP(0)
TRACE_WRAP(1)
TRACE_WRAP(2)
TRACE_WRAP(3)
static const method_t trace_wraps[TRACE_SLOTS] = {trace_wrap0, trace_wrap1, trace_wrap2, trace_wrap3};

void scrdebug_trace_method(uintptr_t entry_rva)
{
	if (trace_count >= TRACE_SLOTS) {
		log_printf("trace: no free slot");
		return;
	}
	uint8_t *entry = base + entry_rva;
	const char *name = *(const char **)entry;
	method_t *func = (method_t *)(entry + 8);
	if ((uint8_t *)name < base || (uint8_t *)*func < base) {
		log_printf("trace: rva 0x%llx is not a method table entry", (unsigned long long)entry_rva);
		return;
	}
	int i = trace_count++;
	trace_name[i] = name;
	trace_orig[i] = *func;
	DWORD old;
	VirtualProtect(func, sizeof(*func), PAGE_READWRITE, &old);
	*func = trace_wraps[i];
	VirtualProtect(func, sizeof(*func), old, &old);
	log_printf("trace: method %s (rva 0x%llx) is traced", name, (unsigned long long)((uint8_t *)trace_orig[i] - base));
}
