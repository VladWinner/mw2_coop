#pragma once

// QA: a sampling profile of every thread of the game for the given number of seconds, written to the log (src/loadprof.c; [native] loadprof=<seconds>).
void loadprof_start(unsigned seconds);
// QA: the engine's aspect numbers into the log every 5 seconds ([native] aspectlog=1).
void aspectlog_start(void);
