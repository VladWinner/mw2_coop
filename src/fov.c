// A field of view only one player should get. The Predator's UAV view (maps\_remotemissile)
// sets cg_fov with SetSavedDvar, which reaches both players' screens; setClientDvar cannot set
// cg_fov (the engine refuses it and drops the co-op session). The script names the pilot in
// mw2coop_fov_pilot ("1" host, "2" client, "" when the view ends; maps\_mw2coop::fov_pilot), and
// this thread keeps the non-pilot's field of view unchanged during that view and restores the pilot's
// original cg_fov after the script's hard-coded 65 reset, every 10 ms.
#include <windows.h>

#include "fov.h"
#include "game.h"
#include "log.h"

static DWORD WINAPI fov_thread(LPVOID unused)
{
	(void)unused;
	dvar_t *fov = NULL, *pilot = NULL;
	float normal = 0;
	int held = 0;
	char active_pilot = 0;
	for (;;) {
		Sleep(10);
		if (!fov)
			fov = game_find_dvar("cg_fov");
		if (!pilot)
			pilot = game_find_dvar("mw2coop_fov_pilot");
		if (!fov || fov->type != DVAR_TYPE_FLOAT)
			continue;
		char who = 0;
		if (pilot && pilot->type == DVAR_TYPE_STRING && pilot->current.string)
			who = pilot->current.string[0];
		if (who != '1' && who != '2') {
			// The UAV script resets cg_fov to 65 before clearing mw2coop_fov_pilot. On the pilot's
			// own machine there was no remote-view override to trigger a restore, so put back the
			// value captured before the view; on the partner's machine it is already held at normal.
			if (active_pilot && normal > 0.0f && fov->current.value != normal) {
				fov->current.value = normal;
				fov->modified = 1;
				log_printf("fov: restored cg_fov %.0f after the pilot view", normal);
			}
			normal = fov->current.value; // the player's own field of view
			active_pilot = 0;
			if (held) {
				log_printf("fov: pilot view over");
				held = 0;
			}
			continue;
		}
		active_pilot = who;
		char me = game_peek(RVA_PARTY_IS_HOST, 1) ? '1' : '2';
		if (who == me || normal <= 0)
			continue;
		if (fov->current.value != normal) {
			fov->current.value = normal;
			fov->modified = 1;
			if (!held)
				log_printf("fov: player %c flies the Predator; this player keeps cg_fov %g", who, normal);
			held = 1;
		}
	}
	return 0;
}

void fov_install(void)
{
	HANDLE t = CreateThread(NULL, 0, fov_thread, NULL, 0, NULL);
	if (t)
		CloseHandle(t);
}
