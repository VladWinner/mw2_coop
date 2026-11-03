#pragma once

// Loads the Spec Ops zone of a campaign level with it in a co-op session (zones.c).
void zones_install(void);

// Undoes the raised asset pool sizes before the database is initialised again (vid_restart); the next co-op level raises them again.
void zones_pools_reset(void);
