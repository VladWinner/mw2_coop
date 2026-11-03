#pragma once

// Guards against crashes seen in the field (64-bit game): the game's own code trusts a pointer the mod cannot always make true.
void guards_install(void);
