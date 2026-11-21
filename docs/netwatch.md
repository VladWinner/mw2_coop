# Network co-op watch: the partner's silence and the two games going apart (2026-11-19)

Report (Vlad, network co-op, a friend hosted): "there were moments when on his screen I was already dead while I played on through the level on mine, and the
other way round. An on-screen notice when the other player shows no sign of life for more than 3 seconds would help: now there is a 12 s answer check only when
the lobby gathers, none during the game itself."

## Why the games can differ

The network co-op is the game's own Spec Ops lockstep (`docs/native-splitscreen-research/coop-host-frame.md`, `coop-client-send.md`): the host builds a frame of
both players' usercmds every server tick (50 ms) and both PCs execute the same frames with the same function. Each PC simulates the whole level itself; nothing
sends the world's state across or corrects it. If anything makes one PC's simulation take another turn (a value that differs between the PCs, a script reading
something local), each PC goes on with its own game from then on: one PC's player can be dead while the other PC's is alive. What made Vlad's games differ is not
known yet: this watch finds the moment, a log pair from both PCs is the next step.

## What is checked (`src/netwatch.c`, both builds and iw4x-sp)

1. **Silence.** `net.c` notes the time of every packet from the partner. Every 250 ms (the control thread): if a level runs here (client state 6 or more) and
   this PC executed lockstep frames until his last packet, then no packet for 3 s gives the notice "No sign of your partner for N s", again every 5 s, and
   "Your partner is back (an N s gap)" when his packets come back.
2. **Divergence.** The game keeps a running state hash on both PCs (64-bit rva `0x192d7e4`, 32-bit VA `0x1a7f0e0`): in every server frame each player and each
   actor adds its entity number, health, position and angles (G_RunFrameForEntity, 64-bit rva `0x174920`, through `0x22dee0` / `0x22dfb0`). The host writes
   its hash into the header of every lockstep frame (serverId, hash, frame number, validate byte); the partner's game reads it and ignores it. The partner's
   frame executor (64-bit rva `0x2302d0`, 32-bit VA `0x40c4f0`, iw4x-sp rva `0xa6520`) is hooked: it reads the host's hash from the frame's header and takes
   the difference to its own. While both PCs play the same game both hashes take the same additions, so the difference stays the same (it is not always 0: the
   QA runs saw 0 and 0x9eb0180d, constant). The frame where it changes is the first one that differed: logged with both hashes, sent to the host (packet
   `mw2coop_desync <level time>`), which brings the games together again (next section). When the difference holds still for 10 s again by itself, "The two
   games match again".

The notices go to the game's message window (top left) through the cgame's own `CG_GameMessage` (64-bit rva `0xa6400`, 32-bit VA `0x487010`): this PC only,
nothing of the lockstep game changes, so the watch cannot itself make the PCs differ (a script HUD element would: scripts run in lockstep). They are printed on the
main thread by the mod's command `mw2coop_netnotice`, which the control thread queues. Flags 0 show the line at once (0x11, as the game's "Game saved", types it
out letter by letter). The texts are `MW2COOP_NET_*` in `src/localize.c` (Russian in Windows-1251).

`[native] netwatch=0` turns both checks off; `netwatchlog=1` logs every 20th frame's hashes. QA commands: `mw2coop_netpause <ms>` (this PC sends the partner
nothing for that long), `mw2coop_netqa_desync` (64-bit: player 1's value at gentity +0x90 changes on this PC only), `mw2coop_netqa_say <flags> <text name>`.

## Bringing the games together again (`saves_resync`, owner 2026-11-19)

The owner asked for the world to be brought back in sync, but not as the Cyberpunk 2077 co-op mod Nightcoop does it (the host's world held with a message
while the other player loads the host's world). Of three ways (both PCs load the last checkpoint they hold in common; the host's world sent across and loaded
by both; values patched in place) he chose the first:

1. The client finds the divergence (above) and sends the host its level time (`mw2coop_desync <t>`, again every 15 s until the host answers).
2. The host (`saves_resync`, saves.c): the last checkpoint must be the same on both PCs (the tags `<map>_<level time>_net` they already exchange for a failed
   mission) and older than the first differing frame: a checkpoint both took in the same lockstep frame before it holds the same world on both. Saves must be on
   (the official 32-bit exe cannot load them: there the players are told to restart the level).
3. The host sends `mw2coop_resync <tag>` (three times); the client sets `mw2coop_retry load`. 1.5 s later (the players read the notice "both PCs load the common
   checkpoint") the host runs `fast_restart` with `mw2coop_retry load`: as after a failed mission, both PCs load their own copy of that checkpoint (the client on
   the coopRestart the map restart sends). Nobody waits for the other with a held world: both load at once, from memory, in about half a second.
4. After the load the client compares the hashes again; when they hold still for 3 s it shows, and tells the host to show, "Back in sync: the game goes on from
   the common checkpoint".

When it cannot (no common checkpoint, the last one taken after the divergence, saves off, or the same checkpoint loaded twice for this and the games went apart
again: the cause is still in the world), both PCs show "Out of sync since N s and no common checkpoint: restart the level", again every 15 s.

## Checked (network pairs on this PC, The Pit, `tools/qa_pair.sh p2ghost`)

* 64-bit: the hashes compared from level time 350 on, the difference constant (842 frames before the forced change); `mw2coop_netqa_desync` on the host found
  at the very frame (level time 36000 / 42500), both PCs showed the notice (frame captured: the Russian line in the top left); a send pause gave the silence
  notice at 3 s and "back" after it.
* Official 32-bit and iw4x-sp: the same comparison (constant difference, no false divergence), the silence notice at 3 s and "back".
* The game's own host timeout: when the client gets no frames for 10 s it ends the session itself ("did not get the acknowledgement within 10 s", an engine
  error). After a pause of 5-8 s the host's resends (its back-off scheme) came late enough to reach that timeout in two of four runs.

Resync (`mw2coop_netqa_desync` on the host 50 s into The Pit; the level's start checkpoint `trainer_300_net` on both):

* 64-bit: the client found the divergence 0.2 s after it, the host chose `trainer_300_net` (taken at 300, before 35750), both PCs loaded it 1.5 s later, the
  hashes agreed from the first frame after the load (difference 0) and both showed "Back in sync" 3.4 s after the load; the frame before the load shows the
  notice "Out of sync since 35 s: both PCs load the common checkpoint" (top left).
* iw4x-sp: the same (divergence at 26450, load, agree again after 3.4 s).
* Official 32-bit (saves off there): the host answered "the checkpoints are off in this session", both PCs showed the restart-the-level notice.

## What went into the hash (64-bit, 2026-11-21)

The hash takes additions only between `0x22dec0` (on) and `0x22ded0` (off): for each player, vehicle and actor (`0x174920`) its type, number and health
(`0x22dee0` called at `0x174a82`, `0x174a98`, `0x174ab0`), position and angles (`0x22dfb0` at `0x174ac9`, `0x174ae2`); four values of `0x171370` (`0x22dee0` at
`0x1714d2`, `0x171607`, `0x17161d`, `0x171633`); and strings (`0x22df10` at `0x190ee9`, `0x231026`, `0x23103b`). The two at `0x231026` / `0x23103b` are the
script's `GetDvar`: the name and the value of every dvar a script reads go into the hash, because a dvar is this PC's own and a script that reads one can take
another path on the other PC.

The byte that turns the additions on (`0x192d8f9`) is not a resolver address: it is read from the adder's own first instruction (`0x22dee0`: `cmp byte [rip+disp32], 0`), so the records work on a game build the mod was not made for too (checked with `MW2COOP_FORCE_UNKNOWN=1` on both copies: 495 of 495 addresses found by signature, a desync made on purpose on the host named by `netwatch_compare.py` as player 1's health).

All twelve calls are hooked (`hook_call`) and each addition is kept in a ring (65536 entries) with the level time of the lockstep frame executed last. When the
games go apart, both PCs write the additions of the steps before the first differing frame to the log (`netwatch: add t <time> ent <n> type .. health .. pos ..
angles ..`, `netwatch: add t <time> at <rva> string ".."`): the client when it finds the difference, the host when the client's report comes. At most four
dumps a level; `[native] netwatchrec=0` off. `python -I tools/netwatch_compare.py <host log> <client log>` puts the two dumps side by side and lists what differs.

## QA over an internet-like link

`MW2COOP_QALAG` (one-way delay in ms), `MW2COOP_QAJITTER` (up to that many ms more per packet, the order kept) and `MW2COOP_QALOSS` (packets lost per 1000),
environment variables of the test copies (net.c): each copy holds its packets to non-loopback IPv4 addresses in a queue a thread sends when they are due.
`export MW2COOP_QALAG=60 MW2COOP_QAJITTER=40 MW2COOP_QALOSS=10` before `tools/qa_pair.sh` makes the pair talk as over the internet (no relay, nothing for the
firewall to ask about).

## QA over the real internet: a relay on a server

`tools/qa_relay.py` runs on a server (Python 3, standard library): `python3 qa_relay.py <udp port> <the PC's public address> [seconds] [bind address]`. The test
copies get `MW2COOP_QARELAY=<server address>:<port>` (net.c): every game packet to a LAN address and a game port (28900-29099) goes to the relay wrapped with its
destination, and the relay sends it to the copy that registered that port; each copy registers its game sockets every second from those sockets. Only outgoing
packets leave the PC, so the home router lets the answers in with no port forwarding, and the game's own socket receives them (no new program for the firewall
to ask about). Every packet crosses the internet twice (PC to server to PC). The server's firewall needs the one UDP port open, for the PC's address only.

On a server with several addresses, give the relay the address the copies send to as [bind address]: bound to 0.0.0.0 its answers left from the server's main
address, and the home router's NAT dropped every one (nothing came back; a plain UDP test from the PC showed the same). The copies log `net: QA relay: N packets
out, N in, N answers to the registrations` every 10 s: answers 0 means the way back is closed.

## Issue 27: the climb of Cliffhanger went apart at the same spot after every load (found and fixed 2026-11-21)

Report: over Steam, any mission, "out of sync" and a checkpoint load many times a mission; the Cliffhanger jump had to be done three times, both players
climbed up fine and then it went out of sync and loaded the top again.

Reproduced: `tools/qa_pair.sh climb "" 420 cliffhanger` with the QA link above: the games went apart at level time 87.5-87.7 s (the pull-up at the end of the
first wall, `climb_pullup`) three times, each after the load of the common checkpoint (50.5 s). The dumps of both PCs (`netwatch_compare.py`): every entity the
same, but in the step after frame 87550 the script's `GetDvar( "specialops" )` gave "0" on the host and "1" on the client, and the client's script went on
reading `coop` and `mapname` where the host's read `scr_damagefeedback`.

Cause (`menus.c`): the co-op lobby is Spec Ops' and its menu sets `specialops` to 1; the launch of a campaign co-op sets it to 0 for the whole session
(`campaign_loading_begin`), and the menu poll gives the old value back when the session is over. On a client, whose party is not the active one, the poll right
after the host's start packet (the client's level server not yet running, `sv_running` 0) took the session for over: `specialops` went back to 1 15 ms after
the launch, and the client played every level with 1 while the host had 0. Fix: while the launch keeps it at 0, the engine's `coop` dvar at 1 also keeps the
session (`menus_poll`). Both PCs and both builds (the code is shared).

## Open

* The records are 64-bit only; the 32-bit build still says only that the hashes differ.
* Vlad's own case: his and his friend's logs of a session where it happened (both `mw2coop.log`, the lines `netwatch:`).
