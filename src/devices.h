#pragma once

// The native split screen lobby's devices (devices.c): each player's device, chosen by a press in the lobby (the first device pressed is player 1's)
#define DEVICE_NONE (-2)
#define DEVICE_KEYBOARD (-1)
void devices_begin(void);        // the lobby opens: both slots free, the presses are watched
void devices_begin_assigned(int player0, int player1); // reopen the lobby with the same two devices already assigned
void devices_end(void);          // the lobby closes
int devices_get(int player);     // DEVICE_NONE until he pressed, DEVICE_KEYBOARD, or an XInput pad 0-3
int devices_menu_pad(void);      // player 1's pad in the lobby (he works the menus), -1 when he is not on a pad
void devices_install(void);
