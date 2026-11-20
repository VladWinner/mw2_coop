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
// How many units of a glyph table make one pixel of `font` (1 for a font the mod has not made again). The game scales its texts by the font's pixel height, which the mod
// writes as the height times this; code that draws a font at a fixed scale of 1 (the iw4x-sp client's own texts) gets letters this many times larger.
int fonts_units_per_pixel(const void *font);
