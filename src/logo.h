#pragma once
// The main menu's logo with the mod's name under it (logo.c). <material> is the game's logo material (Material *). dump_only: only write the game's logo
// to mw2coop\logo-dump.tga (QA, for making the picture of the word), do not change anything.
void logo_install(void *material, int dump_only);

// The About page's four icons (mw2coop\about-icons.tga, 512 x 128, straight alpha) as the picture of <material> (a Material *): a new texture goes in the material's
// image. Returns 1 if the picture is in place.
int icons_install(void *material);
