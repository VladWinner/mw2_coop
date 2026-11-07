#pragma once

// Discord presence (discord.c): shows the mission and the mode (solo, split screen, co-op) in the player's Discord, through the Discord
// client's local pipe. Off unless settings.ini [discord] client_id holds the id of a Discord application. Call often (the poll of control.c).
void discord_poll(void);
