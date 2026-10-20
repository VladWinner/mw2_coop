#pragma once

// Log GSC runtime errors (the retail game discards their text) with the script call stack.
void scrdebug_install(void);

// Called for every script the game compiles, with the final text: records its function names.
void scrdebug_note_source(const char *ext, const char *text, int len);
