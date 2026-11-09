#pragma once
// The main menu's logo with the mod's name under it (logo.c). <material> is the game's logo material (Material *). dump_only: only write the game's logo
// to mw2coop\logo-dump.tga (QA, for making the picture of the word), do not change anything.
void logo_install(void *material, int dump_only);

// A copy of the material <template_material> (a Material *) with the About page's four icons (mw2coop\about-icons.tga, 512 x 128) as its picture; the game's own material is
// not changed. NULL if the picture cannot be made.
void *icons_material(void *template_material);
