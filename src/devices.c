// The native split screen lobby's devices: each player presses a button on his device, the first device pressed is player 1's, the next other one player 2's
// (pad or keyboard). A pad's Back or the keyboard's Backspace takes the choice back. native.c gets the two devices at Start.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "devices.h"
#include "log.h"
#include "menus.h"

static volatile LONG assigned_dev[2] = {DEVICE_NONE, DEVICE_NONE}; // the players' devices: DEVICE_NONE, DEVICE_KEYBOARD, or an XInput pad 0-3
static volatile LONG assign_gen;

// The lobby's devices are polled here (XInput state as XInputGetState gives it)
typedef struct {
	DWORD packet;
	WORD buttons;
	BYTE lt, rt;
	SHORT lx, ly, rx, ry;
} assign_pad_t;
static DWORD(WINAPI *assign_xinput)(DWORD, assign_pad_t *);

// what a device did since the last look: 0 nothing, 1 a press (a button, a trigger, a stick pushed), 2 its cancel (a pad's Back, the keyboard's Backspace)
static int pad_event(int i)
{
	static WORD was[4];
	static int moved[4];
	if (!assign_xinput) {
		const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
		for (int k = 0; k < 3 && !assign_xinput; k++) {
			HMODULE h = LoadLibraryA(dlls[k]);
			if (h)
				assign_xinput = (DWORD(WINAPI *)(DWORD, assign_pad_t *))GetProcAddress(h, "XInputGetState");
		}
		if (!assign_xinput)
			return 0;
	}
	assign_pad_t st;
	memset(&st, 0, sizeof(st));
	if (assign_xinput((DWORD)i, &st) != ERROR_SUCCESS) {
		was[i] = 0, moved[i] = 0;
		return 0;
	}
	WORD b = st.buttons;
	int m = st.lt > 100 || st.rt > 100 || st.lx > 20000 || st.lx < -20000 || st.ly > 20000 || st.ly < -20000;
	WORD pressed = b & ~was[i];
	int pushed = m && !moved[i];
	was[i] = b, moved[i] = m;
	if (pressed & 0x0020) // Back
		return 2;
	return pressed || pushed ? 1 : 0;
}
static int keyboard_event(void)
{
	static BYTE was[256];
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	int event = 0;
	for (int vk = 8; vk < 256; vk++) {
		if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
			continue; // (the lobby is clicked with the mouse)
		BYTE down = (GetAsyncKeyState(vk) & 0x8000) != 0;
		if (down && !was[vk] && pid == GetCurrentProcessId())
			event = vk == VK_BACK ? 2 : event ? event : 1;
		was[vk] = down;
	}
	return event;
}

// The split screen lobby: each player presses a button on his device. The first device pressed is player 1's, the next other one player 2's; a pad's Back or the
// keyboard's Backspace takes the choice back (the slot is free for the next press). Runs as long as the lobby is open.
static DWORD WINAPI assign_thread(LPVOID gen_arg)
{
	LONG gen = (LONG)(LONG_PTR)gen_arg;
	keyboard_event(); // the keys held while the lobby opened are not a press
	for (int i = 0; i < 4; i++)
		pad_event(i);
	while (gen == assign_gen) {
		Sleep(30);
		int ev[5];
		for (int i = 0; i < 4; i++)
			ev[i] = pad_event(i);
		ev[4] = keyboard_event();
		for (int k = 0; k < 5 && gen == assign_gen; k++) {
			int dev = k < 4 ? k : DEVICE_KEYBOARD;
			int slot = assigned_dev[0] == dev ? 0 : assigned_dev[1] == dev ? 1 : -1;
			if (ev[k] == 2 && slot >= 0) {
				InterlockedExchange(&assigned_dev[slot], DEVICE_NONE);
				log_printf("devices: player %d gave his device back", slot + 1);
				menus_split_changed();
			} else if (ev[k] == 1 && slot < 0) {
				int free = assigned_dev[0] == DEVICE_NONE ? 0 : assigned_dev[1] == DEVICE_NONE ? 1 : -1;
				if (free < 0)
					continue;
				InterlockedExchange(&assigned_dev[free], dev);
				log_printf("devices: player %d plays on %s %d", free + 1, dev == DEVICE_KEYBOARD ? "the keyboard" : "pad", dev == DEVICE_KEYBOARD ? 0 : dev);
				menus_split_changed();
			}
		}
	}
	return 0;
}

// for menus.c: the lobby is worked by player 1; when he plays on a pad, his pad's buttons as menu keys (the lobby's own thread only reads presses)
int devices_menu_pad(void)
{
	return assigned_dev[0] >= 0 ? (int)assigned_dev[0] : -1;
}

void devices_begin_assigned(int player0, int player1)
{
	if (player0 < DEVICE_NONE || player0 > 3)
		player0 = DEVICE_NONE;
	if (player1 < DEVICE_NONE || player1 > 3)
		player1 = DEVICE_NONE;
	LONG gen = InterlockedIncrement(&assign_gen);
	InterlockedExchange(&assigned_dev[0], player0);
	InterlockedExchange(&assigned_dev[1], player1);
	CreateThread(NULL, 0, assign_thread, (LPVOID)(LONG_PTR)gen, 0, NULL);
}

void devices_begin(void)
{
	devices_begin_assigned(DEVICE_NONE, DEVICE_NONE);
}

void devices_end(void)
{
	InterlockedIncrement(&assign_gen);
	InterlockedExchange(&assigned_dev[0], DEVICE_NONE);
	InterlockedExchange(&assigned_dev[1], DEVICE_NONE);
}

int devices_get(int player)
{
	return (int)assigned_dev[player & 1];
}

// QA: mw2coop_split_pad <player 2's pad> [<player 1's device: -1 keyboard>] chooses the devices as the presses would
static void split_pad_f(void)
{
	const char *a = game_cmd_argv(1);
	InterlockedExchange(&assigned_dev[1], a[0] ? atoi(a) : 0);
	const char *b = game_cmd_argv(2);
	InterlockedExchange(&assigned_dev[0], b[0] ? atoi(b) : DEVICE_KEYBOARD);
	menus_split_changed();
}

static cmd_function_t splitpad_cmd = {NULL, "mw2coop_split_pad", NULL, NULL, split_pad_f CMD_NODE_FLAG};

void devices_install(void)
{
	game_add_command(&splitpad_cmd);
}
