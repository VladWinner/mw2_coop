# Weapon player anim type (third-person body pose) - both builds

Question: the partner's third-person body holds Endgame's `ending_knife` sideways at the chest in a rifle pose. Which WeaponDef field
picks the body's animation set, what values exist, and which value would suit a knife held in one hand.

Read on 2026-11-17 (agent `wanim`), Ghidra projects read only, the game was not run. Inputs:
- 64-bit `iw4sp.exe` sha256 `2f8c9cfd...a69d805`; its zones `zone/russian/common.ff` (`489af865...`) and `ending.ff` (`b33ccef7...`), read from the 64-bit test copy.
- 32-bit `image32.bin` (official build `FC9E4843...52D7`, base 0x400000); its zones `common.ff` (`3eae5b65...`) and `ending.ff` (`8fa0b913...`) from the 32-bit test copy.
- Zones were decompressed in memory only (zlib stream, as `local/xbox360-ref/pc_ff_rawfiles.py` does); nothing was written next to the game.

## Summary (offsets inside the struct that `WEAPON_DEF(w)` returns)

| field | 64-bit | 32-bit | how known |
|---|---|---|---|
| playerAnimType (int, index into the anim type names) | 0x50 | 0x28 | code in both builds (below) + 32-bit field table |
| weapType (bullet, grenade, projectile, riotshield) | 0x54 | 0x2c | 64: GScr weaponType; 32: field table |
| weapClass | 0x58 | 0x30 | code in both builds + 32-bit field table |
| penetrateType | 0x5c (derived) | 0x34 | 32: field table; 64: by the layout, not seen in code |
| inventoryType | 0x60 | 0x38 | 64: GScr weaponInventoryType; 32: field table |
| fireType | 0x64 (derived) | 0x3c | 32: field table |
| offhandClass | 0x68 (derived) | 0x40 | 32: field table |
| gunXModel (XModel **, first person) | 0x08 | 0x04 | 64: viewmodel code rva 0xc6480; 32: field table (FullDef 0x7cc is the inline array) |
| worldModel (XModel **, per model index) | 0x2e0 | 0x1d8 | code in both builds (GetWeaponModel, the gclient model updater) |
| worldKnifeModel (XModel *) | 0x300 | 0x1e8 | code in both builds (the gclient model updater) + 32-bit field table |
| knifeModel / worldClipModel / rocketModel | 0x2f8 / 0x2e8 / 0x2f0 (derived) | 0x1e4 / 0x1dc / 0x1e0 | 32: field table; 64: by the layout |

`ending_knife` in both builds' `ending.ff`: **playerAnimType 3 = smg**, weapType 0 (bullet), weapClass 3 (smg), inventoryType 0 (primary).

## Observed

### Anim type names - loaded from a rawfile, not built into the exe
- 64-bit rva 0x85070 / 32-bit VA 0x445840 (`BG_LoadPlayerAnimTypes`): reads rawfile `mp/playeranimtypes.txt`, one name per token, at most 64,
  into an array of string pointers; the index of a name is its line number from 0. It also stores the index of `"hold"` in a global that
  nothing reads (64: written at 0x1400851b8 only).
  - 64-bit: names `0x1403ed8e0` (char *[64]), count `0x1403ed8d8`, hold index `0x1403ed8dc`. 32-bit: names `0x1a69538`, count `0x1a689ec`,
    hold index `0x1a689e8` (`"hold"` string at 0x6ba8e0).
  - 64-bit rva 0x851e0 / 32-bit map guess 0x4bcd70 hands each name to rva 0x6dc60 (registers it as a value of the anim script's
    PLAYERANIMTYPE condition).
- The rawfile from `common.ff` (64-bit and 32-bit zones: same 18 lines, only CR/LF differ; the Xbox 360 dump in `local/xbox360-ref` has the same list):

| value | name | value | name | value | name |
|---|---|---|---|---|---|
| 0 | none | 6 | sniper | 12 | m203 |
| 1 | other | 7 | rocketlauncher | 13 | hold |
| 2 | pistol | 8 | explosive | 14 | briefcase |
| 3 | smg | 9 | grenade | 15 | riotshield |
| 4 | autorifle | 10 | turret | 16 | laptop |
| 5 | mg | 11 | c4 | 17 | throwingknife |

  There is no `default` and no `melee`/`knife` type. Check of the mapping: in the zones `usp` has 2 (pistol), `ak47` 4 (autorifle),
  shotguns 1 (other), `claymore` and `c4` 13 (hold), `riotshield` 15, `m79` 12 (m203), `fraggrenade` 9.
- Fixed name tables in the exe (enum -> text, used by GScr and the 32-bit weapon file parser): weapType {bullet, grenade, projectile, riotshield},
  weapClass {rifle, sniper, mg, smg, spread, pistol, grenade, rocketlauncher, turret, throwingknife, non-player, item},
  inventoryType {primary, offhand, item, altmode, exclusive, scavenger}. 64-bit tables at 0x1403bd650 / 0x1403bd670 / 0x1403bd6d0
  (getters rva 0x85040 / 0x85020 / 0x85030); 32-bit at 0x736b08 / 0x736b18 / 0x736b48.

### Anim script conditions
- Condition name table, 64-bit 0x1403bc920 (32-bit `"PLAYERANIMTYPE"` string 0x6d0b44, table ref 0x736360): 0 PLAYERANIMTYPE, 1 WEAPONCLASS,
  2 MOUNTED, 3 MOVETYPE, 4 CROUCHING, 5 FIRING, 6 WEAPON_POSITION, 7 STRAFING, 8 PERK, 9 DAMAGETYPE, 10 HITLOCATION, 11 HITDIRECTION,
  12 AKIMBO, 13 DIVEDIRECTION, 14 RIOTSHIELDNEXT, 15 PLAYERANIMTYPEPRIMARY, 16 FASTMANTLE, 17 SCRIPTED.
- Per-client condition values: `*(bgs ptr) + clientNum * stride + base + cond * 8` (bit masks for enum conditions).
  64-bit: bgs pointer `0x1403d53d0`, stride 0x198, base 0x93294. 32-bit: pointer `0x1a54bdc`, stride 0x180, base 0x82a50.
- The script is rawfile `mp/playeranim.script` in `common.ff` (64-bit loader rva 0x6bc00, 32-bit map guess 0x4f20f0). Conditions are tried in order,
  first match wins. The animations it names for the types below are all present in `common.ff` (checked by name).

### Where playerAnimType is read (each animation update, straight from the WeaponDef)
- **From the player state** (64-bit rva 0x6d170, 32-bit VA 0x4f9d20; bytes 32: `53 8b 5c 24 08 55 56 8b 33 57 56 e8 c0 5e f2 ff`;
  64: `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57`). Argument: pmove (ps = first field). Reads
  `WEAPON_DEF(viewmodel weapon)->playerAnimType` (64 +0x50 / 32 +0x28) into condition 0, `WEAPON_DEF(ps weapon at +0x2b4)->playerAnimType` into
  condition 15 (PLAYERANIMTYPEPRIMARY), and `->weapClass` (64 +0x58 / 32 +0x30) into condition 1.
  The viewmodel weapon is rva 0x85820 / VA 0x41fbf0: `(ps+0x2bc & 2) ? ps+0x2a8 (offhand) : ps+0x2b4`.
  Callers 64: rva 0x7d190, 0x70310, 0x7f8b0; 32: VA 0x651b40, 0x650160, 0x48c760.
- **From the entity state** (64-bit rva 0x6cce0, 32-bit VA 0x649990; bytes 32: `53 55 56 57 8b f8 8b 44 24 14 0f b7 48 7c 51 e8`;
  64: `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48`). Reads the weapon index (u16) at entity state +0x7c, then the same two fields.
  32-bit convention: clientInfo in EAX, entity state on the stack (`[esp+0x14]` after four pushes); 64-bit: (es, ci) in RCX, RDX.
  Called by the player animation step 64-bit rva 0x6efa0 / 32-bit VA 0x4fbfd0, which then runs the legs (ci+8) and torso lerp frames;
  64-bit caller of that: rva 0x15b340 (server side; called from rva 0x173e90 and 0x15e4b0).
- Condition evaluator 64-bit rva 0x6daa0 (enum conditions: mask AND; value conditions: equality); set helpers rva 0x6f950 (mask), 0x6f9c0 (value).
  32-bit counterparts: VA 0x40c830 and 0x49f8c0 also touch base 0x82a50 (not decompiled; likely the same two kinds).

### The 32-bit weapon file field table (the 64-bit exe has none: `"playerAnimType"` is not in it)
- `cspField_t { const char *name; int offset; int type; }`, 12 bytes, starting at VA 0x736c68 (`displayName`). Offsets are into a
  WeaponFullDef; **WeaponDef = FullDef + 0x74** (checked: `aimAssistRange` 0x358, `aimAssistRangeAds` 0x35c, `autoAimRange` 0x354 are
  `native.c`'s WD_AIM_RANGE 0x2e4 / 0x2e8 / WD_AUTO_RANGE 0x2e0 plus 0x74).
- Entries: `playerAnimType` 0x9c type 0x18 (VA 0x736c8c), `weaponType` 0xa0 type 0x10, `weaponClass` 0xa4 type 0x11, `penetrateType` 0xa8,
  `inventoryType` 0xac type 0x1e, `fireType` 0xb0, `offhandClass` 0xb4, `worldModel`..`worldModel16` 0x9b4..0x9f0 (the inline array),
  `worldClipModel` 0x250, `rocketModel` 0x254, `knifeModel` 0x258, `worldKnifeModel` 0x25c, `handModel` 0x7c, `gunModel` 0x7cc.
- The custom-type handler (code at VA 0x655fa0, not a defined function) stores weapType at FullDef+0xa0, weapClass +0xa4, inventoryType +0xac.

### Third-person weapon model: chosen natively, attached by script
- Single player has no native third-person weapon attachment for players. The game keeps two model pointers in the gclient and notifies the
  script; `maps\_loadout::UpdateModel` (common.ff) does `waittill("weaponchange")` and re-attaches:
  - `GetPlayerWeaponModel()` -> by model name: `weapon_riot_shield` to `tag_weapon_left`; `weapon_claymore`, `weapon_c4`,
    `weapon_claymore_no_legs` to `tag_inhand`; everything else to `tag_weapon_right` (the mod's `attach_weapon_model` hooks this default).
  - `GetPlayerKnifeModel()` -> `tag_inhand`. Dual wield: the weapon model again on `tag_weapon_left`. Then `GetWeaponHideTags` parts hidden.
  - The anim type does not choose the tag; only the model name does.
- The updater: 64-bit inside ClientThink_real rva 0x15be30 (at 0x14015c0c6..0x14015c135); 32-bit its own function VA 0x5b3660
  (`53 55 8b 6c 24 0c f7 45 0c 00 0c 00 00 56 8b b5`, cdecl, arg gentity; called from ClientThink_real VA 0x457390):
  - if `(ent+0xc & 0xc00) == 0` and the client's weapon (`client+0x2b4`) != 0: `model = WD->worldModel[BG_GetWeaponModelIndex(client, weapon)]`
    (64 WD+0x2e0, index from rva 0x86620; 32 WD+0x1d8, VA 0x4a2a90); and if the torso animation (`client+0x90`) has flag bit 8 in the
    anim script's animation entry (+0x50) (64 rva 0x6dd30, 32 VA 0x4f9800), `knife = WD->worldKnifeModel` (64 +0x300, 32 +0x1e8), else none.
  - stored in the gclient: 64-bit `+0xafc0` weapon model, `+0xafc8` knife model; 32-bit `+0xafbc`, `+0xafc0`. On change it notifies
    `weaponchange` (scr_const 64 `0x141157b6c`, 32 `0x12cc1f4`).
  - Script methods: `getplayerweaponmodel` 64 rva 0x1630f0 / 32 VA 0x5b7e10; `getplayerknifemodel` 64 rva 0x163160 / 32 VA 0x5b7e80;
    `getweaponmodel` 64 rva 0x185960 / 32 VA 0x5ca220 (reads WD->worldModel[i], i < 0x100).
- Tag scr_consts (64-bit, from the const init at rva 0x1af190): `tag_weapon_left` 0x1411579e2, `tag_weapon_right` 0x1411579e4,
  `tag_weapon_chest` 0x1411579e6, `tag_stowed_back` 0x1411579e8. Their only native readers are the first-person viewmodel (rva 0xc6480)
  and an actor weapon script command (rva 0x14cef0); none is the player body.

### What the anim script plays per type (COMBAT state, standing still, hip, not strafing, primary = same type)
First match per movement type, evaluated from `mp/playeranim.script` (a hand-written evaluator; mantle, turns, flinch, stumble omitted).

| move | smg (ending_knife now) | pistol (2) | hold (13) | throwingknife (17) | none (0) |
|---|---|---|---|---|---|
| idle | pb_stand_alert (default rifle) | pb_stand_alert_pistol | pb_hold_idle | pb_stand_pullout_knife | pb_stand_alert |
| idlecr | pb_crouch_alert | pb_crouch_alert_pistol | pb_crouch_hold_idle | pb_crouch_alert (default) | pb_crouch_bombplant |
| idleprone | pb_prone_aim | pb_prone_aim_pistol | pb_prone_hold | pb_prone_pullout_knife | pb_prone_bombplant |
| walk | pb_stand_shoot_walk_forward | pb_combatwalk_forward_loop_pistol | pb_hold_run | default rifle | pb_combatwalk_forward_loop_pistol |
| run | pb_combatrun_forward_loop | pb_pistol_run_fast | pb_hold_run | default rifle | pb_pistol_run_fast |
| sprint | pb_sprint | pb_sprint_pistol | pb_sprint_hold | pb_sprint | pb_sprint |
| runcr | pb_crouch_run_forward | pb_crouch_run_forward_pistol | pb_crouch_hold_run | default rifle | pb_crouch_run_forward_grenade |
| laststand | pb_laststand_idle | pb_laststand_idle | pb_deadhands_idle | pb_laststand_idle | pb_laststand_idle |

Events: `fireweapon` with hold plays `pt_hold_throw` / `pt_hold_prone_throw`; with throwingknife `pt_stand_throw_knife` / `pt_prone_throw_knife`.
`knife_melee` plays `pt_melee_pistol_1` for every type except prone/laststand.

## Derived (from the observations above, not tested)
- `ending_knife` = smg makes every pose fall to the script's "two handed rifle type weapon" default: that is the rifle pose the partner sees.
- Writing a different value to `WEAPON_DEF(ending_knife) + 0x50` (64) / `+ 0x28` (32) should change the body's set, because both readers take the
  value from the WeaponDef at each animation update. A write lives in the loaded zone and is gone when `ending.ff` is loaded again.
- Fit for a knife in one hand:
  - **hold (13)**: the only type with its own pose for every movement (idle, crouch, prone, walk, run, sprint, crouch run, last stand).
    Single player gives it to `claymore` and `c4`, whose models `_loadout` puts on `tag_inhand`, so the set is made for a small item in the
    `tag_inhand` hand. Best candidate, with the knife attached to `tag_inhand` instead of `tag_weapon_right`.
  - pistol (2): full set too, but it aims the item forward like a handgun.
  - throwingknife (17): knife poses only while standing or prone still (`pb_*_pullout_knife`, a pull-out animation); crouching and all
    movement fall back to the rifle default, so the rifle pose stays whenever he moves.
  - none (0) and other (1) give no knife pose (none mixes rifle idle, bomb-plant crouch and pistol walk; other is the rifle default).

## Unknown
- How `pb_hold_*` actually looks with a knife, and which hand `tag_inhand` is on the body rigs: not run, not viewed.
- Whether a looping `pb_stand_pullout_knife` as an idle looks like a held knife.
- Other readers of playerAnimType (side effects of changing it, e.g. on movement or the first-person view): not surveyed.
- When a changed value takes effect (next animation change or at once): not traced into the anim selection code.
- 64-bit offsets marked "derived" in the summary (penetrateType, fireType, offhandClass, knifeModel, worldClipModel, rocketModel): no code read.
