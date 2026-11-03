#pragma once

// Start the thread that runs commands appended to <game dir>\mw2coop\commands.txt.
void control_start(void);
// Writes to the game memory at rva (size 4 or 8) are watched in every thread; each hit logs the writing code (a hardware watchpoint, see control.c).
void control_watch_write(uintptr_t rva, int size);
// Up to four write watchpoints at once; each hit line also carries what the probe returns (set it with control_set_probe).
void control_watch_multi(const uintptr_t *rvas, int count);
void control_set_probe(int (*fn)(void));
