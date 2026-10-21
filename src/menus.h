#pragma once

void menus_install(void);
void menus_poll(void); // control thread, every 250 ms: queues the menu work for the main thread

// net.c: a packet from the partner; 1 if it was mw2coop's lobby packet (the game must not see it).
int menus_on_packet(const char *buf, int len);
