#pragma once

// QA: log the game's UDP packets by pointing its WSOCK32 sendto/recvfrom IAT slots at wrappers.
void net_install(void);
