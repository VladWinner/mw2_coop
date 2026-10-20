#pragma once

// Progress file on the host and the mw2coop_continue console command.
void progress_install(void);

// Host: remember the mission and checkpoint to continue from.
void progress_save(const char *map, const char *start);

// Host: the out-of-band packet that tells the client the start point; returns its length.
int progress_launch_packet(char *buf, int size);

// Client: handles a received packet if it is ours (returns 1: drop it, the game must not see it).
int progress_on_packet(const char *buf, int len);

// the mission and start point saved last; 0 if none
int progress_last(char *map, int map_size, char *start, int start_size);
