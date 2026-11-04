#pragma once
#include <stdint.h>

// QA: which per-client state of the engine is outside the swapped ranges (src/ctxdiff.c, [native] ctxdiff=1). 64-bit only. native.c brackets each context's client frame with before/after.
void ctxdiff_start(uint8_t *base, int (*in_swapped_range)(uint32_t rva));
void ctxdiff_before(int ctx);
void ctxdiff_after(int ctx);
void ctxdiff_report(void);
