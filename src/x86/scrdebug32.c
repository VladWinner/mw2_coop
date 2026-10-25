// Script runtime errors in the 32-bit build. Scr_Error (0x457ae0) keeps its message (copied into a 1 KB buffer at 0x190ec18,
// pointed to by 0x18e3f90) and jumps to the error handler 0x612f50, which longjmps out of the failing script thread (or ends
// the game) without logging it. The hook logs the message first, then runs the handler as it is. The 64-bit scrdebug.c also
// maps the code position to file::function; that part is not ported yet (the positions need the program buffer and the
// loaded-scripts table of this build).
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "hook.h"
#include "log.h"
#include "scrdebug.h"

#define RVA_ERROR_HANDLER 0x212f50 // void (void); starts with cmp byte [0x18e3f8f], 0
#define RVA_ERROR_MESSAGE 0x14e3f90 // const char * (0x18e3f90)
#define RVA_ERROR_FLAG_A  0x14e3f8f // uint8
#define RVA_ERROR_FLAG_B  0x1188a34 // uint8 (0x1588a34)

static uint8_t *base;
static void (*handler_original)(void);

static void error_hook(void)
{
	if (!base[RVA_ERROR_FLAG_A] && !base[RVA_ERROR_FLAG_B]) {
		const char *message = *(const char **)(base + RVA_ERROR_MESSAGE);
		MEMORY_BASIC_INFORMATION mbi;
		if (message && VirtualQuery(message, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT)
			log_printf("script error: %.400s", message);
		else
			log_printf("script error (no message)");
	}
	handler_original();
}

void scrdebug_install(void)
{
	static const uint8_t expected[] = {0x80, 0x3d, 0x8f, 0x3f, 0x8e, 0x01, 0x00};
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!hook_function(base + RVA_ERROR_HANDLER, expected, sizeof(expected), error_hook, (void **)&handler_original)) {
		log_printf("scrdebug: unexpected bytes at the script error handler, script errors are not logged");
		return;
	}
	log_printf("scrdebug: script errors are logged");
}
