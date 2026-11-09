#pragma once

// Guards against crashes seen in the field (64-bit game): the game's own code trusts a pointer the mod cannot always make true.
void guards_install(void);

// Called before level-zone teardown/rebuild and vid_restart to invalidate per-thread zone-region and renderer-table caches.
void guards_invalidate_caches(void);
