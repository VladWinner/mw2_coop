#pragma once
#include <stddef.h>
#include <stdint.h>

// Code patches of the game's functions, in one place (the six hooks of the DLL used to carry a copy each).

// Puts the first `len` bytes of the function at `target` (14 <= len <= 32), which must be `expected`, into a
// trampoline that goes on at target + len, and makes the function jump to `replacement` first. The
// trampoline (call it to run the original) is stored in *original BEFORE the function is patched: the game's
// other threads call hot functions (asset lookups), and one that reaches the replacement the instant the
// jump is in place must find the original to call. Returns 1, or 0 (nothing changed) when the bytes are not
// the ones the build has. The trampoline page is written once and then read-execute only: this DLL leaves
// no memory that can be both written and run.
int hook_function(uint8_t *target, const uint8_t *expected, size_t len, void *replacement, void **original);

// The same for code that cannot be copied as it stands (a relative call in it): `code` is the trampoline
// as built by the caller, copied into a read-execute page that is returned (NULL when none could be made).
uint8_t *hook_code(const uint8_t *code, size_t size);

// Overwrites the first `len` bytes (14 <= len <= 32) of `target` with a jump to `replacement`, the rest with
// `filler`. For functions the DLL rewrites whole (the original is not called).
void hook_jump(uint8_t *target, size_t len, const void *replacement, uint8_t filler);

// The 14 bytes of `jmp [rip+0]; dq to`, for callers that build a trampoline by hand.
void hook_jump_bytes(uint8_t out[14], const void *to);
