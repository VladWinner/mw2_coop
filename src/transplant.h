#pragma once

// No Russian: the crew's mesh on the co-op body's skeleton, in memory (transplant.c).
void transplant_install(void);
// Control thread, every 250 ms: queues the main-thread work a level script asked for.
void transplant_poll(void);
// "mw2coop_rider" (the dvar of that name holding "<skeleton> <mesh>"): a copy of the skeleton
// model wearing the mesh model's surfaces (NULL for other names or when the models are not
// loaded); called from the DB_FindXAssetHeader hook with the original function.
void *transplant_clone(const char *name, void *(*find)(int, const char *));

// A level is loading (zones.c): the buffers of the models made two levels ago are freed.
void transplant_new_level(void);
