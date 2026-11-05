#pragma once

// QA: a sampling profile of every thread of the game for the given number of seconds, written to the log (src/loadprof.c; [native] loadprof=<seconds>), starting delay seconds after
// the game started ([native] loadprofat=<seconds>, to profile a fight instead of the load).
void loadprof_start(unsigned seconds, unsigned delay);
// QA: the engine's aspect numbers into the log every 5 seconds ([native] aspectlog=1).
void aspectlog_start(void);
// QA: counts the engine's ReadFile calls by size and the time spent in them, every 10 s ([native] readlog=1).
void readlog_start(void);
