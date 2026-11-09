#pragma once
// The menus the mod adds to the game (buttons of the main menu, rows of its popups) as a Call of Duty .menu file: content/release/menus.menu.
// The built-in copy is compiled in (menus_gen.h, tools/gen_menus.py); mw2coop\menus.menu next to the game replaces the text and action of an item
// (and a popup's title) of the same menu and name. Only the subset the mod needs is read: menuDef, itemDef, name, text, title, action { ... }.

typedef struct {
	char menu[40];
	char name[40];
	char text[80];    // the label, "@NAME" for a localized text
	char action[240]; // the menu script in the game's parsed form ("play" "mouse_click" ; ...); stable for the life of the process
} MenuItem;

// The n-th (0-based) itemDef of <menu>, in file order, or NULL.
const MenuItem *menutable_item(const char *menu, int n);

// The popup's heading ("@NAME"), or NULL.
const char *menutable_title(const char *menu);

// A yes/no popup's question ("@NAME"), or NULL.
const char *menutable_question(const char *menu);
