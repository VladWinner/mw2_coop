#pragma once
// The mod's updater: asks GitHub for the newest release at start, offers it at the main menu and installs it from inside the game (update.c).

// commands, and the start thread: the last update's leftovers removed, then the check (settings.ini [update] check)
void update_install(void);
// menus.c's tick (main thread, no level running): the popups; main_menu is 1 while the main menu is up and no lobby or session is
void update_tick(int main_menu);
// this build's version ("v1.5.3", "nightly-20261121-abcdef0", or "dev" for a build that is not a release)
const char *update_version(void);
