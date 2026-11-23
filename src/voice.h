#pragma once
// Push-to-talk voice between the two players of a network co-op session (voice.c): +mw2coop_talk, [voice] in settings.ini.
void voice_install(void);
void voice_on_packet(const char *buf, int len); // net.c: the partner's "mw2coop_v" packet (taken off before the network watch counts his packets)
void voice_menu_tick(void);			// menus.c (main thread): the talk key bound once
