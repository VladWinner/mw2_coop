// Diagnostic log of the mouse and the pads ([native] inputlog=1): every line is "<ms since the start>,<KIND>,<fields>" in <game dir>\mw2coop\inputlog.csv (the file is closed and
// renamed inputlog.old.csv at 40 MB, so the newest minutes are always there). The kinds are written by native.c (PAD, PADMENU, PADLOST, MOUSE, FRAME, XI, EVENT) and rawinput.c (RAW).
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "inputlog.h"
#include "log.h"

static FILE *file;
static CRITICAL_SECTION lock;
static int started, enabled;
static DWORD t0;
static long written;
static char path[MAX_PATH];

int inputlog_on(void)
{
	return enabled;
}

void inputlog_open(void)
{
	if (started)
		return;
	started = 1;
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != 0x5c)
		n--;
	path[n] = 0;
	char ini[MAX_PATH];
	lstrcpyA(ini, path);
	lstrcatA(ini, "mw2coop\\settings.ini");
	if (!GetPrivateProfileIntA("native", "inputlog", 0, ini))
		return;
	lstrcatA(path, "mw2coop\\inputlog.csv");
	file = fopen(path, "wb");
	if (!file)
		return;
	InitializeCriticalSection(&lock);
	t0 = GetTickCount();
	enabled = 1;
	fprintf(file, "# mw2coop input log: ms,KIND,fields (PAD: player,device,context,play,dt,lx,ly,rx,ry,buttons,lt,rt,keycatchers,forward,right,yaw_in,yaw_out,pitch_in,pitch_out,acc_yaw,acc_pitch,assist; "
		      "MOUSE: context,routed,kb_player,x,y; RAW: events,raw_dx,raw_dy,cursor_dx,cursor_dy,used_dx,used_dy; FRAME: context,split,native,kb_player,dev0,dev1,keycatchers,cursor_x,cursor_y,focus,map; "
		      "XI: pad,result,lx,ly,rx,ry,buttons,lt,rt; EVENT: text)\n");
	log_printf("inputlog: writing %s", path);
}

void inputlog(const char *fmt, ...)
{
	if (!enabled)
		return;
	char line[400];
	int n = snprintf(line, sizeof(line), "%lu,", (unsigned long)(GetTickCount() - t0));
	va_list ap;
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
	va_end(ap);
	if (n < 0 || n > (int)sizeof(line) - 2)
		n = (int)sizeof(line) - 2;
	line[n++] = '\n';
	EnterCriticalSection(&lock);
	fwrite(line, 1, n, file);
	written += n;
	static int rows;
	if ((++rows & 31) == 0)
		fflush(file);
	if (written > 40L * 1024 * 1024) {
		fclose(file);
		char old[MAX_PATH];
		lstrcpyA(old, path);
		lstrcpyA(old + lstrlenA(old) - 3, "old.csv");
		DeleteFileA(old);
		MoveFileA(path, old);
		file = fopen(path, "wb");
		written = 0;
		if (!file)
			enabled = 0;
	}
	LeaveCriticalSection(&lock);
}
