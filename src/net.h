#pragma once

// QA: log the game's UDP packets by pointing its WSOCK32 sendto/recvfrom IAT slots at wrappers.
void net_install(void);

// Send an out-of-band packet "\xff\xff\xff\xff<text>" to the co-op partner; 0 if none is known yet.
int net_send_mod(const char *text);
