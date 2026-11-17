#pragma once
#include <stdint.h>
#include <windows.h>

// The mod's gamepads (pads.c): Xbox pads (XInput, as before) and every other pad SDL3 knows (PlayStation DualShock 4 / DualSense / DualSense Edge, Nintendo Switch pads, 8BitDo and
// others in their non-XInput modes; USB or Bluetooth), shown as XInput pads in the slots the Xbox pads leave free, so the split screen, the lobby's device pick, the menus and the
// rumble work with them without Steam Input or DS4Windows. SDL3.dll (zlib license, mw2coop\SDL3.dll) is loaded at run time; without it the mod has the Xbox pads only.
// [input] sdlpads=0 in settings.ini: Xbox pads only.

// the XInput layouts (the same as XINPUT_STATE / XINPUT_VIBRATION)
typedef struct {
	WORD buttons;
	BYTE lt, rt;
	SHORT lx, ly, rx, ry;
} pads_gamepad_t;
typedef struct {
	DWORD packet;
	pads_gamepad_t pad;
} pads_state_t;
typedef struct {
	WORD left, right; // the low-frequency (big) and the high-frequency (small) motor, 0..65535
} pads_vibration_t;

// XInputGetState / XInputSetState for the mod: the same signatures and return codes (ERROR_SUCCESS, ERROR_DEVICE_NOT_CONNECTED)
DWORD WINAPI pads_get_state(DWORD index, pads_state_t *state);
DWORD WINAPI pads_set_state(DWORD index, pads_vibration_t *vibration);
// "XInputGetState" / "XInputSetState": the functions above, for the places that looked them up in the XInput DLL; NULL for other names
void *pads_xinput_proc(const char *name);
