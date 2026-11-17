#pragma once

// Russian game: the mod's own fonts instead of the generic one (see fonts.c).
// Called from the DB_FindXAssetHeader hook for every font asset (type 24) the game asks for; `find` is the
// game's own lookup (to find the other fonts).
void fonts_seen(void *font, const char *name, void *(*find)(int type, const char *name));
// Throttled retry: handles language or font-atlas readiness after startup. It looks fonts up through `find`, so it must not run inside an asset lookup:
// the lookup hook only asks for it (fonts_retry_later) and the menu tick runs it (fonts_retry_tick), at the top of the main thread.
void fonts_retry(void *(*find)(int type, const char *name));
void fonts_retry_later(void *(*find)(int type, const char *name));
void fonts_retry_tick(void);
