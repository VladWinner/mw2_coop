#include <windows.h>
#include <stdio.h>

#include "game.h"
#include "log.h"

static uint8_t *base;
static int ready;

int game_init(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
	uint32_t stamp = nt->FileHeader.TimeDateStamp;
	ready = stamp == IW4SP_TIMESTAMP;
	log_printf("game: exe timestamp 0x%08x, expected 0x%08x -> %s", stamp, IW4SP_TIMESTAMP,
		   ready ? "known build" : "UNKNOWN BUILD, game calls disabled");
	return ready;
}

void game_cbuf_addtext(const char *text)
{
	if (!ready)
		return;
	((void (*)(int, const char *))(base + RVA_CBUF_ADDTEXT))(0, text);
}

dvar_t *game_find_dvar(const char *name)
{
	if (!ready)
		return NULL;
	return ((dvar_t * (*)(const char *))(base + RVA_DVAR_FINDVAR))(name);
}

const char *game_dvar_string(const char *name, char *buf, int size)
{
	dvar_t *d = game_find_dvar(name);
	if (!d) {
		snprintf(buf, size, "<missing>");
		return buf;
	}
	switch (d->type) {
	case DVAR_TYPE_BOOL:
		snprintf(buf, size, "%d", d->current.enabled);
		break;
	case DVAR_TYPE_INT:
	case DVAR_TYPE_ENUM:
		snprintf(buf, size, "%d", d->current.integer);
		break;
	case DVAR_TYPE_FLOAT:
		snprintf(buf, size, "%g", d->current.value);
		break;
	case DVAR_TYPE_STRING:
		snprintf(buf, size, "\"%s\"", d->current.string ? d->current.string : "");
		break;
	default:
		snprintf(buf, size, "<type %d>", d->type);
	}
	return buf;
}

unsigned long long game_peek(unsigned long long rva, int width)
{
	if (!ready || rva >= 0x4942000) // SizeOfImage of the known build
		return 0;
	const uint8_t *p = base + rva;
	switch (width) {
	case 1:
		return *p;
	case 2:
		return *(const uint16_t *)p;
	case 8:
		return *(const uint64_t *)p;
	default:
		return *(const uint32_t *)p;
	}
}

unsigned long long game_peek_ptr(unsigned long long rva, unsigned long long off, int width)
{
	uintptr_t p = (uintptr_t)game_peek(rva, 8);
	if (!p)
		return 0;
	const uint8_t *q = (const uint8_t *)(p + off);
	if (IsBadReadPtr(q, width))
		return 0;
	switch (width) {
	case 1:
		return *q;
	case 2:
		return *(const uint16_t *)q;
	case 8:
		return *(const uint64_t *)q;
	default:
		return *(const uint32_t *)q;
	}
}

void game_xuid_add(long long delta)
{
	if (!ready)
		return;
	uint64_t *xuid = (uint64_t *)(base + RVA_XUID_TABLE); // local client 0
	uint64_t before = *xuid;
	*xuid = before + delta;
	log_printf("game: xuid %016llx -> %016llx", (unsigned long long)before, (unsigned long long)*xuid);
}

int game_dvar_raw(const char *name, char *buf, int size)
{
	dvar_t *d = game_find_dvar(name);
	if (!d)
		return 0;
	switch (d->type) {
	case DVAR_TYPE_STRING:
		if (!d->current.string)
			return 0;
		lstrcpynA(buf, d->current.string, size);
		return 1;
	case DVAR_TYPE_BOOL:
		snprintf(buf, size, "%d", d->current.enabled);
		return 1;
	case DVAR_TYPE_INT:
	case DVAR_TYPE_ENUM:
		snprintf(buf, size, "%d", d->current.integer);
		return 1;
	default:
		return 0;
	}
}

void game_log_commands(void)
{
	if (!ready)
		return;
	cmd_function_t *c = *(cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	char line[1800];
	int n = 0, count = 0;
	for (; c; c = c->next) {
		count++;
		uintptr_t fn = (uintptr_t)c->function;
		uintptr_t rva = fn >= (uintptr_t)base ? fn - (uintptr_t)base : 0;
		int w = snprintf(line + n, sizeof(line) - n, "%s@%llx ", c->name ? c->name : "?", (unsigned long long)rva);
		if (w < 0 || n + w >= (int)sizeof(line) - 64) {
			log_printf("cmds: %s", line);
			n = 0;
			w = snprintf(line, sizeof(line), "%s@%llx ", c->name ? c->name : "?", (unsigned long long)rva);
		}
		n += w;
	}
	log_printf("cmds: %s", line);
	log_printf("cmds: %d commands registered", count);
}

static const char *cmd_args_argv(int rva, int i)
{
	uint8_t *args = (uint8_t *)GetModuleHandleA(NULL) + rva;
	int nesting = *(int *)args;
	if (nesting < 0 || nesting >= 8 || i < 0 || i >= *(int *)(args + 0x44 + nesting * 4))
		return "";
	return (*(const char ***)(args + 0x68 + nesting * 8))[i];
}

const char *game_cmd_argv(int i)
{
	return cmd_args_argv(RVA_CMD_ARGS, i);
}

const char *game_sv_cmd_argv(int i)
{
	return cmd_args_argv(RVA_SV_CMD_ARGS, i);
}
