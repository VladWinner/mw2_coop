#pragma once

// Raw mouse input in place of the cursor's movement (64-bit build; [input] rawmouse=0 in settings.ini switches it off). See src/rawinput.c.
void rawinput_install(void);
