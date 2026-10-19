// Console commands added by mw2coop, run on the game's main thread by Cbuf like any command.
//
// mw2coop_party_join (QA): the normal client path joins the host's Steam lobby first
// (rva 0x28f1b0 -> 0x22dda0 -> PartyClient_Join 0xd7cb0), which activates the client party
// before "connect ip port". Two test copies on one Steam account cannot join each other's Steam
// lobby, so this does what PartyClient_Join does, minus the Steam lobby address lookup.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "party.h"

static uint8_t *base;
#define FN(rva, type) ((type)(base + (rva)))

static void party_join_f(void)
{
	uint8_t *lobby = *(uint8_t **)(base + RVA_PARTY_LOBBY);
	if (!lobby) {
		log_printf("party: no lobby object");
		return;
	}
	uint64_t host_info[8] = {0};
	FN(RVA_PARTY_RESET, void (*)(void))();
	FN(RVA_PARTY_CLEAR_MEMBERS, void (*)(int))(0);
	base[RVA_PARTY_IS_HOST] = 0;
	char ok = FN(RVA_LOBBY_JOIN, char (*)(void *, uint64_t, uint32_t, uint64_t *, uint64_t, uint32_t, uint32_t))(
		lobby, 0, 0x322, host_info, 0, 2, 0);
	*(int32_t *)(base + RVA_PARTY_START_TIME) = FN(RVA_SYS_MILLISECONDS, int (*)(void))();
	FN(RVA_PARTY_ACTIVATE, void (*)(void))();
	FN(RVA_PARTY_AFTER_JOIN, void (*)(void))();
	log_printf("party: client party joined (lobby join %d, active %d, lobby+0x10 %d)", ok,
		   base[RVA_PARTY_ACTIVE], lobby[0x10]);
}

static cmd_function_t commands[] = {
	{NULL, "mw2coop_party_join", NULL, NULL, party_join_f},
};

void party_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	cmd_function_t **head = (cmd_function_t **)(base + RVA_CMD_FUNCTIONS);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		commands[i].next = *head;
		*head = &commands[i];
		log_printf("party: added console command %s", commands[i].name);
	}
}
