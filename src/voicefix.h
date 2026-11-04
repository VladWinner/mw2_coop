#pragma once

// [russian] voicefix=1: links the player's own voice archives from mw2coop\voicefix into main (src/voicefix.c); 0 removes what was made. Called from settings_load.
void voicefix_apply(const char *settings_path);
