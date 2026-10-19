// winmm exports implemented in C instead of a plain jmp stub (see HOOKED in
// tools/gen_winmm_proxy.py). timeGetTime is called by the game's main loop every frame, which
// gives us a main-thread tick without patching iw4sp.exe.
#define WIN32_LEAN_AND_MEAN // keep mmsystem.h's dllimport prototypes out
#include <windows.h>

#include "winmm_names.h"

extern void *winmm_real[];

DWORD WINAPI timeGetTime(void)
{
	return ((DWORD(WINAPI *)(void))winmm_real[WINMM_IDX_timeGetTime])();
}
