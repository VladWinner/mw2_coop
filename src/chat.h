#pragma once
#include <windows.h>

// The text chat of a network co-op session (chat.c): [chat] in settings.ini, the key bound to mw2coop_chat.
void chat_install(void);
void chat_menu_tick(void);			 // menus.c (main thread): the chat key bound once
void chat_on_packet(const char *buf, int len);	 // net.c: the partner's "mw2coop_t" packet (taken off before the network watch counts his packets)
int chat_window_message(HWND h, UINT msg, WPARAM w, LPARAM l, LRESULT *result); // console.c's window procedure: 1 when the open line took the message
void chat_draw(void);				 // native.c, after every cgame draw: the open line
