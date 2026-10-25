#pragma once

// Split screen: several copies of the game on one PC, side by side. settings.ini [splitscreen] says what this copy is
// (see splitscreen.c); with nothing set this does nothing.
void splitscreen_install(void);
// This copy becomes the first of a split screen session (slot 0): the shared window and the half-size picture. Called when the second process is started.
void splitscreen_host_activate(void);
// The first copy's Start: returns 1 when it took the launch over (the half-size picture comes first, then it sends coopLaunch itself).
int splitscreen_before_launch(void);
// The first XInput pad with something pressed, or -1.
int splitscreen_poll_any_pad(void);
