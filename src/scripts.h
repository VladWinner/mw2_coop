#pragma once

// Replace Scr_AddSourceBuffer so GSC files in <game dir>\mw2coop\scripts override zone rawfiles.
void scripts_install(void);

#ifdef MW2COOP_IW4X
// The iw4x-sp client reads scripts as rawfiles: a made-up rawfile for a script of ours (NULL for any other name), and whether
// DB_IsXAssetDefault must say "not default" for the name. Called from the hooks of DB_FindXAssetHeader and DB_IsXAssetDefault.
void *scripts_raw_override(const char *name);
int scripts_is_override(const char *name);
#endif
