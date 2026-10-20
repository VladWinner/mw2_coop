#pragma once
#include <stdint.h>

// Addresses in iw4sp.exe of the 64-bit Steam build (buildid 25374375, PE timestamp 0x6aaaa257,
// SHA-256 2F8C9CFD...D805), as RVAs from the image base. Found with Ghidra, see NOTES.md.
#define IW4SP_TIMESTAMP 0x6aaaa257u

#define RVA_CBUF_ADDTEXT   0x1ee890  // void Cbuf_AddText(int localClientNum, const char *text); locks CS 0x24
#define RVA_DVAR_FINDVAR   0x26bdf0  // dvar_t *Dvar_FindVar(const char *name); reader-locked, thread safe
#define RVA_CMD_FUNCTIONS  0x1315308 // cmd_function_t *cmd_functions (list head)
#define RVA_XUID_TABLE     0x1bbaad8 // uint64 xuid of local client i at +i*0x68 (Live_GetXuid, rva 0x28f8b0)

// Co-op party (lobby) state
#define RVA_PARTY_LOBBY          0x5dd958 // lobby object *: +0x10 active, +0x70/+0xa8 member slots
#define RVA_PARTY_IS_HOST        0x5dd950 // uint8
#define RVA_PARTY_ACTIVE         0x5dd951 // uint8, gates the party frame (0xd74a0)
#define RVA_PARTY_LAUNCH         0x5dd953 // uint8, set by coopLaunch (0xd7a50)
#define RVA_PARTY_START_TIME     0x5dd954 // int32
#define RVA_PARTY_RESET          0x0d7dd0 // void Party_Reset(void)
#define RVA_PARTY_CLEAR_MEMBERS  0x236920 // void (int)
#define RVA_LOBBY_JOIN           0x236d50 // char Lobby_Join(lobby, localClient, 0x322, hostInfo[8], 0, 2, 0)
#define RVA_PARTY_ACTIVATE       0x0d7380 // void Party_Activate(void): active = 1, member timers
#define RVA_PARTY_AFTER_JOIN     0x0d6820 // void (void), called at the end of PartyClient_Join (0xd7cb0)
#define RVA_SYS_MILLISECONDS     0x293dc0 // int Sys_Milliseconds(void)
#define RVA_COOP_LAUNCH_MAP      0x22e1a0 // void (int localClient): coop slot state 3, net rates, "map <coop_mapName>"
#define RVA_COOP_SLOT_STATE      0x533cb8 // int32 per local client (stride 16): 0 idle, 3 launched

// Co-op lobby (see NOTES.md): listen @0xd25e0, connect @0xd23c0, coopLaunch @0xd25d0,
// host-side coopConnect handler @0x22d830. Online gate @0x28fc00 needs Steam logged on and
// the DemonWare state (rva 0x115c07c) == 4.

// Script loading
#define RVA_SCR_ADD_SOURCE_BUFFER 0x217950 // char *Scr_AddSourceBuffer(filename, extFilename, codePos, archive)
#define RVA_DB_FIND_HEADER  0x0ef5d0 // void *DB_FindXAssetHeader(int type, const char *name)
#define RVA_DB_IS_DEFAULT   0x0efff0 // int DB_IsXAssetDefault(int type, const char *name)
#define RVA_DB_RAWFILE_LEN  0x0efdc0 // int DB_GetRawFileLen(RawFile *)
#define RVA_DB_RAW_BUFFER   0x0efc70 // void DB_GetRawBuffer(RawFile *, char *buf, int size)
#define RVA_HUNK_TEMP_HIGH  0x2693f0 // void *Hunk_AllocateTempMemoryHigh(int64 size)

typedef enum {
	DVAR_TYPE_BOOL = 0,
	DVAR_TYPE_FLOAT = 1,
	DVAR_TYPE_FLOAT_2 = 2,
	DVAR_TYPE_FLOAT_3 = 3,
	DVAR_TYPE_FLOAT_4 = 4,
	DVAR_TYPE_INT = 5,
	DVAR_TYPE_ENUM = 6,
	DVAR_TYPE_STRING = 7,
	DVAR_TYPE_COLOR = 8,
} dvar_type_t;

typedef union {
	uint8_t enabled;
	int32_t integer;
	float value;
	float vector[4];
	const char *string;
} dvar_value_t;

typedef struct dvar_s {
	const char *name;    // 0x00
	uint32_t flags;      // 0x08
	uint8_t type;        // 0x0c
	uint8_t modified;    // 0x0d
	uint8_t pad[2];
	dvar_value_t current; // 0x10
	dvar_value_t latched; // 0x20
	dvar_value_t reset;   // 0x30
	uint8_t domain[0x18]; // 0x40
	struct dvar_s *hash_next; // 0x58
} dvar_t;

typedef struct cmd_function_s {
	struct cmd_function_s *next; // 0x00
	const char *name;            // 0x08
	const char *autocomplete_dir; // 0x10
	const char *autocomplete_ext; // 0x18
	void (*function)(void);      // 0x20
} cmd_function_t;

// Returns 1 when the running exe is the build the addresses above belong to.
int game_init(void);
void game_cbuf_addtext(const char *text);
dvar_t *game_find_dvar(const char *name);
// Formats a dvar's current value into buf; returns buf ("<missing>" when not registered).
const char *game_dvar_string(const char *name, char *buf, int size);
void game_log_commands(void);
// Copies a string dvar's current value into buf; 0 if missing or not a string dvar.
int game_dvar_raw(const char *name, char *buf, int size);
// Read the pointer stored at rva, then width bytes at pointer+off (0 if null/unreadable).
unsigned long long game_peek_ptr(unsigned long long rva, unsigned long long off, int width);
// QA only: change local client 0's XUID by delta.
void game_xuid_add(long long delta);
// Read 1/2/4/8 bytes of the game image at an RVA (0 outside the image).
unsigned long long game_peek(unsigned long long rva, int width);
