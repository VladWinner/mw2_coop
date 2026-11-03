#pragma once

// Dark title bar and square corners on the game's window (Windows 10 / 11), like current clients: set while the window has a title bar, so borderless and full screen are left alone.
// Re-applied when the window or its style changes (vid_restart, alt+enter). [display] darktitle=0 in settings.ini switches it off.
void window_install(void);
