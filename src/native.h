#pragma once

// Native split screen (src/native.c): one game process, two local players, the picture cut top and bottom. 64-bit build only.
//   settings.ini [native] players=2 experiment=10 ...   QA: the session starts with the game
//   native_split_start() / native_split_stop()          the menu's split screen lobby: the session starts with its Start and ends when the game is back in the menus
void native_install(void);
// a new level's zones are loading: client 1's last server command of the previous level is cleared
void native_level_load(void);
// menus_poll: notes player 2's client while a level runs (for native_level_load)
void native_poll_players(void);
// 1 while a session runs
int native_split_active(void);
// Where context ctx (0 player 1, 1 player 2) keeps the bytes the game has at a reference rva of the client statics that a split screen keeps per context; NULL outside a session
void *native_ctx_addr(int ctx, unsigned rva);
// One key press (down and up) through the engine's key event, for the split screen lobby's pad
void native_menu_key(int key);
// The Start of a split screen lobby: the game becomes a two local players game; the caller launches the mission. pad: the XInput pad of the second player, -1 none. Returns 1 on success.
int native_split_start(int pad);
int native_split_start2(int dev0, int dev1); // each player's device: an XInput pad 0-3, or -1 the keyboard and mouse
// The session ends (the game is back in the menus): the game's own player count comes back
void native_split_stop(void);
// For the crash log: which client context was live, the session state
const char *native_crash_state(void);
// Arms an EndScene callback; the Present hooks are installed on the render thread after the scene ends.
// Returns 1 when the bootstrap is armed or the hooks are already installed.
int native_present_hooks(void);
void native_present_poll(void);
