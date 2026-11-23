#pragma once
// The main menu's logo (logo.c). <material> is the game's logo material (Material *). dump_only writes the untouched image
// to mw2coop\logo-dump.tga (QA, for making the Enhanced overlay), without changing the live texture.
void logo_install(void *material, int dump_only);
// Return a mod-owned copy of the game's logo material: original artwork (0) or Enhanced composite (1).
// The caller swaps the menu item's background material; the game's original texture is never modified.
void *logo_material_variant(void *material, int enhanced);

// A copy of the material <template_material> (a Material *) with the About page's four icons (mw2coop\about-icons.tga, 512 x 128) as its picture; the game's own material is
// not changed. NULL if the picture cannot be made.
void *icons_material(void *template_material);
