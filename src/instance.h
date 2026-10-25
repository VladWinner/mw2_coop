#pragma once

// The second game process of a split screen session, made from the same install (instance.c).
//   mw2coop_split <pad>   console command: starts it (pad: the XInput pad index that drives it, -1: none)
// Returns 1 when the process was started.
int instance_start(int pad);
// Closes the second process (when this one quits).
void instance_stop(void);
void instance_install(void);
