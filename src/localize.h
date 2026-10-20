#pragma once

// Answers DB_FindXAssetHeader for the mod's own localized strings (&"MW2COOP_..." in scripts).
void localize_install(void);

// Show the text of string <name> (MW2COOP_...) wherever the menus show <alias>.
void localize_select(const char *alias, const char *name);
