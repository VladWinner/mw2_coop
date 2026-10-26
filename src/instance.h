#pragma once

// The second game process of a split screen session, made from the same install (instance.c).
//   mw2coop_split <pad>   console command: starts it (pad: the XInput pad index that drives it, -1: none)
// Returns 1 when the process was started.
int instance_start(int pad);
// Closes the second process (when this one quits).
void instance_stop(void);
// 1 while the second process of a split screen session runs.
int instance_running(void);
// The lobby of a split screen session: the popup that asks the second player to press a button on his device, then his A is his Ready.
void instance_assign_begin(void);
void instance_assign_end(void);
int instance_pad(void);          // his pad, -1 until he has pressed something
int instance_second_ready(void); // 1 when he pressed Ready
void instance_install(void);
