#pragma once
// Makes <path> (the player's settings.ini) from the template when it is not there, and brings it up to date with the template when it is, keeping what the
// player changed (inimerge.h). The file as it was goes to <path>.bak when the merge changes it; the new file is written to <path>.tmp and moved over the old
// one. A file the code cannot read as text (UTF-16), cannot read or cannot write is left as it is. Logs what it did (log_printf).
void ini_sync_file(const char *path, const char *tmpl);
