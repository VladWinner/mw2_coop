#pragma once

// Answers DB_FindXAssetHeader for the mod's own localized strings (&"MW2COOP_..." in scripts).
void localize_install(void);
// 1 when the game is the Russian one (its MENU_YES starts with a Cyrillic letter); asks the asset database on first use
int localize_is_russian(void);

// Show the text of string <name> (MW2COOP_...) wherever the menus show <alias>.
void localize_select(const char *alias, const char *name);

// Show <text> (Windows-1251) wherever the menus show <alias>.
void localize_set_text(const char *alias, const char *text);

// The game's own localized string <name> (no '@'), or NULL. Main thread only: it is an asset lookup.
const char *localize_text(const char *name);
