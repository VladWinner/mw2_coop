# Host side of the co-op data flow (64-bit iw4sp.exe, Ghidra, 2026-10-26, agent "hostframe")

Read-only analysis. All addresses are RVAs (address - 0x140000000). Statement tags: OBSERVED = seen in the decompile or disassembly,
INFERRED = concluded from observed code but not proven by a run, unknown = not found. Names are mine; the decompiler names are FUN_/DAT_.
Scratch decompiles (not in the repo): `local/scratch/hostframe/` (range1.txt = every function 0x22cf20-0x2343a0).

## 0. Corrections to the task text (OBSERVED)

- **rva 0x22d460 is NOT the host's coopConnect handler.** The out-of-band (OOB) command table is at rva 0x350ce0 (16 bytes per entry: name pointer, handler pointer):
  `coopLaunch` -> 0x22d2e0, `coopConnect` -> **0x22d830**, `coopAccept` -> **0x22d460**, `coopReject` -> 0x22dc90, `coopAcceptAck` -> 0x22d1c0.
  So: 0x22d830 = HOST receives the partner's `coopConnect` request and answers `coopAccept`; 0x22d460 = PARTNER receives `coopAccept` and answers `coopAcceptAck`; 0x22d1c0 = HOST receives the ack.
  The dispatcher that looks at the table is 0x22f990 (it also has inline branches for `coopDis`, `coopLoaded`, `coopRestart`).
- The slot table (rva 0x540ba0, 2 slots, stride 0x2650) is the **same table on both machines**, indexed by PLAYER number (0 = host's player, 1 = partner's player).
  On each machine the slot of the player that sits at this machine has address type 2 (NA_LOOPBACK, "local"), the other slot has the real address (type 4, NA_IP). Slot 0 on the partner machine = the host.
  The address is the netchan's remoteAddress (slot+0x10), so "slot in use" = address type != 0, "local" = type 2.
- DAT_1403c3eb0 (rva 0x3c3eb0) is the **number of local/game players**, not a connection count: G_InitGame (rva 0x171370, instructions at 0x1714a6..0x1714b8) does
  `players = 1`, and only if the coop byte (0x52fb5c) is set `players = DAT_141919314` (rva 0x1919314, the coop player count). Every slot loop in this code has the shape
  `coop ? slot.type != 0 : index < players` - the non-coop branch is a leftover local-multi-player path (see section 6).

## 1. The one-paragraph picture (OBSERVED unless marked)

The lockstep unit is a **coop frame**, one per server tick (sv time step 0x32 = 50 ms, 20 per second), numbered by the game's `level.time` (rva 0xd96ec8, read by FUN 0x171360).
Inside the game frame (G_RunFrame, rva 0x173e90, state 1) the host does, in this order: (1) every LOCAL player's client code packs that client's new usercmds + reliable commands into a
client packet and sends it over its netchan (for the host's own player this is the loopback); (2) the host starts a new frame (0x22f830), pumps the network (0x1fd7c0) which routes every incoming client
packet - the remote partner's from UDP, the host's own from the loopback queue - into the frame (0x22f0d0 appends it as a "select client / that client's block" section); (3) 0x22efd0 sends the frame
(plus the unacked older frames) to every REMOTE slot (0x22f400) and then **executes the frame itself** (0x2302d0): for each section it decodes the usercmds, stores the newest into `client_t[n].lastUsercmd`
and calls ClientThink(n) (0x15bd80). The partner machine receives the same frame bytes (0x22edd0 stores them in the same ring) and executes them with the same function 0x2302d0 from its own G_RunFrame (0x15c9e0, client branch).
So host and client execute the identical byte stream; the host's own player is not special.

## 2. Globals used by the host side (rva)

| rva | size | role (my name) | evidence |
|---|---|---|---|
| 0x52fb5c | byte | coop session flag (mirror of the dvar "coop", dvar pointer at 0x1905308) | set by listen 0xd25e0, cleared 0x22dc90/0x22dda0/0x22e5e0 |
| 0x1905302 | byte | **isHost** (1 on the machine that accepted a partner / listened) | set 0xd25e0, 0x22d830; cleared 0x22d460 (partner); tested in 0x15c9e0, 0x22f0d0, 0x22e7a0 |
| 0x1905301 | byte | partner side: "a coopConnect is in flight" (set by 0xd23c0, consumed by 0x22d460) | |
| 0x1905300 | byte | host side: "waiting for the partner's coopAcceptAck" (set 0x22d830, cleared 0x22d1c0); launch is blocked while set (0x22e790) | |
| 0x1905304 | int | validate-data flags mirrored from the host (bit0/bit1), written by 0x22d830, 0x22d460, 0x2302d0 | |
| 0x1919314 | int | coop player count (host: set by listen = 1, by 0x22d830 = party size; partner: read from coopLaunch in 0x22d2e0) | |
| 0x3c3eb0 | int | local player count used by every slot loop (see section 0) | |
| 0x19ada44 | int | **serverId** of this coop session (host chooses, partner copies from coopAccept/coopLaunch); every frame and client packet carries it | 0x22f830, 0x22f0d0, 0x2302d0 |
| 0x540b5c | int | coop time/seed value (listen: timeGetTime; sent in coopLaunch and coopRestart) | |
| 0x540ba0 | 2 x 0x2650 | the slot table, see section 4 | |
| 0x19ada98 | 2 x 0x1678 | **server client_t[2]** (the engine's client array; 0x22d040 clears 0x2cf0 bytes = 2 clients) | |
| 0x192d380 | 20 x 0x38 | **frame ring**: +0 frameNo (int), +4 serverId (int), +8 msg_t (0x30 bytes: +0 overflow, +8 data ptr, +0x18 maxsize, +0x1c cursize, +0x24 readcount, +0x28 bit pos); | 0x22f830, 0x2302d0 |
| 0x1919380 | 20 x 0x1000 | frame data buffers (entry i uses 0x1919380 + i*0x1000) | |
| 0x1919378 | int | ring head index 0..19 (the frame being built / newest) | |
| 0x191937c | int | ring index of the frame being executed now (set at the top of 0x2302d0) | |
| 0x192d7e0 | byte | "coop dropped / disconnect pending" (message text at 0x192d7f0, 0x100 bytes) | 0x22f3d0, 0x22dd10, 0x22ed70 |
| 0x192d7e1 | byte | restart (fast_restart) pending | |
| 0x192d7e3 | byte | **waiting for everyone to load** (set by 0x22e4d0 at launch/map load) | |
| 0x192d7e4 | int | running state hash (0x22dee0/0x22df10/0x22dfb0 add values, only when 0x192d8f9 != 0); written into every frame header | |
| 0x192d8f4 | byte | **a remote partner is too many frames behind -> host sim is held** | set at the end of 0x22efd0 and in 0x22e920 |
| 0x192d8f5 | byte | (partner side) coopLoaded was sent | |
| 0x192d8f7 | byte | "frame-building window open": 0x22f0d0 accepts client packets only while this or 0x192d8f4 is set | 0x22e040/0x22e050 |
| 0x192d8f8 | byte | "inside a real G_RunFrame step with level.time > 250": gate for building/executing a frame (0x15c9e0) | set by 0x22e780, cleared by 0x22e460 in the sim step wrappers 0x233a30, 0x233de0, 0x234140, 0x234260 |
| 0x192d8f9 | byte | state-hash accumulation enabled (G_InitGame calls 0x22dec0 in coop) | |
| 0x192d8fc/0x192d900/0x192d904/0x192d908 | ints | behind-timer start, next resend time, resend backoff multiplier, rand state | 0x22e920 |
| 0x13260e8 | ptr | dvar **cl_paused** (string at 0x3298a8): the coop code uses it as the hold switch of the server sim | 0x22e4d0, 0x22e920, 0x233190, 0x233400 |
| 0x13260c0 | ptr | dvar sv_running | |
| 0xd9e070 | n x 0x2a0 | game g_entities; gentity+0x108 = gclient_t*, gclient+0xaca8 = sess.cmd (0x40), +0xace8 = sess.oldcmd | 0x15bd80 |
| 0x1919318 | ptr | dvar coop_validateData (0 off, 1, 2) | registered 0x230c00 |

All the coop dvars are registered by 0x230c00 (defaults: coop_maxFramesClientCanBeBehind 10 [0x1919320], coop_minFramesClientCanBeBehind 5 [0x1919328], coop_maxFramesPerPacket 10 [0x1919330],
coop_fastForwardThreshold 100 [0x1919338], coop_maxPacketUserCmds 5 [0x1919340], coop_backoffScheme [0x1919370], coop_hosttimeout 10000 [0xd03938], coop_maxServerLoadWaitTime 90000 [0xd03940],
coop_autoLaunch [0xd03948], coop_waitWarnDelay/Rate, coop_mapName, coop_start, coop). 0x22e1a0 overwrites min/maxFramesClientCanBeBehind, maxFramesPerPacket, maxPacketUserCmds and cl_maxpackets from the measured bandwidth (FUN 0xd6760) at launch.

## 3. Data layouts (OBSERVED unless marked)

### 3.1 Slot table entry (rva 0x540ba0 + p*0x2650, p = 0..1, "slot p" = player p)

| offset | meaning | evidence |
|---|---|---|
| +0x0000 .. +0x0638 | **netchan_t** (cleared and filled by Netchan_Setup rva 0x203190, 0x638 bytes): +0 outgoingSequence (starts 1), +4 sock (0 or 1), +8 dropped, +0xc incomingSequence, +0x10 remoteAddress, +0x24 fragmentSequence, +0x28 fragmentLength, +0x30/+0x38 fragment receive buffer ptr/size, +0x3c unsentFragments, +0x44 unsentLength, +0x48/+0x50 unsent buffer ptr/size | 0x203190, 0x203200, 0x202ff0 |
| +0x10 .. +0x24 | the netchan's remoteAddress = netadr_t {+0 int type (0 free, 2 NA_LOOPBACK = local, 4 NA_IP = remote), +4 ip[4], +8 u16 port, ...}. **"Slot in use" = type != 0; "local" = type == 2.** For a type-2 slot the u16 at +0x18 (the port) is the **local client number** of that player (0x22d460 stores its param there; listen 0xd25e0 stores 0 in slot 0; read by 0x15ce10, 0x22e7a0, 0x2302d0 as `*(ushort*)(slot+0x18)`). The loopback layer also selects its ring by this port | 0x22d460, 0xd25e0, 0x15ce10 |
| +0x0638 .. +0x1638 | fragment receive buffer (0x1000), passed to Netchan_Setup | 0x22d460/0x22d830 (0x1405411d8 + p*0x2650) |
| +0x1638 .. +0x2638 | unsent-fragment buffer (0x1000) | (0x1405421d8 + p*0x2650) |
| +0x2640 | int, **host side: the newest coop frame number the remote slot has acknowledged** (written by 0x22f0d0 from every accepted client packet; cleared by 0x22f2d0) | 0x22f0d0, 0x22ec30, 0x22f400 |
| +0x2644 | byte, **"coopLoaded" flag** of the slot (set by the OOB handler in 0x22f990 for a remote slot, by 0x22e330 for a local one) | 0x22f990, 0x22e330, 0x22e920 |
| +0x2648 | int, Sys_Milliseconds (FUN 0x293dc0) when the flag was set | 0x22e3d0 |
| +0x2638, +0x263c, +0x264c | not referenced anywhere I read: unknown | |

(The "state values 2 and 3" in the task: 2 = NA_LOOPBACK in the address type; 3 appears as the 3-bit section type "select client" in the frame stream and as the `3` argument of FUN 0xd7220 - meaning there unknown.)

### 3.2 server client_t (rva 0x19ada98 + n*0x1678, n = 0..1; n = player number = slot number) - the fields the coop code touches

| offset | meaning |
|---|---|
| +0x0000 | int state; the coop code needs > 2 to send/execute, >= 4 to accept a section for the client ("Invalid co-op packet - references invalid clientNum %i"). 0x22d040 sets 4 for every client in use |
| +0x1210 | int **coop client state**: 0 unused, 1 "ignore its packets" (0x2308a0, 0x230020, 0x2306e0 return on it), 3 -> the executor calls 0x22cf90 (ClientBegin trigger), 4 active. 0x22d040 sets 4 when `index < DAT_141919314` |
| +0x1218 | int last reliable-command sequence executed for this client (0x2306e0) |
| +0x121c | 0x400-byte copy of the last reliable command string |
| +0x161c | 0x10-byte name "player %i" (set by 0x22d040) |
| +0x162c | int time of the last packet (Sys_Milliseconds); 0x2308a0 / 0x230020 / 0x22efd0 refresh it |
| +0x1630 | **lastUsercmd, 0x40 bytes** (SV_GetUsercmd rva 0x2316e0 copies it) |
| +0x1670 | gentity pointer (g_entities + n*0x2a0), set by 0x22cf90 and 0x22d040 |

### 3.3 Coop frame ring (rva 0x192d380, 20 entries x 0x38; data buffers rva 0x1919380, 20 x 0x1000)

Entry: +0 frameNo (= level.time of the step; 1 if it would be 0), +4 serverId, +8 msg_t whose data pointer is the entry's 0x1000-byte buffer (cursize at entry+0x24).
msg_t layout (OBSERVED from the MSG helpers 0x2016b0 = BeginReading, 0x2016c0 = Init, 0x2016f0 = ReadByte, 0x201810 = ReadLong, 0x201860 = ReadShort, 0x201bb0 = WriteByte, 0x201b30 = WriteBits(msg, value, bits), 0x201bd0 = WriteData, 0x201c70 = WriteLong):
+0 overflowed, +8 data ptr, +0x18 maxsize, +0x1c cursize, +0x24 readcount, +0x28 bit position.

**Frame bytes** (written by 0x22f830 + 0x22f0d0, read by 0x2302d0):
```
header (0x22f830):  long serverId | long stateHash (0x192d7e4) | long frameNo | byte validateLevel (coop_validateData 0/1/2)
one section per client packet (0x22f0d0), in packet-arrival order:
    3 bits value 3 ("select client") | 1 bit player number | byte 0x3d |
    <the client's payload, copied verbatim from its packet after the packet header, see 3.4> |
    byte 0x33
```
(The reader 0x2302d0: BeginReading, ReadLong serverId, ReadLong hash [value not used there], ReadLong frameNo, ReadByte -> 0x1905304; then loops 0x2306e0 "read next section type" = 3 bits.)
Section types read by 0x2302d0 / 0x2306e0 (3 bits): **1** usercmds (byte 0x51, byte count 1..32, count delta-coded usercmds, byte 0x47), **2** one reliable client command (long seq, string up to 0x400, byte 0x5b),
**3** select client (1 bit client number, byte 0x3d), **4** end of this client's block (byte 0x33; if readcount == cursize the frame is finished). Type 1 before any type 3 would dereference a null client pointer, so each block starts with 3.

### 3.3b Host -> remote packet (written by 0x22f400)
`byte 0x1d | byte 1 | byte n | n x { short size | size bytes of one frame }` sent with Netchan_Transmit (rva 0x203200). n = min(frames the slot is behind, coop_maxFramesPerPacket).
0x22f920(ack) = how many ring steps back from the head the entry with frameNo == ack is (the caller uses at least 1). The partner reads the packet in 0x22edd0 and stores the frames in its own ring (a frame is stored only if its serverId matches and its frameNo is newer than the head).

### 3.4 Client -> host packet (written by 0x1400cd340(localClientNum); the host consumes it in 0x22f0d0). OBSERVED from the decompile; the client side is another agent's part
```
byte 0x1d | byte 0 (= "client packet") | long A (= DAT_1405dac64, copy of serverId made by 0x1400cd330) |
long B (= DAT_1405dac60, newest frame number the client has: set each step by 0x15ce10 from 0x22fff0) |
byte validate (0x22e0f0) | byte "valid bits in the last byte" (patched at the end) |
  0..n x reliable commands: 3 bits 2 | long seq | string | byte 0x5b          (seqs reliableAck+1 .. reliableSequence of clc[n])
  usercmds (only if the unsent count > 0): 3 bits 1 | byte 0x51 | byte count | count deltas (0x2021e0, baseline from FUN 0x2019b0) | byte 0x47
  3 bits 4
```
0x22f0d0 reads: ReadLong A and drops the packet unless A == serverId (0x19ada44); ReadLong B -> `slot[n].+0x2640`; ReadByte validate (adopted into dvar coop_validateData when that is 0); ReadByte trailing-bit count;
then appends the section as in 3.3. It does this only while 0x192d8f7 (frame window) or 0x192d8f4 (stalled) is set; when only 0x192d8f4 is set, the ack is stored but the payload is dropped.

## 4. Function notes (rva; role; tags)

**Per-step order on the host (OBSERVED)** - one pass of the game frame, rva 0x173e90 state 1 (the game frame is entered from the sim-step wrappers 0x233a30 / 0x233de0 / 0x234140 / 0x234260; each wrapper sets 0x192d8f8 and 0x192d8f7 [0x22e780 / 0x22e040] only if level.time > 250 and clears them after; the step first advances level.time by 0x32 in 0x174d00):
1. `0x15ce10` - for the local players: stamp the newest frame number (`0x1400d4be0(0x22fff0(serverId))`) and, for each of the 2 players whose gentity is in use (`gentity+0xb0`) and whose slot is local (type 2; non-coop: `index < DAT_1403c3eb0`), call `0x1400d02a0(localClientNum)`.
   In coop that is `0x1400cd260(n)` -> (rate limit; for a type-2 slot 0 it passes whenever cls state is 6 or 1 s passed) `0x1400cd340(n)` which builds the client packet (3.4) and sends it with Netchan_Transmit on **slot 0's netchan (hard-coded rva 0x540ba0, not slot n)**.
   In non-coop (flag 0) the same function instead walks the local client's usercmd ring and calls **`0x22d000(n, usercmd*)` directly** - see section 6.
2. `0x15c9e0` - the coop frame hook (below).
3. `0x15cbb0`, then for each player in use `0x15cbf0` (per-player pre-think), then the rest of the game frame (entities, scripts).

### 0x15c9e0 coop frame hook (called once per game frame from 0x173e90 state 1)
- OBSERVED: `if coop && any client_t.state > 1 (0x22ed40)`: host (0x1905302): if 0x192d8f8 -> `0x22e040` (open window), `0x22f830` (begin frame), `0x1fd7c0` (pump), `0x22efd0` (end frame), `0x22e050`; else `0x22e050`, `0x1fd7c0` only.
  Partner: if 0x192d8f8 and no drop pending (0x22f820): `0x22e920` (wait/stall maintenance) then `0x2302d0(serverId, level.time)`.
- So frames exist only for steps with level.time > 250 ms; 0x232590 runs 5 steps (250 ms) at map load before that (OBSERVED: loop of 5 x {0x2337f0 advance time, 0x233a30 step}). If level.time starts at 0 (INFERRED) the first coop frame number is 300.

### 0x22f830 begin frame
- Advances `head = (head+1) % 20` (rva 0x1919378), inits the entry's msg_t on `0x1919380 + head*0x1000`, stores frameNo = level.time (0x171360; 1 if 0), serverId (0x19ada44), writes header long serverId, long 0x192d7e4, long frameNo, byte (coop_validateData: 0, 1 or 2).
- No arguments. Caller 0x15c9e0 only. Per game frame, host only (only when 0x192d8f8).

### 0x1fd7c0 pump and 0x230020 / 0x2308a0 packet router
- 0x1fd7c0 (OBSERVED): if coop: read all pending UDP packets (`0x293940` = recvfrom on the two game sockets 0x1bbf0d8 / 0x1bbf2d0) and, **separately, the server loopback queue (`0x202a30(1, ...)` -> `0x202a60`/`0x2003f0`) when dvar sv_running is set**, each into `0x230020(from, msg)`.
- 0x230020 / 0x2308a0 (same body; 0x2308a0 pulls the packets itself with 0x293940, used by 0x233190 and 0x22e920): for a non-OOB packet: find the client n with `client_t[n].+0x1210 != 0` whose slot address matches `from` according to **0x202970** (for address types 0 and 2 it compares the PORT; for type 4 only the ip, see section 5);
  then Netchan_Process on that slot's netchan (`0x202ff0`); if accepted and `+0x1210 != 1`: refresh `+0x162c`, ReadByte must be 0x1d, next byte 0 -> `0x22f0d0(client_t, msg)` (host: client packet), 1 -> `0x22edd0(client_t, msg)` (partner: frames). No slot match -> OOB "coopDis" back. OOB packets (first int -1) go to the string compare in `0x22f990`.
- Per game frame (0x1fd7c0) and from the engine frame (0x233190 / 0x22e920 pump with 0x2308a0).

### 0x22f0d0 append one client's packet to the current frame (host)
- Args `(client_t* c, msg_t* packet)`; player number n = (c - 0x19ada98)/0x1678. Returns at once if not host, or if neither 0x192d8f4 nor 0x192d8f7 is set. Details in 3.4. Writes `slot[n].+0x2640` (ack), may set dvar coop_validateData, appends the section to the head frame (3.3), complains "Packet is %i bytes, unable to add %i more" on overflow.
  When the stalled flag is set it also resets the resend timer (`0x192d900 = -1`, `0x192d904 = 1`); `0x192d8fc = now`.
- Callers: 0x230020 / 0x2308a0 (per received packet).

### 0x22efd0 end of frame (host)
- OBSERVED: `0x22f400()` (send to remote slots); then the first client n with state > 2 whose slot is local (type 2) and `+0x1210 != 1` is the executing client: `+0x162c = now`, MSG_BeginReading on the head frame, **`0x2302d0(0x19ada44, 0x171360())`** = execute the frame just built (the ring lookup by frameNo+serverId finds the head); the loop stops at the first match.
  Finally `0x192d904 = 1` and **`0x192d8f4 = 0x22ec30(coop_maxFramesClientCanBeBehind)`**; if set, `0x192d8fc = 0x192d900 = 0x192d908 = now`.
- The host therefore executes its own frame in the same call that sends it. There is no waiting for the partner's input: whatever sections arrived during the step are in the frame, the rest goes into later frames.

### 0x22f400 send frames to remote slots
- OBSERVED, see 3.3b. Skips type-2 slots (`coop ? type != 2 : !(i < DAT_1403c3eb0)`), needs `client_t.state > 2` and the slot in use. On a Netchan_Transmit failure: type 2 -> error "EXE_TRANSMITERROR", else the slot is dropped (`0x273710` + `0x1fe660(slot.type, 0)`).
- Called by 0x22efd0 (once per frame) and by 0x22e920 (resend while stalled, with 0x192d8f8 set around it).

### 0x22ec30 and 0x192d8f4 "remote partner too many frames behind"
- `0x22ec30(limit)`: min = the smallest `+0x2640` ack over the slots that are **in use and not type 2**; counts how many ring steps back from the head that frame number is (stops at an empty entry or after a full turn); returns `limit <= steps`.
- **OBSERVED quirk:** with NO remote slot (both slots type 2) the min stays 0x7fffffff, no entry matches, the walk covers the whole filled ring (up to 19-20 steps) and the function returns TRUE as soon as the ring holds `limit` entries (default 10). INFERRED consequence: a host with only local slots sets 0x192d8f4 after about 10 frames; then 0x233190 holds the sim (`0x22e170` returns 1 -> hold branch), and after coop_hosttimeout (10 s) 0x22dd10 sets the "COOP_CLIENTDROP" error. This function has to be hooked to return 0 when no remote slot exists.
- 0x22e920 re-evaluates it with coop_minFramesClientCanBeBehind each engine frame while stalled; the stall ends when it returns 0.

### 0x2302d0 frame executor (host and partner alike)
- `(int serverId, int frameNo)`. Finds the ring entry with that frameNo and serverId (error "Unable to find player data for server time %i, serverId %i"), sets `0x191937c = entry index`, BeginReading, reads the header; on the partner adopts the host's validate flags; then loops over sections (3.3):
  - type 2 (inside 0x2306e0): `if seq > client.+0x1218`: gap > 1 -> `0x22feb0(client, "EXE_LOSTRELIABLECOMMANDS")`; else (if `0x232440` allows) **`0x166710(clientNum, string)` = the game's ClientCommand dispatcher** (cheat commands, "where", "setviewpos", "dropweapon", ...), `+0x1218 = seq`, string copied to `+0x121c`.
  - type 1: ReadByte, count 1..32; `0x2316c0(n)` = playerstate n (`n * DAT_1419c3860 + DAT_1419c3858`); `0x2019b0(ps, baseline)` builds the baseline usercmd from the playerstate (weapon ids from ps+0x2b4/+0x2a8, angle deltas, button bits from ps flags); for each cmd `0x201d30(msg, from, to)` = ReadDeltaUsercmd; weapon ids are validated with `0x1400866f0(ps, id)` (invalid -> the current weapon);
    if `client.+0x1210 == 3` -> `0x22cf90` (ClientBegin 0x15e350, sets `+0x1630 = level.time` and the gentity pointer); **if `+0x1210 == 4`: for every cmd with `cmd.serverTime > client.+0x1630.serverTime`: copy the 0x40 bytes into `client_t[n].+0x1630` and call `0x15bd80(n)`** (the ClientThink of that player).
  - type 3: 1 bit n; if n > 1 or `client_t[n].state < 4` -> error "Invalid co-op packet - references invalid clientNum %i" (silently ignored when the frame number < 251); type 4: end of block.
  After the stream, for each slot p < 2 that is local (type 2 in coop): **`0x1400cd300(*(ushort*)(slot+0x18), client_t[p].+0x1218)`**, i.e. tell the local client its reliable commands are acknowledged: `clc[localClientNum].reliableAcknowledge (rva 0x545848 + n*0x3218) = value`, only if `cls state[localClientNum] (rva 0x533cb8 + n*0x10) > 2`.
- Callers: 0x22efd0 (host, once per frame), 0x15c9e0 (partner, once per frame).

### 0x22d000 SV_ClientThink analog (**the answer to "which function applies a usercmd to a player entity"**)
- OBSERVED (disassembly): `(int clientNum in ECX, const usercmd* in RDX)`: copies the 0x40-byte usercmd to `client_t[clientNum].+0x1630` and tail-jumps (`JMP 0x15bd80`, ECX kept) to `0x15bd80(clientNum)`. Only caller: `0x1400d02a0` (the non-coop local path).
- `0x15bd80(n)`: `gentity = g_entities + n*0x2a0` (rva 0xd9e070), `gclient = *(gentity+0x108)`; copies gclient+0xaca8 (sess.cmd, 0x40) to +0xace8 (sess.oldcmd), `0x2316e0(clientNum, gclient+0xaca8)` = SV_GetUsercmd (copies client_t[n].+0x1630 to gclient+0xaca8), `0x15be30(gentity)` (ClientThink_real: returns at once unless the int at `gclient+0xaca4` (right before sess.cmd) == 2, then sets stance/move state, runs pmove), then `0x15f060` or `0x15f020` depending on a float at gclient+0x58.
- So the input of player 1 into the simulation is exactly `client_t[1].+0x1630` plus a call of `0x15bd80(1)`, both done by `0x22d000(1, &cmd)`.

### 0x22e920 coop maintenance (called each engine frame from 0x233190 when level.time > 249, and from 0x15c9e0 on the partner)
- Partner (0x1905302 == 0): the first call sends the OOB **"coopLoaded"** to slot 0's address (the host) and sets 0x192d8f5; afterwards, while 0x192d8f4 or 0x192d7e3 is set: `0x22e330`, `0x2308a0`, and the error "COOP_CLIENTDROP" when `0x22e3d0() > coop_maxServerLoadWaitTime` (rva 0xd03940).
- Host (0x1905302 != 0), only while 0x192d8f4 or 0x192d7e3: `0x234260` (run a pending queued step), `0x22e330` (mark local slots loaded), `0x2308a0` (pump);
  - if `0x192d7e3 == 0 && 0x192d8f4 != 0` (partner behind): if now - 0x192d8fc > coop_hosttimeout -> `0x22dd10` (drop remote + "COOP_CLIENTDROP"); if now >= 0x192d900: set 0x192d8f8, resend frames with `0x22f400`, next resend after `0x1400770b0(0, 0x192d904*50, 0x192d908)` ms with the backoff scheme (coop_backoffScheme 1 = double, 2 = +1); `0x192d8f4 = 0x22ec30(coop_minFramesClientCanBeBehind)`; returns while still behind and within the timeout.
  - else (waiting for everyone to load): for each used slot with `+0x2644 == 0`: if `0x22e3d0() <= coop_maxServerLoadWaitTime` return (keep waiting), else if `0x6bae0() < 3` -> `0x22dd10` and return, else stop waiting.
  - when nothing is left to wait for: `0x192d7e3 = 0`, **dvar cl_paused := 0**, `0x192d8f4 = 0`, `0x192d904 = 1`.
- `0x22e3d0` = the largest of (now - slot[p].+0x2648) over loaded slots, 0 if none. `0x22e330` = for every slot in use and of type 2 with `+0x2644 == 0`: set it to 1 and store the time.

### 0x233190 engine-frame driver (rva 0x1fbde0 calls it every engine frame with the msec)
- OBSERVED, in this order: sv_running set; `0x1400d5820` (client connected-in-game test; else only pump + `0x234260`); `0x22c340() == 0` (not in the load/restart state machine; it calls 0x22c970 which calls 0x22f7b0 and 0x22f2d0); `0x22ed70()` (drop check); partner fast-forward (`coop_fastForwardThreshold`); then `if (0x22e470() != 0) { if level.time > 249: 0x22e920(); if cl_paused != 0 or 0x22e170() [= 0x192d8f4 ? 1 : 0x192d7e3]: pump + do NOT step the sim ... return }`, then `0x22e7a0(msec)` (partner-only, returns 0 on the host), then **`0x233400(msec)` = the server step loop** (accumulates msec; per 0x32 ms: advance level.time via 0x234400 / 0x174d00 and run the game frame through the wrappers).
- `0x22e470()` = 0 only when coop, DAT_1403c3eb0 != 1, a REMOTE slot (type not in {0, 2}) exists, none of {0x192d7e3, 0x192d8f4} is set and 0x192d8f5 is set; it returns 1 otherwise. 0x192d8f5 is only ever set to 1 in the partner branch of 0x22e920 (OBSERVED: refs), so **on the host 0x22e470 is always 1**, which just means "call 0x22e920 and test the hold conditions".
- `0x22e120` = partner only: "newest ring frame <= current sim time", i.e. no newer frame to run (the partner sim waits for frames).
- `0x22ed70` (called from 0x233190): coop and fewer than 2 slots in use -> `0x22dd10` (drop); and if 0x192d7e0 shows/clears the disconnect message.

### Session setup / launch exchange (host view) - the answer to question 4
- `0xd25e0` "listen" (runs when `cls[0].state < 3` and `0x28fc00(0)`): console `nosplitscreen`; clears both slots; slot 0: type 2, ip 127.0.0.1 (0x0100007f), port 0; `0x1919314 = 1`; `0x1905302 = 1`; `Netchan_Setup(0, slot0, loopback addr, 0, ...)`; `0x52fb5c = 1`; dvar solo_play 0; party member 0 added with `0xd7220(0, name, xuid, 1, 0, 3, ..., &addr, 1)`, `0xd7c70(0,..)`, `0xd7b30(0,..)`, `0x24ee50(0, 0xb)` (client state).
- Partner's request -> `0x22d830` (OOB "coopConnect"): replies "coopReject COOP_WRONGPROTOCOL" if the protocol int != 0x1d or the network types mismatch (values 2/10); "coopReject COOP_FULL" if `0x1400d7370` says the party is full or the xuid is already in it; "coopReject COOP_WRONGSTATE" unless coop && host && `cls[param].state < 3` && the request's xuid != own;
  else picks the slot (`0x22e060` finds an existing slot by address, else the first free slot, default 1), copies the partner's netadr into slot p (+0x10..), `DAT_1403c3eb0 = 0x6bae0()`, sends OOB **"coopAccept"** (slot number byte, player count byte, dvar coop_mapName, serverId long, ... host member data), `Netchan_Setup(1, slot p, partner addr, ...)`, `0x1919314 = DAT_1403c3eb0`, `0x1905302 = 1`, `0x1905300 = 1` (waiting for ack), adds the partner as party member p with `0xd7220(..., 0 [not host], index p, 3, 0, &addr, ...)`.
- Partner `0x22d460` (OOB "coopAccept"): requires coop && `cls.state < 3` && `0x1905301` (set by its connect sender 0xd23c0); stores serverId (0x19ada44), g_gameskill, validate flags; marks **its own** slot p as local (`0x1fe660(slot p type, 2)`, port = local client number) and `Netchan_Setup(1, slot 0, host address)`; `0x1905302 = 0`; adds the host (member 0) and itself (member p); replies OOB **"coopAcceptAck"**.
- Host `0x22d1c0` (OOB "coopAcceptAck"): finds the slot by address (0x1402028a0), stores the two longs of the ack as member data (`0xd7c70(p, a)`, `0xd7b30(p, b)`), clears 0x1905300, `0xd84c0()` and `0xd6850(addr)` (party-side bookkeeping, not read), and with dvar coop_autoLaunch `0xd7a50` (sets the launch flag 0x1405dd953).
- Launch: `0xd74a0` (the party frame, per engine frame): the host launches when the launch flag 0x1405dd953 is set, `0x22e790() == 0` (0x1905300 clear = ack received), the party has exactly 2 present members (`0x236820(party)` counts the members flagged at party+0x70 and +0xa8) and `0xd6810()` is false: it calls **`0x22e610`** (broadcast OOB "coopLaunch" to all non-local used slots: player count byte, coop_mapName, serverId long, coop_start, seed long 0x540b5c, member settings, ...) and **`0x22e1a0(0)`**: `cls[0].state = 3`, `0x22e4d0` (reset: 0x192d8f5 = 0, 0x192d7e0 / e1 = 0, message cleared, and in coop 0x192d7e3 = 1, cl_paused := 1 if it was 0, both slots' loaded flags/times := 0), party member dvars, measured bandwidth (0xd6760) -> coop_maxFramesPerPacket / min- and max-FramesClientCanBeBehind / coop_maxPacketUserCmds / cl_maxpackets, then console `map <coop_mapName>`.
  Partner `0x22d2e0` (OOB "coopLaunch", needs coop and `cls.state == 0`): reads the same values, sets `0x1919314`, mapName, `0x19ada44`, `0x540b5c`, member settings, then fills every unused slot with a fake `type 4, ip 127.0.0.1, port 100+i` address and sets its own launch flag (`0xd7a50`) so that its 0xd74a0 runs 0x22e1a0.
- Map load (SV_SpawnServer rva 0x2326a0 / 0x231790): the serverId is bumped (`+0x10`, high nibble never 0) on both machines from the same start value; `0x22f2d0` (called on load when sv_running was 0, and from 0x22c970) **resets the whole coop state**: ring frameNos := 0, head := 0, executing := 0, `0x192d7e4` / `0x192d8f9` := 0, acks := 0, `0x192d8f4 := 0`, `0x192d904 := 1`, and calls `0x22e4d0` (waiting flag set again); SpawnServer sets `cl_paused := 1` near its end (call `0x14026e020(0x13260e8, 1)`).
  `0x171370` (G_InitGame): `DAT_1403c3eb0 = DAT_141919314` in coop (1 otherwise), `0x22dec0` (hash on); `0x22d040` (0x231790 calls it): clears the 2 client_t, for each slot in use sets `+0x1670` gentity, `0x232180` init, `state = 4`, and `+0x1210 = 4` plus the name for `index < 0x1919314`; 0x231790 then runs `0x22cf90` for every client in 0..DAT_1403c3eb0-1 with state 4 (ClientBegin 0x15e350), and `0x171800` (runs `0x15e310(i)` = ClientConnect-like for every slot in use, then one game frame).
- **coopLoaded** exchange: partner -> host OOB "coopLoaded" (sent once from 0x22e920 on the partner when it first runs after load). Host: in 0x22f990 the sender is matched to a slot with 0x1402028a0 (NET_CompareAdr: for type 2 always equal; for IP compares ip+port) and `slot[p].+0x2644 = 1`, `+0x2648 = now`. Host-local slots are flagged by 0x22e330 without any message. The host's sim runs only after all used slots are flagged (cl_paused := 0 in 0x22e920), or after the timeout path above.
- **coopRestart** (fast_restart sync): `0x22f7b0` (called from 0x22c970 on a restart) sends OOB "coopRestart <serverId> <seed>" to the non-local slots; the partner (0x22f990) executes console `fast_restart` if the serverId matches and 0x192d7e1 is clear (it sets `0x540b5c = seed`).
  **coopDis**: `0x22ff60(text)` (disconnect everything): sets coop_validateData 0, OOB "coopDis <text>" to non-local slots (`0x22de10`), `0x192d8f8 = 0`; the receiver (0x22f990) sets 0x192d7e0 and the message.
  **coopReject** `0x22dc90` on the partner: coop flag off, "coop" dvar 0.

## 5. Address matching and the loopback: what a second LOCAL slot runs into (OBSERVED unless marked)

- `0x202970(a, b)` (used by 0x230020 / 0x2308a0 to find the slot of an incoming packet): same type required; for types 0 and 2 it returns `a.port == b.port`; for type 4 (IP) it compares only the 4 ip bytes (the port is NOT compared); type 5 compares 10 bytes.
  `0x1402028a0(a, b)` (NET_CompareAdr, used by 0x22e060 and by the coopLoaded / coopAcceptAck / coopDis handlers): type 2 -> always equal, type 4 -> ip and port.
  So two loopback slots are told apart for incoming packets only through the port of `from`, and only by the packet router, not by the OOB handlers (irrelevant for a session without a remote partner).
- The loopback layer (rva 0x202db0 NET_SendPacket / 0x202a60 NET_GetLoopPacket; taken from docs/native-splitscreen-plan.md, not re-read by me): a client send (sock 0) is tagged with the sender's `sock`, and the server read returns that tag as `from.port`.
  The netchan of slot p carries `sock` (+4): Netchan_Setup(0, ...) for the host's own slot 0 (0xd25e0), Netchan_Setup(1, ...) for remote slots. INFERRED: a second local client cannot be told apart by `from.port` without a hook on the loopback or without bypassing it (tag 1 is the server side of the same API).
- `0x1400cd340(localClientNum)` builds the packet from `cl[localClientNum]` data (usercmd ring rva 0x533d20 + n*0xce9c [128 x 0x40], cmdNumber rva 0x535d20 + n*0xce9c, packet history 32 x 12 bytes at rva 0x5409dc + n*0xce9c, reliable ring: seq rva 0x545844 / ack 0x545848 / strings 0x54584c + (k&0x7f)*0x40, stride 0x3218 per local client)
  but ALWAYS transmits on `&slot0.netchan` (rva 0x540ba0) and indexes its packet history with slot 0's outgoingSequence. It gates on `cls state[n] (rva 0x533cb8 + n*0x10) > 2`, `cl_paused == 0`, and `state == 6 || 1 s since the last send` (`_DAT_1405431d8`, shared by all local clients).
- `0x1400cd300(n, ack)` (reliable ack, called by the executor for every local slot) writes `clc[n].ack` only if `cls state[n] > 2`.

## 6. The non-coop "local players" branch the same code already contains (OBSERVED code; that it works is INFERRED)

Almost every slot loop in the coop code and in the game module has the form `coop ? slot[p].type != 0 : p < DAT_1403c3eb0` (in use) and `coop ? slot[p].type == 2 : p < DAT_1403c3eb0` (local), with the local client number `coop ? slot[p].port : p` (examples: 0x15ce10, 0x22d040, 0x22e330, 0x22f400, 0x2302d0, 0x171800, 0x2326a0, 0x194740, 0x18dea0, 0x133850).
That is a complete second configuration: **N local players and no network at all** (what a console split screen needs; the strings so_character_select_splitscreen / so_character_select_coop are used by 0xd7a60).
In that configuration (coop byte 0):
- `0x15ce10` -> `0x1400d02a0(i)` for i < N: runs the local client's reliable commands from `clc[i]` (calls `0x166710(i, string)` directly) and applies up to 32 new usercmds from `cl[i].cmds` (those after `cl[i].+0x535d24`) with **`0x22d000(i, &cmd)`**. No packets, no frames, no slots, no coopLoaded, no stall logic (0x15c9e0 returns at once, 0x233190 never holds).
- `0x22d040` / `0x231790` create client_t[i] (state 4) and ClientBegin for i < N; `0x171800` runs `0x15e310(i)` for i < N.
- **The only thing that pins N to 1 is the store in G_InitGame: `c7 05 <rel32> 01 00 00 00` at rva 0x1714a6 (imm32 at rva 0x1714ac) = `DAT_1403c3eb0 = 1`, replaced by `= DAT_141919314` when the coop byte is set** (bytes checked with a memory read). The `IsSplitScreen()` builtin (rva 0x194720) calls 0x222140(0) = pushes false, as the plan says.
- Not checked: whether the rest of the PC build (cgame, scripts, party, profile code) survives N = 2 without the coop byte; the mod's scripts and C code take `coop == 1` as the signal.

## 7. What would have to be faked so the host simulates both players with player 1's usercmds from a LOCAL source

Two designs, both host side only. Design X keeps the coop byte (everything the mod and the scripts already assume stays as it is); design Y is the engine's own local-players branch of section 6.

### Design X: coop byte 1, both slots local (INFERRED to work; every step is backed by the OBSERVED code cited)

1. **Session state** (what 0xd25e0 + 0x22d830 + 0x22d1c0 would have produced for a partner): coop byte 0x52fb5c = 1; isHost 0x1905302 = 1; dvar "coop" := 1 (`0x26d800(*(void**)0x1905308, 1)`, as 0x22d830 does after accepting; the scripts read it);
   slot 0 as 0xd25e0 makes it (type 2, ip 127.0.0.1, port 0, Netchan_Setup(0, ...)); **slot 1 (rva 0x5431f0): type `*(int*)(rva 0x543200) = 2`, ip 127.0.0.1, port `*(ushort*)(rva 0x543208) = 1` (= local client number 1)** and, if any code should be able to transmit on it, Netchan_Setup(0, slot1, addr, 0, slot1+0x638, 0x1000, slot1+0x1638, 0x1000, -1);
   `DAT_141919314 = 2` (G_InitGame turns it into DAT_1403c3eb0 = 2); the serverId needs no value (the host picks it); 0x1905300 stays 0 (it is set only while a remote ack is pending, and 0xd74a0 will not launch while it is set).
2. **Party**: a second member present (party struct pointed to by [0x1405dd958]: member present bytes at party+0x70 / +0xa8; `0xd7220(localClient, name, xuid, 0, 1, 3, ?, &addr{2,127.0.0.1,port 1}, ?)` fills it; then `0xd7c70(1, v)`, `0xd7b30(1, v)` like 0x22d1c0), so that `0x236820(party) == 2` and the party frame 0xd74a0 launches. The launch flag is `0x1405dd953 = 1` (`0xd7a50`). `0x22e610` (the coopLaunch broadcast) sends to nobody (no non-local slot) and `0x22e1a0(0)` loads the map. The xuid of member 1 must differ from member 0 (0x22d830 rejects a duplicate with COOP_FULL; later code looks members up by xuid, 0x28fdf0 / 0x28f870).
3. **Do not let the frame stall logic see "no remote client"**: hook `0x22ec30(int limit)` to return 0 when no used slot has a type other than 2 (otherwise 0x192d8f4 becomes 1 after about 10 frames, see section 4).
4. **Player 1's usercmds, minimal form:** after the host's frame has executed (right after `0x15c9e0` returns inside state 1 of 0x173e90, before 0x15cbb0) call **`0x22d000(1, &cmd)`** once per step (or once per local cmd) while `level.time > 250`, `client_t[1].state == 4` and `client_t[1].+0x1210 == 4` (both set by 0x22d040 at map load for every slot in use). `cmd` is a 0x40-byte usercmd; its layout (OBSERVED from the delta reader 0x201d30): +0 serverTime, +4 buttons (23 bits), +8 / +0xc / +0x10 the three view angles (16 bits each), +0x14 / +0x16 / +0x18 three u16 ids (8-bit on the wire; +0x14 is the weapon, +0x18 the offhand weapon according to the baseline builder 0x2019b0), +0x1a / +0x1b / +0x1e / +0x1f bytes, +0x1c / +0x1d signed 7-bit values (move forward, right), +0x20..+0x34 six 32-bit values (floats), +0x38 / +0x3c / +0x3d bytes.
   unknown: what the mid fields mean, and how ClientThink_real / pmove uses cmd.serverTime (stock Q3: msec = cmd.serverTime - ps.commandTime). Copy the shape of player 0's cmds from a log of `client_t[0].+0x1630` before inventing values.
   Player 1's reliable client commands, if needed, go straight to `0x166710(1, "string")` (that is all the executor does with them).
5. Nothing else on the host is needed for player 1: no packet, no frame section, no ack. Keep `cls state[1]` at 0 so that 0x15ce10 -> 0x1400cd260(1) -> 0x1400cd340(1) does nothing (both test `cls state > 2`).
6. **The engine-conform form of the input** (INFERRED): let local client 1 build its packet with `0x1400cd340(1)` from `cl[1]` (needs `cls state[1]` = 6, `cl[1].cmds` filled, `clc[1]` reliable ring) and deliver it to the host by calling `0x22f0d0(client_t[1], msg)` yourself with the packet positioned after the two bytes 0x1d, 0, or by hooking Netchan_Transmit (rva 0x203200) when it is called from 0x1400cd340 for local client 1 and feeding the bytes (without the 4-byte sequence the transmit prepends) to 0x22f0d0. Reason for a hook instead of the loopback: the packet has no sender field, the sender is derived from `from.port` / the slot (section 5). 0x22f0d0 only accepts a packet while 0x192d8f7 is set (between 0x22e040 and 0x22e050 around the game frame), so call it from a hook that runs inside 0x15c9e0 after 0x22f830.
7. Per-step flow stays: 0x15ce10 (sends local packets) -> 0x15c9e0: 0x22f830, pump, 0x22f0d0 for each packet, 0x22f400 (skips local slots), 0x2302d0 (executes) -> your injection.

### Design Y: coop byte 0, N = 2 (INFERRED; fewer moving parts on the host, more unknowns elsewhere)

- Change the immediate at rva 0x1714ac from 1 to 2 (so DAT_1403c3eb0 = 2), make `IsSplitScreen()` (0x194720) return true if the scripts need it, and give the engine its two local clients: `cl[1].cmds` / `cmdNumber` filled each client frame, `clc[1]` for reliable commands.
  0x15ce10 -> 0x1400d02a0(1) -> 0x22d000(1, cmd) does the rest. `DAT_141919314` and slot 1 are not needed. The `coop` dvar stays 0 (the scripts use `IsSplitScreen() || coop == 1`) and every `DAT_14052fb5c` test in the engine takes its non-coop branch (about 100 readers; I did not check them one by one).

## 8. What a second local client needs from this part (plain words)

1. **The host already has two real players and already has an input path per player number.** Player n is client_t[n] (rva 0x19ada98 + n*0x1678) and the game entity g_entities[n]. A player moves when `client_t[n].+0x1630` holds a usercmd and `0x15bd80(n)` runs. The one function that does both is `0x22d000(n, &usercmd)`.
2. **In co-op the engine does not call it directly. It routes every player's cmds through a frame** (one per 50 ms step, numbered by level.time): the local client packs its new cmds into a packet, the host collects the packets of all clients into one frame, sends it to the remote machine, and executes it itself with `0x2302d0`. The host's own player goes through the same path over the loopback. A second local client would be a second packet source for the same frame, or can skip the frame and use `0x22d000(1, ...)`, because nobody else needs to see the frame.
3. **Slot 1 must exist and be "local".** The slot table (rva 0x540ba0) is the list of players: type 2 = this machine, type 4 = remote. For a one-machine session slot 1 is a second type-2 slot whose port is the local client number 1. Everything that decides "who is local" (game scripts included) reads it. No coopConnect / coopAccept / coopAcceptAck / coopLoaded / coopLaunch message is needed: local slots are flagged as loaded automatically (0x22e330), the launch broadcast goes to non-local slots only, and the party/launch part is done by creating the second party member and setting the launch flag.
4. **One engine function must be told that there is no remote partner:** `0x22ec30` takes an empty remote side for "10 frames behind" and would freeze the simulation after half a second and then raise COOP_CLIENTDROP.
5. **The packet path has the host's own player 0 hard-wired:** the client sender always uses slot 0's netchan and the loopback marks packets only with the sender's socket number, so a second packet source cannot share it unchanged; either call `0x22f0d0` directly or skip packets.
6. **Everything a second local client needs on its own side (cl[1] cmd ring, clc[1], cls[1], the second snapshot/cgame context) is outside this part;** the host side only reads `client_t[1].+0x1630` (and the reliable commands via `0x166710`). The arrays `cl[n]` (stride 0xce9c), `clc[n]` (stride 0x3218) and `cls[n]` (stride 0x10) are real per-local-client arrays indexed by the local client number (the code multiplies by these strides everywhere); how many entries they have I did not determine.

## 9. Unknowns and things I did not read

- unknown: the number of entries of `cl[n]` / `clc[n]` / `cls[n]` (the loops I saw never go beyond 2; the coop code has only 2 slots).
- unknown: the meaning of client_t.+0x1210 == 1 (checked in several places as "ignore this client"; no writer found by the references I ran) and of slot bytes +0x2638 / +0x263c / +0x264c.
- unknown: how pmove uses `usercmd.serverTime` for a cmd injected by hand (Design X step 4).
- not read: `0x232180` (client_t init), `0x15e350` / `0x15e310` (ClientBegin / ClientConnect), `0x1400d6850`, `0x1400d84c0`, `0x1400d7490`, the snapshot builder `0x232d80`, the loopback code itself (`0x202db0`, `0x202a60`, `0x2003f0`), the client frame (`0xd5890`), the body of `0x22c340` / `0x22c970` (load/restart state machine).
- INFERRED only (no run): that a host with two type-2 slots reaches `0x2302d0` and executes frames at all (the code path is clear, but the PC game never ran this configuration); that `0x22ec30` is the only blocker (the other stall inputs, 0x192d7e3 and cl_paused, release by themselves according to 0x22e920).
- The names "ClientThink", "SV_ClientThink", "G_RunFrame", "Netchan_*", "MSG_*" are my analogue names from the Quake 3 / IW lineage, chosen from behaviour; the decompiler names are FUN_/DAT_.

## 10. Call / hook cheat sheet (rva, my signature, use)

| rva | signature (x64, rcx, rdx, ...) | use for a local second player |
|---|---|---|
| 0x22d000 | `void (int clientNum, const usercmd_t* cmd)` | apply one usercmd to player n: copy to client_t[n].+0x1630, tail-jump to 0x15bd80 |
| 0x15bd80 | `void (int clientNum)` | ClientThink of player n (reads client_t[n].+0x1630 through 0x2316e0) |
| 0x166710 (thunk 0x22d150) | `void (int clientNum, const char* cmd)` | the game's ClientCommand dispatcher (reliable client commands) |
| 0x15c9e0 | `void (void)` | the coop frame hook, called once per game frame (only caller 0x173e90 state 1); wrap it to run code right after the frame executed |
| 0x22ec30 | `bool (int limit)` | "partner is `limit` frames behind"; hook to return 0 when no used slot has a type other than 2 |
| 0x22f830 / 0x22efd0 / 0x22f400 / 0x2302d0 | `void()` / `void()` / `void()` / `void (int serverId, int frameNo)` | begin frame / end frame (send + execute) / send to remote slots / execute |
| 0x1fd7c0 | `void (void)` | network pump of the game frame (UDP + server loopback) |
| 0x22f0d0 | `void (client_t* c, msg_t* packetAfterHeader)` | append one client packet to the head frame (host only, needs 0x192d8f7 or 0x192d8f4 set) |
| 0x1400cd340 | `void (int localClientNum)` | build + transmit the local client's packet (always on slot 0's netchan) |
| 0x203200 | `bool (netchan* nc, int len, void* data)` | Netchan_Transmit (prepends the 4-byte sequence) |
| 0x202ff0 | `bool (netchan* nc, msg_t* m)` | Netchan_Process |
| 0x22e1a0 | `void (int localClient)` | local launch (state 3, reset of the wait flags, `map <coop_mapName>`) |
| 0xd74a0 | `void (void)` | party frame that decides the launch (needs launch flag 0x1405dd953, 2 members, 0x1905300 == 0) |
| 0xd7220 | `void (int lc, const char* name, u64 xuid, u8 isHostMember, int index, int ?, ?, netadr*, int ?)` | add a party member |
| 0x22d040 | `void (void)` | create client_t[0..1] for the slots in use (called by the server spawn) |
| 0x22cf90 | `void (client_t*)` | ClientBegin trigger |
| 0x171370 | `bool (...)` | G_InitGame; `DAT_1403c3eb0` store at 0x1714a6 (imm32 at 0x1714ac) |
| 0x22e330 / 0x22e920 / 0x22e4d0 / 0x22f2d0 | `void()` | mark local slots loaded / wait-and-stall maintenance / start waiting / reset the whole coop state |
| 0x26d800 | `void (dvar*, value)` | set a dvar (0x22d830 uses it for the "coop" dvar: `[0x1905308]`) |
