#pragma once

// Russian game: the mod's own fonts instead of the generic one (see fonts.c).
// Called from the DB_FindXAssetHeader hook for every font asset (type 24) the game asks for, on the game's thread; `find` is the
// game's own lookup (to find the other fonts).
void fonts_seen(void *font, const char *name, void *(*find)(int type, const char *name));
