#pragma once

// QA: save this machine's next game frame as <game dir>\mw2coop\shots\<name>.bmp (src/shot.c).
void shot_install(void);
// Saves the frame about to be shown as <game dir>\mw2coop\shots\<name>.bmp; only on the render thread at EndScene (src/native.c).
void shot_save_now(void *dev, const char *name);
void shot_request(const char *name);
void shot_request_present(const char *name);  // QA: the frame as presented (src/native.c calls shot_present_tick from its Present hooks)
void shot_present_tick(void *dev);
