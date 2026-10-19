// winmm.dll proxy: every export in winmm_stubs.S jumps through winmm_real[], which points at
// the real System32 winmm.dll. The game loads winmm.dll from its own folder first, which is
// how this DLL gets into the process.
#include <windows.h>

#include "log.h"
#include "winmm_names.h"

void *winmm_real[WINMM_EXPORT_COUNT];

int proxy_init(void)
{
	wchar_t path[MAX_PATH];
	UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (n == 0 || n > MAX_PATH - 12)
		return 0;
	lstrcatW(path, L"\\winmm.dll");

	HMODULE real = LoadLibraryW(path);
	if (!real)
		return 0;

	int missing = 0;
	for (int i = 0; i < WINMM_EXPORT_COUNT; i++) {
		winmm_real[i] = (void *)GetProcAddress(real, winmm_names[i]);
		if (!winmm_real[i])
			missing++;
	}
	log_printf("proxy: real winmm at %p, %d exports, %d missing", (void *)real, WINMM_EXPORT_COUNT, missing);
	return 1;
}
