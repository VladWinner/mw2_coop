# Coop client side: how a machine gets its own player's input to the host (and how frames come back)

64-bit `iw4sp.exe`, rva = address - 0x140000000. Agent `clientsend`. Read-only Ghidra headless analysis; nothing was run, nothing edited.
Every statement is marked OBSERVED (seen in the decompile or disassembly) or INFERRED. `unknown` means not found. Role names are mine; the
decompiler names are FUN_/DAT_. Raw decompiles are in `local/scratch/clientsend/` of the outer workspace (not committed).

## 0. The whole picture

1. OBSERVED. Usercmds are CREATED once per render frame: `0x1fbde0 -> 0xd5b00 -> 0xd5890 -> 0xbca00 (cgame draw) -> 0xcf800 -> 0xd03a0`; they go into a 128-entry ring.
2. OBSERVED. They are SENT from the simulation tick (50 ms of sim time), not from the render frame: `0x1fbde0 -> 0x233190 -> 0x233400 -> 0x234260 -> 0x173e90 (state 1) -> 0x15ce10 -> 0xd02a0 -> 0xcd260 -> 0xcd340`.
   The same tick can also be started early, in spare time inside the cgame draw (`0xbca00 -> 0x233de0 -> 0x173e90(1, deadline)`), same code, only when dvar ptr `0x19c5ef8` is non-zero (default unknown).
3. OBSERVED. The packet sender `0xcd340(localClientNum)` writes one bit-packed message and sends it with `Netchan_Transmit` on the CONSTANT netchan `rva 0x540ba0` (= coop slot 0), for any client number.
4. OBSERVED. The coop slot table (`rva 0x540ba0`, 2 slots, stride 0x2650) is symmetric: slot i = PLAYER i. A slot whose address type is 2 (loopback) is a player that is LOCAL to this
   machine, and the 16-bit port of that address IS THE LOCAL CLIENT NUMBER. Slot 0 is always the connection used for sending (netchan to the host, or the loopback to ourselves if we are the host).
5. OBSERVED. Host: its own client sends over the loopback ring (client-to-server ring, tag = netchan sock = 0) and the same tick reads it back. The partner sends over UDP.
6. OBSERVED. The host does NOT run client packets directly: it builds a FRAME per tick (header + one record per client: "select client slot", that client's reliable commands, that
   client's usercmds), sends the newest N frames to every REMOTE slot (never to loopback slots) and executes the frame itself. The partner executes the same frames from a 20-entry ring.
7. OBSERVED. Lockstep: the partner can only run sim time T if it holds the frame stamped T. The host never waits for fresh partner input; it only stalls when the partner is too many frames behind.
8. OBSERVED. The engine's own loops already say "for slot in 0..1: if slot.type == 2 then local client = slot.port" in at least 6 places: it takes the port as the local client number in 0x15ce10, 0x22e7a0 and the tail of 0x2302d0, and tests type == 2 / != 2 in 0x22efd0, 0x22f400, 0x22e330, 0x22ec30, 0x22dd10. The code is written for two local players on one machine.
9. OBSERVED. What is missing is storage: the arrays indexed by localClientNum (cl, reliable-command ring, cls state, input state, outPackets) exist only once; element 1 lands on other data.
10. OBSERVED. `0xcd340` hardcodes slot 0's netchan, so a second local client cannot use it unchanged even with real arrays.
11. OBSERVED. The string "Invalid co-op packet - references invalid clientNum %i" exists (rva 0x351300, used in `0x2302d0`, the frame executor). The earlier searches missed it because the text ends in `%i`.
12. INFERRED. In a one-process two-local-client setup every "remote" mechanism (UDP, send frames, fast forward, back-off, ack/stall) is idle by construction; only the loopback path matters.

## 1. Data structures

### 1.1 Coop slot (`rva 0x540ba0 + slot * 0x2650`, 2 slots, table ends at 0x545840)
| offset | what | evidence |
|---|---|---|
| +0x000 | the netchan (0x638 bytes): +0 outgoingSequence, +4 sock, +8 dropped, +0xc incomingSequence, +0x10 remoteAddress (+0x10 type, +0x14 ip, +0x18 port u16; 20 bytes), +0x24 fragmentSequence, +0x28 fragmentLength, +0x30 fragment buffer ptr, +0x38 its size, +0x3c unsentFragments, +0x44 unsentLength, +0x48 unsent buffer ptr, +0x50 its size | Netchan_Setup 0x203190, Netchan_Transmit 0x203200, Netchan_Process 0x202ff0 |
| +0x638 / +0x1638 | two 0x1000-byte fragment buffers (rva 0x5411d8, 0x5421d8 for slot 0) | Netchan_Setup calls |
| +0x2638 | int: time (cls.realtime, rva 0x548bbc) the last packet was written | `_DAT_1405431d8` in 0xcd340, 0xcd260, 0xd23c0, 0xd25e0 |
| +0x2640 | int: ack = latest frame time the peer reports to have received (host side reads it from the partner's packet) | written 0x22f0d0, read 0x22ec30, 0x22f400 |
| +0x2644 / +0x2648 | byte "coopLoaded received" / its time | 0x22f990, 0x22e330, 0x22e920 |

Address type values seen: 0 free, 2 loopback (= local player), 4 IP (= remote).
- Host: slot 0 = {type 2, 127.0.0.1, port 0}; slot 1 = {type 4, partner IP} (OBSERVED in the plan doc's runtime look, and in 0xd25e0 / 0x22d830).
- Partner: slot 0 = {type 4, host IP}; slot 1 = {type 2, port 0}. INFERRED from the code of 0x22d460, which sets `slot[assignedIndex].type = 2` and `slot[assignedIndex].port = localClientNum`; the index is the byte the host sent in "coopAccept" (0x22d830 sends the free slot index, 1). Not seen at runtime.

### 1.2 Server-side coop client array (`rva 0x19ada98 + slot * 0x1678`, 2 elements, end 0x19b0788)
+0x0000 int `state` (OBSERVED uses: `> 2` gates sending frames in 0x22f400 and local execution in 0x22efd0, `>= 2` = a client exists in 0x22ed40, `< 4` makes a frame record's slot select invalid in 0x2302d0; Quake-style connected/primed/active meaning INFERRED),
+0x1210 int second state (OBSERVED: 0 = slot unused (0x230020 skips it), 1 = ignore its packets (0x230020, 0x22efd0, 0x2306e0 stop), 3 = activate on its first usercmd (0x22cf90), then 4 = running ClientThink),
+0x1218 reliable-command acknowledge (last client command number executed), +0x121c last command string (0x400),
+0x162c last packet time (ms), +0x1630..0x166f last usercmd (0x40 bytes). How +0 and +0x1210 relate: unknown. Slot i here corresponds to coop slot i and to party member i (party/entity array at rva 0xd9e070, stride 0x2a0, byte +0xb0 = in use).

### 1.3 Per local client arrays the send path indexes by `localClientNum` n (all have ONE element in this exe)
- cls connection state: int at `rva 0x533cb8 + n*16` (6 = in game; the guards need >= 3).
- cl block, stride 0xce9c: usercmd ring 128 x 0x40 at `0x533d20 + n*0xce9c`; `cmdNumber` int at `0x535d20 + n*0xce9c`; int at `0x535d24` (last cmd number handled, non-coop path); base-usercmd source at `0x535d38`; flags at `0x535d44` (&0x80000); view angle floats at `0x533d14` / `0x533d18`; outPackets[32] x 0xc {cmdNumber, serverTime, realtime} at `0x5409dc`.
- reliable client command ring, stride 0x3218: `0x545844` sequence, `0x545848` acknowledge, strings 128 x 0x40 from `0x54584c`.
- input state (0xd03a0): `0x52fbc0 + n*0x21c` (buttons, sticks), dvar ptrs `0x52fb90/98/a0`, scalars `0x548bb0` (frame msec), `0x548bb4` (time), `0x548bbc` (cls.realtime), `0x52fb70/74`.
- PROOF the arrays are folded: `cl[1]` usercmd ring would start at rva 0x540bbc, which is inside slot 0's netchan (remoteAddress field); `cl[1]` is not backed by storage of its own.
- Singletons used by the send path: netchan slot 0 (0x540ba0; unsentFragments at 0x540bdc), dvar ptrs 0x540b78 and 0x540b80 (cl_packetdup / cl_maxpackets-like, names INFERRED from use), 0x1919340 (coop_maxPacketUserCmds), 0x1919318 (coop_validateData),
  client serverId copy `0x5dac64` (written by 0xcd330, called from 0x22c970 and 0x231790), client ack-time copy `0x5dac60` (written by 0xd4be0, called only from 0x15ce10), coop flag `0x52fb5c`, host flag `0x1905302`, partner-connecting flag `0x1905301`.

### 1.4 Frame ring (host builds, partner receives)
- 20 entries (index mod 0x14). Entry table at `rva 0x192d380`, stride 0x38: +0 int frame time (0 = empty), +4 int serverId, +8 msg_t (0x30 bytes) whose data buffer is `0x1919380 + idx*0x1000`.
- write index `0x1919378`, read index `0x191937c` (set by the executor to the entry it found, cleared by 0x22f2d0). The frame the host is building = entry [0x1919378].
- msg_t layout seen: +0 overflowed, +8 data ptr, +0x18 maxsize, +0x1c cursize, +0x24 readcount, +0x28 bit position.

## 2. Per-tick order (OBSERVED from 0x173e90 case 1, 0x15c9e0, 0x234260)
Before the tick, `0x234260`: if sim time > 250 ms set `0x192d8f8` ("frame processing on") and `0x192d8f7` ("accept client packets") via 0x22e780 / 0x22e040; run `0x173e90(0,0)`; clear both (0x22e460 / 0x22e050).
Inside `0x173e90` state 1 (coop):
1. `0x15ce10`: first stores the ack time `0xd4be0(0x22fff0(serverId))` (time of the newest ring frame with a matching serverId). Then for slot 0..1: if party member i is in use AND slot i type == 2 then `0xd02a0(slot.port)` -> `0xcd260` -> `0xcd340` (send).
2. `0x15c9e0`:
   - host: `22e040; 22f830 (begin frame); 1fd7c0 (drain network); 22efd0 (finish frame); 22e050`.
   - partner: if "frame processing on" and not dropped (0x22f820): `0x22e920` (coop upkeep) then `0x2302d0(serverId, simTime)` = execute the frame stamped simTime.
3. `0x15cbb0`, `0x15cbf0` per active slot, `0x170e60`, then states 2..9 (server-side per-entity work; not analysed).
In NON-coop (flag 0x52fb5c == 0), `0x15ce10` is called at state 8 instead and `0xd02a0` takes its single-player branch (direct calls `0x22d000` per new usercmd and `0x166710` per reliable command; no packet).

## 3. Functions

### 0xcd340  coop_WritePacket(localClientNum n)  (the sender)
- args: `int n`. Callers: `0xcd260` (normal) and `0x22e7a0` (resend while waiting, partner). OBSERVED.
- Guards: cls state[n] >= 3 and dvar ptr `0x13260e8` value == 0, else returns without doing anything.
- Builds the base usercmd `from` with `0x2019b0(rva 0x535d38 + n*0xce9c, buf)`; msg init `0x2016c0(msg, buf[0x1000])`.
- Header: WriteByte 0x1d; WriteByte **0** (literal, verified in disassembly: `XOR EDX,EDX`); WriteLong `[0x5dac64]` (serverId); WriteLong `0xd3140()` (= `[0x5dac60]`, ack time); WriteByte `0x22e0f0()` (validate mode 0/1/2 from coop_validateData); WriteByte 0 (placeholder, patched at the end with `bitpos & 7` = valid bits in the last byte).
- Reliable commands: for seq = ack+1 .. sequence (`[0x545848+n*0x3218]+1 .. [0x545844+n*0x3218]`): WriteBits(2, 3 bits), WriteLong seq, WriteString ring[seq & 0x7f] (0x40 each), WriteByte 0x5b.
- Usercmds: count = min( cmdNumber - outPackets[(outgoingSequence - dvar@0x540b78.value - 1) & 31].cmdNumber , coop_maxPacketUserCmds ). If count >= 1: WriteBits(1, 3), WriteByte 0x51, WriteByte count, then the newest `count` cmds oldest first, each `0x2021e0(msg, from, to)` (delta usercmd, no key: serverTime delta as 8 or 32 bit, then an "all unchanged" bit or per-field changed bits), `from` = the previous cmd (first from = base); WriteByte 0x47.
- Trailer: WriteBits(4, 3) (EOF); patch the placeholder; overflow -> `0x1fb5f0(1, "@EXE_SERVERCOMMANDOVERFLOW")`.
- Bookkeeping: `[0x5431d8] = cls.realtime (0x548bbc)`; outPackets[outgoingSequence & 31] = {cmdNumber, serverTime of the newest cmd, realtime}.
- Send: `0x203200(netchan 0x540ba0, cursize, buf)` = Netchan_Transmit; then `while ([0x540bdc]) 0x203300(0x540ba0)` pumps the remaining fragments. The netchan is the constant slot 0 for any n. OBSERVED (decompile + disassembly).
- Packets shorter than 0x1ff2 bytes (loopback) or 0x514 bytes (other) go out unfragmented as `[outgoingSequence long][payload]` through `0x200b60` (NET_SendPacket with optional lag simulation) -> `0x202db0` (NET_SendPacket).

### 0xcd260  coop_SendCmdGate(n)
- Runs only if coop flag 0x52fb5c is set and cls state[n] >= 3 and (state[n] == 6 OR more than 999 ms since `[0x5431d8]`; i.e. while connecting it re-sends about once a second).
- If slot 0 address type != 2 (we are the PARTNER talking to a remote host): rate limit, skip when `cls.realtime - outPackets[(outgoingSequence-1)&31].realtime < 1000 / [0x540b80].value` (cl_maxpackets-like). Over loopback (host): no limit, every call sends.
- Then `0xcd340(n)`.

### 0xd02a0  CL_SendCmd(n)   (called only from 0x15ce10)
- coop: `0xcd260(n)`. Non-coop: delivers pending reliable commands to `0x166710(n, str)` and up to 0x20 new usercmds to `0x22d000(n, cmd)`, then stores `[0x535d24 + n*0xce9c] = cmdNumber`.

### 0x15ce10  coop_SendAllLocal()  (once per tick, state 1)
- See section 2. The local client number for slot i is `*(u16*)(0x540bb8 + i*0x2650)` (the port) when coop, else i. Only for party members in use (byte +0xb0 of the 0x2a0-stride array).

### 0x173e90  sim tick state machine (called `(0,0)` from 0x234260, `(1,deadline)` from 0x233de0 and 0x234140, also from 0x171800 and 0x233a30)
- State in `0xd97bf8` (1..9), resumable. Coop state 1: 0x15ce10, 0x15c9e0, 0x15cbb0, 0x15cbf0 per active slot, 0x170e60. Later states: per-entity script/frame work (0x14019efd0, 0x223ac0, 0x2311d0 ...). Not analysed (server side).
- `0x174d00` (via `0x234400`) advances the sim clock `[0xd96ec8] += 0x32` and `[0xd96ec4]++` at the start of a tick; `0x171360()` returns `[0xd96ec8]`. A second clock `[0x19c3868]` (sv time) is also advanced by 0x32 in `0x233400`. Their exact relation: unknown (they move together).

### 0xcf800  CL_CreateNewCommands   (render frame, from 0xbca00)
- Returns early in some sv-running/paused states (dvars `0x13260c0`, `0x13260e8`, `0x5dacc0`); sets `[0x548bc1] = 1`; msec = `[0x13260f0] - [0x52fb74]` capped at 200 -> `[0x52fb70]`.
- `cmd = 0xd03a0(buf, 0)` (the local client number is the literal **0**), `cmdNumber = ++[0x535d20]`, copies 0x40 bytes to `ring[0x533d20 + (cmdNumber & 0x7f)*0x40]`. OBSERVED; no n parameter.

### 0xd03a0  CL_CreateCmd(out, n)
- Zeroes 0x40 bytes; if `0xd19a0(n, 0x10)` is 0 (the key-catcher / menu-up test; flag word at rva 0x533cb0): reads `0x52fbc0 + n*0x21c` (via the axis helper `0xcfbc0`), updates view angles `[0x533d14 / 0x533d18 + n*0xce9c]`, handles `cl flags & 0x80000` (variant `0xcff70`), `0xce200`, `0xcf900`, `0xcfcf0(cmd, serverTime)`, `0xcf340` (finalise). Else only the stance/weapon button bits from `[0x533cdc + n*0xce9c]`. Button/axis details: not needed here.

### 0xd2110 / 0xd21a0  CL_AddReliableCommand(n, str)
- OBSERVED. Ring `[0x545844 + n*0x3218]` (seq++), string 0x40 into `0x54584c + n*0x3218 + (seq & 0x7f)*0x40`; error "EXE_ERR_CLIENT_CMD_OVERFLOW" if seq - ack > 0x80. Callers pass the literal 0 (for example 0xd5890).
### 0xcd300  setAck(n, v)
- `[0x545848 + n*0x3218] = v` if cls state[n] > 2. Called at the end of 0x2302d0 for every loopback slot with `v = serverClient[slot].ack (+0x1218)`; also from 0xb6050 (not examined).

### Network layer (OBSERVED)
- `0x203190 Netchan_Setup(sock, chan, adr, qport, buf1, size1, buf2, size2, ..)`: zero 0x638 bytes, chan+4 = sock, adr -> chan+0x10.., chan+0 (outgoingSequence) = 1.
- `0x203200 Netchan_Transmit(chan, len, data) -> bool`: len > 0x4000 error; if len < 0x1ff2 (loopback) / 0x514 (else): msg = [outgoingSequence++ long][data] and `0x200b60(chan.sock, len, buf, &chan.remoteAddress)`; else it sets up fragments (chan+0x3c = 1) and calls `0x203300` (TransmitNextFragment).
- `0x200b60`: NET_SendPacket with the lag simulator (dvars around 0x1385530), otherwise `0x202db0`.
- `0x202db0 NET_SendPacket(sock, len, data, to)`: to.type 2 -> loopback: sock < 1 (client) writes ring `0x1391690 + (w&63)*0x2008` (counter `0x1411894`, length at +0x2000 of the entry in the side array, tag = sock); sock >= 1 writes ring `0x14118a0 + to.port*0x18068 + (cnt%12)*0x2008` (counters 0x1429900/04); other types -> `0x293b70` (UDP).
- `0x202a60 NET_GetLoopPacket(sock, &from, &msg)`: sock == 1 reads the client-to-server ring (from = {type 2, port = tag}); other sock reads `ring[sock]`. `0x202a30` = wrapper (lag variant `0x2003f0`).
- `0x202970 NET_CompareAdr(a, b)`: same type required; types 0 and 2 (loopback) compare the 16-bit PORT; type 4 compares ip and port. So a loopback packet tagged k only matches the slot whose address port == k.
- `0x202ff0 Netchan_Process(chan, msg)`: reads seq (long), fragment header if bit 31 (start long, length short), reassembles via the chan+0x30 buffer, sets chan.dropped and chan.incomingSequence; returns 1 when a full message is ready (msg then positioned after the sequence).
- `0x293940` (thunk 0x202c10): `recvfrom` on the two UDP sockets (`[0x141bbf0d8]`, `[0x141bbf2d0]`); NOT the loopback.

### Packet pumps and dispatchers (OBSERVED)
- `0x1fd7c0` (coop only, flag 0x52fb5c): drains UDP via the thunk -> `0x230020`, then drains the loopback client-to-server ring (`0x202a30(1, ..)`) -> `0x230020`, only if sv running (`0x13260c0`). Called from 0x15c9e0 (host) after begin-frame.
- `0x1fbb80`: the same drain for other contexts; its coop part runs only when sv is NOT running.
- `0x2308a0`: UDP-only version of the dispatcher, called from the waiting loops (0x233190 idle path, 0x22e7a0, 0x22e920). Out-of-band packets (header -1) go to `0x22f990` (coopConnect / coopAccept / coopLoaded / coopDis / coopRestart / coopLaunch table).
- `0x230020(from, msg)`: connected packets: for slot 0..1 with serverClient `+0x1210` != 0, `NET_CompareAdr(from, slot.addr)`; on match `Netchan_Process(slot netchan, msg)`; if `+0x1210` != 1: stamp `serverClient.lastPacketTime`, read byte (must be 0x1d), read byte: 0 -> `0x22f0d0(serverClient, msg)` (client-to-host), 1 -> `0x22edd0` (host-to-client frames); no slot matches -> OOB "coopDis".

### 0x22f0d0  host_AppendClientRecord(serverClient*, msg)   (client packet -> current frame)
- OBSERVED. Only if host (`0x1905302`) and (`0x192d8f4` or `0x192d8f7`). Slot index = pointer difference / 0x1678.
- Reads: serverId (must equal `[0x19ada44]`, else dropped); ack long -> `slot+0x2640`; validate-mode byte (the host may switch coop_validateData to 1/2); bit-count byte.
- Appends to frame entry [0x1919378]: WriteBits(3, 3 bits), WriteBits(slotIndex, 1 bit), WriteByte 0x3d, the client packet's remaining bytes verbatim (last partial byte with `bits` bits), WriteByte 0x33. Overflow -> "Packet is %i bytes, unable to add %i more" (rva 0x351338). Then discards the rest of the message. Host only.

### Frame builder / sender / executor (OBSERVED)
- `0x22f830` begin frame: `idx = (idx+1) % 20`, init msg, `time = [0xd96ec8]` (1 if 0), validate mode as in 0x22e0f0; entry.time, entry.serverId = `[0x19ada44]`; header WriteLong serverId, WriteLong `[0x192d7e4]` (meaning unknown), WriteLong time, WriteByte mode.
- `0x22efd0` finish frame (host): `0x22f400` (send), then for the first slot with server `state (+0)` > 2, address type 2 and `+0x1210` != 1: stamp time and `0x2302d0(serverId, [0xd96ec8])` = execute locally; then `[0x192d8f4] = 0x22ec30(coop_maxFramesClientCanBeBehind)` (stall flag), if set record the stall start.
- `0x22f400` host_SendFrames: for each slot with address type != 0, server `state (+0)` > 2 and **type != 2** (remote only): n = `0x22f920(slot.ack)` (frames newer than the ack), at least 1, capped by coop_maxFramesPerPacket (`0x1919330`); packet = WriteByte 0x1d, 1, n, then oldest..newest: WriteShort length + frame bytes; copies the slot's netchan to the stack (0x638 bytes) and `Netchan_Transmit`s it, writes the outgoingSequence back; on failure "EXE_TRANSMITERROR" for a loopback slot, otherwise the remote is dropped (0x273710, type 0). Loopback slots are skipped, so a host with only local players sends no frames.
- `0x22edd0` partner_ReceiveFrames(serverClient*, msg)   (the 360 sub_8233BAF0 "receive coop frames" analogue). OBSERVED:
  - only when NOT host (`0x1905302 == 0`). n = ReadByte, accepted 1..20. `last` = time of the newest ring entry (matching serverId) else 0; if `last < simTime - 0x32` then `last = simTime - 0x32`.
  - for each of the n frames: len = ReadShort; start = readcount; serverId = ReadLong; ReadLong (skipped); time = ReadLong; ReadByte; if `serverId == [0x19ada44]` and `time > last` and the ring has room (`[0x191937c] != (w+1) % 20`): w++, entry.time/serverId set, msg initialised on its 0x1000 buffer, `len` bytes copied from `start`; `last = time`; clears the stall flags (`0x192d7e3`, dvar `0x13260e8`). readcount = start + len.
- `0x2302d0` EXECUTE FRAME `(serverId, time)`: finds the ring entry with exactly that time and serverId (else "Unable to find player data for server time %i, serverId %i\n", rva 0x3512c0), sets the read index, rewinds, skips three longs' worth of header (serverId, X, time), reads the flags byte into `[0x1905304]` (on the partner this may reset coop_validateData), then loops `0x2306e0(client, msg, 1, &type)`, which consumes type-2 records (reliable command: seq long, string, byte; executed through `0x166710(slot, str)` when seq == ack+1; a gap -> "EXE_LOSTRELIABLECOMMANDS") and returns the other types:
  - type 3: slot = ReadBits(1); if slot > 1 or `client[slot].state (+0) < 4`: (after 250 ms) "Invalid co-op packet - references invalid clientNum %i"; else select `client = 0x19ada98 + slot*0x1678`, ReadByte (marker 0x3d).
  - type 1: ReadByte (0x51), count = ReadByte (valid 1..0x20); base from `0x2019b0(player entity of 0x2316c0(slot), buf)`; per cmd `0x201d30(msg, from, to)` (delta read) plus weapon validity fix-ups (0x1866f0); if `+0x1210` == 3 -> `0x22cf90` (activate, then it should become 4); if `+0x1210` == 4: for each cmd with `cmd.serverTime > client.lastUsercmd.serverTime`: copy it to client+0x1630 and run `0x15bd80(slot)` (ClientThink: copies the player state, runs `0x1402316e0`, `0x15be30`, then `0x15f020` / `0x15f060`); ReadByte (0x47).
  - type 4: ReadByte (0x33); if readcount == cursize stop, else next record.
  - at the end: for slot 0..1 with address type 2: `0xcd300(slot.port, serverClient[slot].ack)`.
- `0x233190` coop_FrameDriver(msec) (from `0x1fbde0`, every main-loop iteration): see 3.x.
- `0x233400(msec)` run sim: `[0x19b48c4] += msec`; errors if sv time wrapped; loop while the accumulator allows: `0x234260` (tick), `0x233930` (pending console), `0x22e120` (partner: stop if the newest frame time <= sv time, i.e. no frame for the next tick yet), `0x234400` (arm the next tick: `0xcdee0`, `0x174d00`), `sv time += 0x32`. Tick = 50 ms = sv_fps 20.

### 3.x  Waiting, stall and fast forward (0x233190, 0x22e7a0, 0x22e920, 0x22ec30, 0x22e470, 0x22ed70)
- `0x233190(msec)`: if sv running: if the client is not active (0xd5820 false) only pump; else if no map change pending (0x22c340): `0x22ed70` (a coop drop) returns; partner fast-forward test: `latest = 0x22fff0(serverId)` (newest queued frame time); if `latest - sv time > coop_fastForwardThreshold (0x1919338)` then `target = latest` and `0x233400(target - simTime)` runs in one go with the time-limit TLS flag off and the paused dvar cleared; return. Otherwise: `0x22e470` (returns 1 = do the upkeep and stall check, unless a remote peer exists, "coopLoaded" was sent and no stall is active); if 1: when sim time > 0xf9 run `0x22e920`; if paused or stalled (`0x22e170`) hold the sim; then `0x22e7a0(msec)`; if that returns 0: `0x233400(msec)`.
- `0x22e7a0(msec)` partner waiting logic (returns 1 = hold the sim): host -> 0 at once; pump UDP; `have` = newest frame time; if `sv time < have` reset wait counters and return 0 (run). Otherwise wait: `[0x19b48d8] += msec`; when it reaches the back-off deadline `[0x19b48e0]`: for slot 0..1 with address type == 2: `0xcd340(slot.port)` (RE-SEND the usercmd packet); back-off by coop_backoffScheme (`0x1919370`): 1 = `[0x19b48dc] *= 2`, 2 = `+= 1`, next deadline = step*0x32 + waited; if waited >= dvar `0xd03938` -> "COOP_HOSTTIMEOUT" and disconnect.
- `0x22ec30(N)` (host): min over remote slots of their ack; counts ring entries from the newest back to that ack time; returns count >= N. Used with coop_maxFramesClientCanBeBehind (0x1919320, in `0x22efd0`: set stall) and coop_minFramesClientCanBeBehind (0x1919328, in `0x22e920`: re-evaluate, clear the stall). Loopback slots are excluded.
- `0x22e1a0` coop launch: cls state[n] = 3, tunes dvars from the measured rate `0xd6760()`: coop_maxFramesPerPacket = clamp((rate/8)/0x960, 3..20), coop_minFramesClientCanBeBehind, coop_maxPacketUserCmds = clamp(rate/0x1900, 3..0x20), `[0x540b80]` = min(1000, rate*0x3e3/1e6 + 5); then console `map <name>`.

### Connection setup that decides the slot types (OBSERVED reading; the server-side agents should confirm)
- `0xd25e0` `listen` (host): zero both slots, slot 0 = {type 2, 127.0.0.1, port 0}, `Netchan_Setup(sock 0, slot 0, ..)`, `[0x1905302] = 1` (host), `[0x52fb5c] = 1`, party member 0 added via `0xd7220`, cls state 0xb.
- `0xd23c0` connect (partner): parse host address, `Netchan_Setup(0, slot 0, hostaddr)`, `[0x1905301] = 1`, send OOB "coopConnect" (0x1d, 8-byte xuid, name, a dvar byte), `[0x52fb5c] = 1`.
- `0x22d830` (host, on "coopConnect"): picks a free slot (index 1), stores the sender's address (type 4), replies OOB "coopAccept" (slot index, player count, map, serverId, ...), `Netchan_Setup(sock 1, slot[i], addr)`, `[0x1905302] = 1`, adds party member i via `0xd7220` (its 5th argument is the party index).
- `0x22d460` (partner, on "coopAccept"): `[0x1905301] = 0`, `[0x19ada44] = serverId`, **slot[index].type = 2, slot[index].port = localClientNum**, `Netchan_Setup(sock 1, slot 0, hostaddr)`, `[0x1905302] = 0`, adds the host as party member 0 and itself as member `index`, answers OOB "coopAcceptAck". Note the partner's slot-0 netchan uses sock 1 (the UDP socket actually used: unknown).
- `0x22d2e0` (map-launch side): fills still-empty slots up to the player count with {type 4, 127.0.0.1, port idx+100} placeholders.

## 4. Wire formats (OBSERVED)
Client to host (payload inside Netchan; the netchan header is `s32 seq` [+ fragment header if >= 0x514 bytes]):
```
u8  0x1d          coop packet
u8  0             0 = client-to-host, 1 = host-to-client
s32 serverId      [0x5dac64]
s32 ackTime       newest frame time received (0 if none)
u8  validateMode  0/1/2
u8  bitsInLast    patched after writing (bitpos & 7)
--- bit stream ---
for each unacked reliable command:  3 bits =2, s32 seq, string(<=0x40), u8 0x5b
if count>0:  3 bits =1, u8 0x51, u8 count, count x deltaUsercmd, u8 0x47
3 bits =4   (end)
```
Host to client: `u8 0x1d, u8 1, u8 nFrames (1..20), nFrames x { u16 len, frame[len] }`.
Frame (also the host's own execution input): `s32 serverId, s32 X ([0x192d7e4]), s32 time, u8 mode`, then per client: `3 bits =3, 1 bit slot, u8 0x3d, <the client's bit stream above, ending with 3 bits =4>, u8 0x33`.

## 5. Dvars (pointer globals, rva) in this part
coop_maxFramesClientCanBeBehind 0x1919320 (0x22efd0), coop_minFramesClientCanBeBehind 0x1919328 (0x22e920), coop_maxFramesPerPacket 0x1919330 (0x22f400), coop_fastForwardThreshold 0x1919338 (0x233190),
coop_maxPacketUserCmds 0x1919340 (0xcd340), coop_validateData 0x1919318 (0x22e0f0, 0x22f0d0, 0x2302d0, 0x22f830), coop_backoffScheme 0x1919370 (0x22e7a0, 0x22e920), 0x540b78 / 0x540b80 (cl_packetdup / cl_maxpackets-like, INFERRED),
0xd03938 (host timeout ms), 0x13260e8 (paused-like), 0x13260c0 (sv running), 0x19c5ef8 (enables the early tick in the draw).

## 6. Unknown / not verified
- Which UDP socket and local port the partner's coop packets leave from (Netchan sock = 1 after coopAccept) and whether the host's slot-1 address (taken from the connect OOB) matches that source port. Not needed for the local case.
- Meaning of `[0x192d7e4]` (second long of the frame header) and `[0x1905304]` (flags byte). Who else writes `[0x5dac64]` besides 0x22c970 / 0x231790. The exact relation of the two sim clocks `[0x19c3868]` and `[0xd96ec8]`.
- Whether the second caller of `0xcd300` (`0xb6050`) matters for coop; the real names of dvars 0x540b78 / 0x540b80.
- The exact fields `0xd03a0` reads from `0x52fbc0 + n*0x21c` (button/axis layout) and `0xd19a0(n, 0x10)`; not needed for the packet.
- The states 2..9 of `0x173e90` (server side), and `0x15cbb0` / `0x15cbf0` / `0x170e60`.
- `0x233de0` runs the tick in spare time inside the cgame draw when dvar `0x19c5ef8` != 0 (value unknown); then `0xcd340` can be called from inside `0xbca00` instead of from the main loop. The order against `0xcf800` inside 0xbca00 was not checked.
- Nothing here was executed: no runtime check of the partner-side slot layout (slot 1 type 2) was made.

## 7. What a second local client needs from this part (plain words)
1. **The engine already has the slot concept.** It decides "this slot is a local player" from address type 2 and takes the local client number from the slot's port. For two local players on one machine that means: slot 0 = {loopback, port 0}, slot 1 = {loopback, port 1}, party members 0 and 1 in use, server-side client structs of both in the states a remote partner reaches after joining (`+0 >= 4`, `+0x1210` = 3 then 4 after the first usercmd; INFERRED), `[0x52fb5c] = 1`, `[0x1905302] = 1`. Then `0x15ce10`, `0x22e7a0`, the tail of `0x2302d0`, `0x22efd0`, `0x22f400` (skips loopback slots), `0x22e330` and `0x22ec30` already treat both correctly without code changes.
2. **Storage is the gap.** The send path reads and writes per-client memory by index n: cls state[n], the cl block (usercmd ring, cmdNumber, outPackets, view angles), the reliable-command ring, the input state; and `0xcf800` (create cmd) uses a literal 0. Element 1 does not exist in this exe (it would overlap the netchan). So context 1 needs its own copy of those ranges, swapped in around every call that touches them: `0xcf800 / 0xd03a0` (create cmd, render time), `0xd2110 / 0xd21a0` (add command), `0xcd340` (send), `0xcd300` (ack). Those functions take the client number as an argument, so the cheapest route (INFERRED) is to call them with n = 0 while context 1's memory is swapped in; no code needs patching for the indexing itself.
3. **The transmit call is hardcoded to slot 0's netchan.** `0xcd340` always calls `Netchan_Transmit(0x540ba0)` and uses that netchan's outgoingSequence for its outPackets index. A second local client must not share it. Options (INFERRED, not tried): (a) hook `0xcd340` so that for context 1 it builds the same message but hands `{header bytes 0x1d, 0 skipped}` + payload to a private queue instead of Netchan_Transmit; (b) tag loopback packets by context: the loopback ring already carries the sender's tag and `NET_CompareAdr` compares loopback ports, so a packet tagged 1 matches slot 1, but the tag comes from the netchan's `sock`, which the client side forces to 0 (sock >= 1 means "server sending"), so a hook on `0x202db0` / `0x202a60` (as the plan says) or an own ring is needed.
4. **Timing matters for any queue or injection.** The host frame for tick T is opened by `0x22f830` inside `0x15c9e0` AFTER `0x15ce10` sent. Packets are folded into the frame only while `0x1fd7c0` drains between begin-frame and `0x22efd0`. So local packets must be produced before that (they are: `0x15ce10` runs first) and consumed in the drain, either via the loopback ring or by calling `0x22f0d0(serverClient[1], msg)` right there. Calling `0x22f0d0` earlier would append to the previous frame, which has already been executed and sent.
5. **No network, no remote logic.** With both slots loopback nothing is sent over the network, `0x22ec30` sees no remote slot, and the partner-only code (`0x22edd0`, fast forward, back-off, host timeout) is never entered (`0x1905302 = 1`). The frame is built and executed in the same tick: zero lockstep latency, both players' input in the same frame, sim step stays 50 ms.
6. **Identity values.** The packet carries the serverId (`[0x5dac64]` must equal `[0x19ada44]`) and the ack time (`[0x5dac60]`, set once per tick from the frame ring; one shared value for both contexts is fine because the host reads it only for remote slots). The validate-mode byte from coop_validateData must be consistent between packet and frame (0 for the local case unless the validator is wanted).
7. **Reliable commands** (script / console client commands) of client n go through its own ring at `0x545844 + n*0x3218`; the host executes them from that slot's record, in order, exactly once (ack in `serverClient.ack`, returned to the client with `0xcd300`). A second context needs its own ring and ack store, otherwise acks of one client confuse the other.
8. **Usercmd creation** is per render frame and takes the literal 0 in `0xcf800`: for two contexts it has to run once per context per render frame (or per alternate frame in the alternate-frame design, which lengthens the cmd interval; cmd `serverTime` must come from the shared clock, not a per-context counter) before the tick's send.
9. **Rates.** One packet per tick per client; loopback is never rate limited; up to coop_maxPacketUserCmds (3..0x20) of the newest `cmdNumber - outPackets[...]` cmds per packet, so a context that creates fewer cmds per tick just sends fewer.
