#pragma once

// The developer console (console.c). The retail builds have the console's dvars but not its code (nothing draws it, nothing reads its key), so the mod brings its own:
// the key left of 1 (` / ~, by scan code) opens a panel over the top of the game picture, the keyboard goes to it and not to the game while it is open.
//   <command>        sent to the game's command buffer
//   <dvar>           prints the dvar's value; <dvar> <value> sets it
//   clear            empties the panel;  help  lists the keys
void console_install(void);
// A line of output for the panel (any thread). Engine output is added the same way once the print function is hooked.
void console_print(const char *text);
// native.c's Present hooks call this once per frame (render thread): draws the panel while the console is open.
void console_present(int via, void *device);
