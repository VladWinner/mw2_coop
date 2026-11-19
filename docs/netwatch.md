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

## Open

* Which value first differs: the hash is one number for all entities. A rolling record of each entity's additions on both PCs, dumped when a divergence is
  found, would name the entity and the frame (compare the two logs).
* Vlad's own case: his and his friend's logs of a session where it happened (both `mw2coop.log`, the lines `netwatch:`).
