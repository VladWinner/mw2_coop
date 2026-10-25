// Parts of the 64-bit DLL that the 32-bit build does not have yet; each stub is replaced by the real thing as
// that part is ported (docs/32bit-plan.md), and its source file moves into the x86 line of build.sh.
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "memdump.h"
#include "menus.h"
#include "net.h"
#include "scrdebug.h"
#include "shot.h"
#include "transplant.h"

void scrdebug_note_source(const char *ext, const char *text, int len)
{
	(void)ext;
	(void)text;
	(void)len;
}

void scrdebug_trace_method(uintptr_t entry_rva)
{
	log_printf("scrdebug: !trace %llx is not ported to the 32-bit build yet", (unsigned long long)entry_rva);
}

void *transplant_clone(const char *name, void *(*find)(int, const char *))
{
	(void)name;
	(void)find;
	return NULL;
}

void transplant_poll(void)
{
}

void transplant_new_level(void)
{
}

int memdump_command(char *line)
{
	(void)line;
	return 0;
}

void menus_poll(void)
{
}

void menus_log_names(void)
{
}

void menus_log_items(const char *menu_name)
{
	(void)menu_name;
}

void menus_log_action(const char *menu_name, const char *item_name)
{
	(void)menu_name;
	(void)item_name;
}

void net_log_budget(int packets)
{
	(void)packets;
}


// The 32-bit game's Direct3D device is not located yet: a static search finds no call of its methods in the exe, a throwaway
// device has the Steam overlay's copy of the vtable, and every object of d3d9.dll in the exe's data looks like a device.
void shot_install(void)
{
}

void shot_request(const char *name)
{
	log_printf("shot: %s not saved (the 32-bit build cannot find the game's Direct3D device yet)", name);
}
