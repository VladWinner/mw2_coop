#pragma once

// No Russian: the crew's mesh on the co-op body's skeleton, in memory (transplant.c).
void transplant_install(void);
// Control thread, every 250 ms: queues the main-thread work a level script asked for.
void transplant_poll(void);
