#pragma once
// Brings a player's settings.ini up to date with the mod's built-in template (settings_template_gen.h, made from content/release/settings.ini)
// without losing what the player changed. Free of the game and of Windows, so tools/test_inimerge.c can check it.
//
// The result follows the template: its sections, its order, its comments. For every key of the template (a line "key=value", or the commented
// default ";key=value") that the player's file has as an active line, the player's own line is kept (his value, his spacing). A key the player's file
// has and the template has not (the console writes some at run time, a test or a newer version of the mod may use others) is kept at the end of its
// section; a section the template has not is kept at the end of the file. Comments the player wrote himself are not kept (the caller keeps the old
// file as settings.ini.bak). Keys and sections compare without regard to case.
#include <stddef.h>

typedef struct {
	int template_keys_added; // keys of the template the player's file did not have (written with the template's default or as a commented default)
	int unknown_kept;        // keys of the player's file that the template does not know, kept
	int player_values_kept;  // keys of the template whose value the player's file decided
} IniMergeStats;

// Returns the merged text with "\n" line ends (malloc: free() it), or NULL when out of memory. `user` may be NULL or empty (a new file: the template).
char *ini_merge(const char *tmpl, const char *user, IniMergeStats *stats);
