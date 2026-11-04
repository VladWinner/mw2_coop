#pragma once

// Diagnostic log of the mouse and the pads (src/inputlog.c; [native] inputlog=1): one line per event in mw2coop\inputlog.csv, for a report of a camera or a control that misbehaves.
int inputlog_on(void);
void inputlog(const char *fmt, ...);
void inputlog_open(void);
