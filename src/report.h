#pragma once
// "Report a bug" (report.c): the pause menu's button and the console command mw2coop_report make mw2coop\reports\mw2coop-report-<date>-<time>.zip with both
// PCs' logs (the partner's over the network), a screenshot, crash.dmp and settings.ini, private paths replaced.
void report_install(void);
// net.c: the partner's "mw2coop_report_req/_again/_part" packets (1: ours, consumed)
int report_on_packet(const char *buf, int len);
// menus.c (main thread, the menu tick): the pause menu gets its "Report a bug" button
void report_menu_tick(void);
// menus_poll (its thread): while a level runs, mw2coop_report_menu is queued every 3 s (the pause menu comes with the level)
void report_poll(int in_level);
