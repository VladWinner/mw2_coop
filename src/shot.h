#pragma once

// QA: save this machine's next game frame as <game dir>\mw2coop\shots\<name>.bmp (src/shot.c).
void shot_install(void);
void shot_request(const char *name);
