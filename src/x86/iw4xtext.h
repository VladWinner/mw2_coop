#pragma once
// The iw4x-sp client's own texts at the size the client meant with the mod's fonts (iw4xtext.c). 32 bits, the iw4x-sp client only.
void iw4xtext_install(void);
// 1, or the font's units per pixel when the call came from the client's own code (a return address in its ".main" section)
int iw4xtext_scale_of(const void *return_address, const void *font);
