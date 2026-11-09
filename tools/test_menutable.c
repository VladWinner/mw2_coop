// Checks src/menutable.c (the .menu reader): the built-in rows give the scripts the code used to have as literals, and a mw2coop\menus.menu next to the
// program replaces the text and action of a known item and a popup's title, ignores an unknown item. Run by tools/test_menutable.sh.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "menutable.h"

void log_printf(const char *f, ...)
{
	(void)f;
}

static int failed;
static void expect(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failed = 1;
	}
}

int main(void)
{
	const MenuItem *i = menutable_item("main_text", 0);
	expect(i && !strcmp(i->name, "mw2coop_campaign_btn"), "first main button");
	expect(i && !strcmp(i->action, "\"play\" \"mouse_click\" ; \"setGameMode\" \"sp\" ; \"setdvar\" \"specialops\" 0 ; \"open\" \"mw2coop_campaign\" ; "), "its script is the game's parsed form (a number stays bare)");
	i = menutable_item("main_text", 1);
	expect(i && !strcmp(i->text, "Custom join"), "an override replaces the text");
	expect(i && !strcmp(i->action, "\"play\" \"mouse_click\" ; \"exec\" \"my_command\" ; \"setdvar\" \"credits\" 0 ; "), "an override replaces the action; a number stays bare");
	expect(menutable_item("main_text", 3) == NULL, "an item that is not built in is ignored");
	i = menutable_item("mw2coop_lobbies", 3);
	expect(i && !strcmp(i->action, "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_address\" ; "), "a popup row");
	expect(menutable_item("mw2coop_lobbies", 4) == NULL, "a popup has four rows");
	expect(!strcmp(menutable_title("mw2coop_host") ? menutable_title("mw2coop_host") : "", "My title"), "an override replaces the title");
	expect(!strcmp(menutable_title("mw2coop_saves") ? menutable_title("mw2coop_saves") : "", "@MW2COOP_SAVES"), "a built-in title");
	expect(menutable_title("nothing") == NULL, "no title for an unknown menu");
	i = menutable_item("mw2coop_member", 1);
	expect(i && !strcmp(i->name, "no") && !strcmp(i->text, "@MENU_NO"), "a yes/no popup's second row");
	expect(menutable_item("mw2coop_member", 2) == NULL, "a yes/no popup has two rows");
	expect(menutable_question("mw2coop_member") && !strcmp(menutable_question("mw2coop_member"), "@MW2COOP_MEMBER_Q"), "a yes/no popup's question");
	expect(menutable_question("mw2coop_saves") == NULL, "a list popup has no question");
	printf(failed ? "menutable: FAILED\n" : "menutable: ok\n");
	return failed;
}
