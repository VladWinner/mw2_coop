#pragma once
// The campaign missions the mod knows (title, Spec Ops zone, lobby picture). The built-in rows are made from
// content/release/missions.csv (tools/gen_missions.py); mw2coop\missions.csv next to the game replaces or adds rows.

typedef struct {
	char map[48];
	char title[64]; // empty: Discord shows the map name
	char zone[48];  // the Spec Ops zone loaded for the second player's body; empty: none
	int zone_own;   // 1: the zone replaces the level's own, 0: it is loaded on top of the level
	int picture;    // 1: the lobby has this mission's picture and text
} Mission;

// The row of <map> (case-insensitive), or NULL.
const Mission *missions_find(const char *map);
