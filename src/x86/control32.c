// The first slice of control.c for the 32-bit build: lines appended to <game dir>\mw2coop\commands.txt are run
// in order, as in the 64-bit DLL (QA only: the file must exist at start-up).
//   plain line  -> Cbuf_AddText(0, line)
//   !sleep <ms> -> pause the queue
//   # comment
// The other "!" commands of control.c come over with the parts of the DLL they need.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

// void Cbuf_AddText(int localClientNum, const char *text); cdecl, locks critical section 0x21 (thread safe),
// "Cbuf_AddText: overflow" inside. Official build FC9E4843...52D7 (docs/32bit-plan.md).
#define RVA_CBUF_ADDTEXT 0x9b40

static void run_line(char *line)
{
	while (*line == ' ' || *line == '\t')
		line++;
	if (!*line || *line == '#')
		return;
	if (*line == '!') {
		if (strncmp(line, "!sleep ", 7) == 0)
			Sleep((DWORD)atoi(line + 7));
		else
			log_printf("control: command %s is not ported to the 32-bit build yet", line);
		return;
	}
	char text[1024];
	snprintf(text, sizeof(text), "%s\n", line);
	log_printf("control: > %s", line);
	((void (*)(int, const char *))((uint8_t *)GetModuleHandleW(NULL) + RVA_CBUF_ADDTEXT))(0, text);
}

static DWORD WINAPI control_thread(LPVOID path_arg)
{
	const char *path = path_arg;
	LONGLONG offset = 0;
	char carry[1024];
	int carried = 0;
	for (;;) {
		Sleep(250);
		HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		LARGE_INTEGER size;
		GetFileSizeEx(f, &size);
		if (size.QuadPart < offset) {
			offset = 0; // file was replaced
			carried = 0;
		}
		if (size.QuadPart > offset) {
			char buf[4096];
			DWORD got = 0;
			LARGE_INTEGER pos = {.QuadPart = offset};
			SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
			if (ReadFile(f, buf, sizeof(buf), &got, NULL) && got > 0) {
				offset += got;
				for (DWORD i = 0; i < got; i++) {
					if (buf[i] == '\n') {
						carry[carried] = 0;
						carried = 0;
						run_line(carry);
					} else if (buf[i] != '\r' && carried < (int)sizeof(carry) - 1) {
						carry[carried++] = buf[i];
					}
				}
			}
		}
		CloseHandle(f);
	}
	return 0;
}

void control32_start(void)
{
	static char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\commands.txt");
	if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		log_printf("control: no %s at start-up, the command file is not read", path);
		return;
	}
	log_printf("control: reading %s (QA)", path);
	CloseHandle(CreateThread(NULL, 0, control_thread, path, 0, NULL));
}
