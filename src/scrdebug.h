#pragma once
#include <stdint.h>

// Log GSC runtime errors (the retail game discards their text) with the script call stack.
void scrdebug_install(void);

// 32-bit: how many entries of the script string table are in use, logged with `when` (each level load; the table filled up after ~16 levels in one process,
// "exceeded maximum number of script strings", 2026-11-17). Nothing on 64-bit.
void scrdebug_string_census(const char *when);
// 32-bit: the same while a level runs (the menu tick), logged when the count moved by 200 or more since the last line.
void scrdebug_string_census_tick(void);

// Called for every script the game compiles, with the final text: records its function names.
void scrdebug_note_source(const char *ext, const char *text, int len);

// QA: log every call of a builtin method (table entry at entry_rva) with the GSC call stack.
void scrdebug_trace_method(uintptr_t entry_rva);
