// mw2coop QA: the night audit of 2026-11-17 (places where the level's code acted on player 1 alone; local/scratch/night-1116/script-bindings.md).
// Dispatched from maps\_mw2coop_qa::main() (its default case) through dispatch(). This file is compiled on every map, so it never names a level's
// script: it reaches level code through the level's own tables (notetrack functions, hint break tests), its entities and its flags.
//   knifeanim   - (ending, any start; MW2COOP_PRE "set mw2coop_god 0") player 2 is the knife fight's lead; the stomp notetrack (face_stomp) runs:
//                 who takes its damage
//   estatemines - (estate, start "ambush") both players prone, the bouncing-betty ambush is set off: is each one thrown by a mine of his own
//   cavecollapse- (af_caves, start "airstrip": the cave exit's collapse plays at once) both players' link, stance and place every 3 s
//   gulagfall   - (gulag, start "cafe"; MW2COOP_PRE "set mw2coop_god 0") the fall in the cafeteria: each player's health and stance
//   attackacc   - (any map) set_player_attacker_accuracy() on the level and on player 1: both players' attackeraccuracy
//   rideprot    - (roadkill, start "dismount") the dismount start's protection on both players and its end; the flash lesson's break test
//   smokebtr    - (invasion, start "bmp") the "get smoke" break test; player 2 throws a smoke grenade (his pad's LB) at the BMP: does the level see it
//   barrettp2   - (dcburning, start "barrett") player 2 uses the Barrett, gets off (X); player 1 uses it: who is on it, player 2's weapon
//   gulagfall2  - (gulag, start "run"; the "cafe" start crashed the split screen at its start on 2026-11-17, before and after the change) player 2 sets
//                 off a stumble trigger, then the level's flags of the cave-in up to the cafeteria fall are set: each player's stance and health
//   gulaghall   - (gulag, start "rappel") the hallway explosion after the cell block (its flags set): who is knocked prone
//   downsync    - (any map; MW2COOP_PRE "set mw2coop_god 0") player 2 is downed, the level gives player 1 a weapon, player 2 is revived: does he get it
//   ffscale     - (any map, friendly fire on) the friendly-fire scale is left at 0 (as Team Player's ride leaves it) and the mission is failed: after the
//                 restart the level sets it back to 1
//   ffhop       - (trainer, friendly fire on) the scale is left at 0 and the mission is completed: in the next level it is 1 again
//   dcempwalk   - (dcemp, start "street") both walk forward on their pads: each one's speed under the street's slowed walk
#include common_scripts\utility;
#include maps\_utility;

dispatch( name )
{
	switch ( name )
	{
		case "knifeanim":
			knifeanim();
			return true;
		case "estatemines":
			estatemines();
			return true;
		case "cavecollapse":
			cavecollapse();
			return true;
		case "gulagfall":
			gulagfall();
			return true;
		case "gulagfall2":
			gulagfall2();
			return true;
		case "attackacc":
			attackacc();
			return true;
		case "rideprot":
			rideprot();
			return true;
		case "smokebtr":
			smokebtr();
			return true;
		case "barrettp2":
			barrettp2();
			return true;
		case "gulaghall":
			gulaghall();
			return true;
		case "dcempwalk":
			dcempwalk();
			return true;
		case "downsync":
			downsync();
			return true;
		case "ffscale":
			ffscale();
			return true;
		case "ffhop":
			ffhop();
			return true;
	}
	return false;
}

report( text )
{
	maps\_mw2coop_qa::report( text );
}

check( ok, text )
{
	maps\_mw2coop_qa::check( ok, text );
}

flag_on( name )
{
	if ( !flag_exist( name ) )
		flag_init( name );
	if ( !flag( name ) )
		flag_set( name );
}

count_damage()
{
	self notify( "qa_count_damage" );
	self endon( "qa_count_damage" );
	self.qa_damage = 0;
	for ( ;; )
	{
		self waittill( "damage", amount );
		self.qa_damage += amount;
	}
}

// The function a level registered for a notetrack (maps\_anim addNotetrack_customFunction).
notetrack_func( animname, anime, note )
{
	if ( !isdefined( level.scr_notetrack ) || !isdefined( level.scr_notetrack[ animname ] ) )
		return undefined;
	if ( !isdefined( level.scr_notetrack[ animname ][ anime ] ) || !isdefined( level.scr_notetrack[ animname ][ anime ][ note ] ) )
		return undefined;
	foreach ( entry in level.scr_notetrack[ animname ][ anime ][ note ] )
		if ( isdefined( entry[ "function" ] ) )
			return entry[ "function" ];
	return undefined;
}

yes( value )
{
	if ( !isdefined( value ) )
		return false;
	if ( value )
		return true;
	return false;
}

hint_break( name )
{
	if ( !isdefined( level.trigger_hint_func ) || !isdefined( level.trigger_hint_func[ name ] ) )
		return undefined;
	return level.trigger_hint_func[ name ];
}

state_text( player )
{
	linked = 0;
	if ( player IsLinked() )
		linked = 1;
	return "linked " + linked + " " + player GetStance() + " health " + player.health;
}

// ------------------------------------------------------------------------------------------------ Endgame knife fight notetracks

knifeanim()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	wait 2;
	fn = notetrack_func( "player_rig", "gun_kick", "vision_effect" );
	check( isdefined( fn ), "the level has the stomp notetrack (player_rig gun_kick vision_effect)" );
	if ( !isdefined( fn ) )
	{
		report( "done" );
		return;
	}
	level.mw2coop_lead = level.player2;
	foreach ( player in level.players )
		player thread count_damage();
	wait 0.1;
	h1 = level.player.health;
	h2 = level.player2.health;
	level thread [[ fn ]]( undefined );
	wait 0.5;
	report( "face_stomp with player 2 as the lead: player 1 took " + level.player.qa_damage + " (health " + h1 + " -> " + level.player.health + "), player 2 took " + level.player2.qa_damage + " (health " + h2 + " -> " + level.player2.health + ")" );
	check( level.player2.qa_damage > 0 && level.player.qa_damage == 0, "the stomp hits the lead (player 2), not the witness (player 1)" );
	level.mw2coop_lead = level.player;
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ Loose Ends mines

estatemines()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	wait 2;
	// the claymore hint's break test (section 2.2): player 1 holds a claymore, player 2 does not
	fn = hint_break( "claymore_hint" );
	if ( isdefined( fn ) )
	{
		level.player GiveWeapon( "claymore" );
		level.player SwitchToWeapon( "claymore" );
		wait 2;
		r1 = level.player [[ fn ]]();
		r2 = level.player2 [[ fn ]]();
		report( "claymore hint break test (player 1 holds " + level.player GetCurrentWeapon() + ", player 2 " + level.player2 GetCurrentWeapon() + "): player 1 " + r1 + ", player 2 " + r2 );
		check( r1 && !r2, "each player's claymore hint goes by his own claymore" );
		level.player SwitchToWeapon( level.player GetWeaponsListPrimaries()[ 0 ] );
		wait 1;
	}
	foreach ( player in level.players )
		player SetStance( "prone" );
	wait 1.5;
	report( "before the ambush: player 1 " + state_text( level.player ) + ", player 2 " + state_text( level.player2 ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " units apart" );
	start1 = level.player.origin;
	start2 = level.player2.origin;
	moved1 = 0;
	moved2 = 0;
	linked1 = false;
	linked2 = false;
	prone1 = 0;
	prone2 = 0;
	flag_set( "bouncing_betty_activated" );
	for ( t = 0; t < 16; t += 0.1 )
	{
		if ( flag( "bouncing_betty_player_released" ) && !flag( "ambushed_player_back_to_normal" ) )
		{
			// the prone-only moment after the throw
			if ( level.player GetStance() == "prone" )
				prone1++;
			if ( level.player2 GetStance() == "prone" )
				prone2++;
		}
		if ( level.player IsLinked() )
			linked1 = true;
		if ( level.player2 IsLinked() )
			linked2 = true;
		d = Distance( level.player.origin, start1 );
		if ( d > moved1 )
			moved1 = d;
		d = Distance( level.player2.origin, start2 );
		if ( d > moved2 )
			moved2 = d;
		wait 0.1;
	}
	report( "after the mines: player 1 thrown (linked to the slide " + linked1 + ", moved " + int( moved1 ) + "), player 2 thrown (linked " + linked2 + ", moved " + int( moved2 ) + "), alive " + IsAlive( level.player ) + " / " + IsAlive( level.player2 ) );
	report( "now: player 1 " + state_text( level.player ) + ", player 2 " + state_text( level.player2 ) + "; prone samples in the prone-only moment: player 1 " + prone1 + ", player 2 " + prone2 );
	check( prone2 > 10, "player 2 is held prone after his throw" );
	check( linked1 && moved1 > 30, "player 1 is thrown by his mine" );
	check( linked2 && moved2 > 30, "player 2 is thrown by a mine of his own" );
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ Just Like Old Times cave exit

cavecollapse()
{
	view = GetEnt( "cave_exit_playerview_01", "targetname" );
	check( isdefined( view ), "the cave mouth's view node is there" );
	for ( t = 0; t <= 45; t += 3 )
	{
		text = "t " + t + ": player 1 " + state_text( level.player );
		if ( isdefined( view ) )
			text = text + " " + int( Distance( level.player.origin, view.origin ) ) + " from the cave mouth";
		text = text + "; player 2 " + state_text( level.player2 );
		if ( isdefined( view ) )
			text = text + " " + int( Distance( level.player2.origin, view.origin ) ) + " from the cave mouth";
		text = text + "; apart " + int( Distance( level.player.origin, level.player2.origin ) );
		report( text );
		wait 2.7;	// (report() waits 0.3 s)
	}
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ The Gulag

gulagfall()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	report( "waiting for the fall in the cafeteria (flag player_falls_down): player 1 " + state_text( level.player ) + ", player 2 " + state_text( level.player2 ) );
	if ( !flag( "player_falls_down" ) )
		level waittill_notify_or_timeout( "player_falls_down", 60 );
	check( flag( "player_falls_down" ), "the cafeteria fall started" );
	low1 = level.player.health;
	low2 = level.player2.health;
	prone1 = -1;
	prone2 = -1;
	for ( t = 0; t < 5; t += 0.05 )
	{
		if ( level.player.health < low1 )
			low1 = level.player.health;
		if ( level.player2.health < low2 )
			low2 = level.player2.health;
		if ( prone1 < 0 && level.player GetStance() == "prone" )
			prone1 = t;
		if ( prone2 < 0 && level.player2 GetStance() == "prone" )
			prone2 = t;
		wait 0.05;
	}
	report( "the fall: player 1 lowest health " + low1 + ", prone after " + prone1 + " s; player 2 lowest health " + low2 + ", prone after " + prone2 + " s" );
	check( low1 <= 10 && prone1 >= 0, "player 1 falls (health down to 5, prone)" );
	check( low2 <= 10 && prone2 >= 0, "player 2 falls too (health down to 5, prone)" );
	report( "done" );
}

gulagfall2()
{
	level.mw2coop_no_follow = true;
	wait 4;
	triggers = GetEntArray( "stumble_trigger", "targetname" );
	report( "stumble triggers " + triggers.size + "; player 2 sets them off" );
	foreach ( trigger in triggers )
		trigger notify( "trigger", level.player2 );
	wait 3;
	flag_on( "exit_collapses" );
	wait 2;
	flag_on( "big_earthquake_hits" );
	wait 2;
	h1 = level.player.health;
	h2 = level.player2.health;
	flag_on( "player_falls_down" );
	low1 = h1;
	low2 = h2;
	prone1 = -1;
	prone2 = -1;
	for ( t = 0; t < 5; t += 0.05 )
	{
		if ( level.player.health < low1 )
			low1 = level.player.health;
		if ( level.player2.health < low2 )
			low2 = level.player2.health;
		if ( prone1 < 0 && level.player GetStance() == "prone" )
			prone1 = t;
		if ( prone2 < 0 && level.player2 GetStance() == "prone" )
			prone2 = t;
		wait 0.05;
	}
	report( "the fall: player 1 health " + h1 + " lowest " + low1 + ", prone after " + prone1 + " s; player 2 health " + h2 + " lowest " + low2 + ", prone after " + prone2 + " s; player 2's own roller " + isdefined( level.mw2coop_fall ) );
	check( prone1 >= 0, "player 1 falls (prone)" );
	check( prone2 >= 0, "player 2 falls too (prone)" );
	report( "done" );
}

gulaghall()
{
	level.mw2coop_no_follow = true;
	wait 2;
	flag_on( "cellblock_player_starts_rappel" );
	wait 6;
	foreach ( name in StrTok( "leaving_cellblock nvg_zone nvg_leave_cellarea player_sees_explosion", " " ) )
	{
		flag_on( name );
		wait 0.2;
	}
	report( "hallway flags set: player 1 " + state_text( level.player ) + ", player 2 " + state_text( level.player2 ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " units apart" );
	prone1 = -1;
	prone2 = -1;
	for ( t = 0; t < 8; t += 0.1 )
	{
		if ( prone1 < 0 && level.player GetStance() == "prone" )
			prone1 = t;
		if ( prone2 < 0 && level.player2 GetStance() == "prone" )
			prone2 = t;
		wait 0.1;
	}
	report( "the hallway explosion: player 1 prone after " + prone1 + " s, player 2 prone after " + prone2 + " s" );
	check( prone1 >= 0, "player 1 is knocked down" );
	check( prone2 >= 0, "player 2 is knocked down too" );
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ set_player_attacker_accuracy (any map)

attackacc()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	wait 1;
	report( "before: player 1 attackeraccuracy " + level.player.attackeraccuracy + ", player 2 " + level.player2.attackeraccuracy );
	set_player_attacker_accuracy( 0.25 );
	wait 0.1;
	report( "set_player_attacker_accuracy( 0.25 ) on the level: player 1 " + level.player.attackeraccuracy + ", player 2 " + level.player2.attackeraccuracy );
	check( level.player.attackeraccuracy == 0.25 && level.player2.attackeraccuracy == 0.25, "a call on the level sets both players" );
	level.player2 set_player_attacker_accuracy( 0.5 );
	wait 0.1;
	report( "level.player2 set_player_attacker_accuracy( 0.5 ): player 1 " + level.player.attackeraccuracy + ", player 2 " + level.player2.attackeraccuracy );
	check( level.player.attackeraccuracy == 0.25 && level.player2.attackeraccuracy == 0.5, "a call on one player stays his" );
	maps\_gameskill::updateAllDifficulty();
	wait 0.1;
	report( "after updateAllDifficulty: player 1 " + level.player.attackeraccuracy + ", player 2 " + level.player2.attackeraccuracy );
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ Team Player

rideprot()
{
	level.mw2coop_no_follow = true;
	report( "dismount start: player 1 attackeraccuracy " + level.player.attackeraccuracy + " ignores random bullets " + yes( level.player.IgnoreRandomBulletDamage )
		+ "; player 2 attackeraccuracy " + level.player2.attackeraccuracy + " ignores random bullets " + yes( level.player2.IgnoreRandomBulletDamage ) );
	check( level.player2.attackeraccuracy == 0 && yes( level.player2.IgnoreRandomBulletDamage ), "player 2 is protected like player 1 at the dismount" );
	wait 11;
	report( "11 s later: player 1 attackeraccuracy " + level.player.attackeraccuracy + " ignores " + yes( level.player.IgnoreRandomBulletDamage )
		+ "; player 2 attackeraccuracy " + level.player2.attackeraccuracy + " ignores " + yes( level.player2.IgnoreRandomBulletDamage ) );
	check( level.player2.attackeraccuracy > 0 && !yes( level.player2.IgnoreRandomBulletDamage ), "player 2's protection ends with player 1's" );
	fn = hint_break( "learn_flash" );
	check( isdefined( fn ), "the level has the flash lesson's break test" );
	if ( isdefined( fn ) )
	{
		foreach ( player in level.players )
		{
			if ( !player HasWeapon( "flash_grenade" ) )
				player GiveWeapon( "flash_grenade" );
			player.flash_ammo = 3;
		}
		level.player SetWeaponAmmoStock( "flash_grenade", 2 );
		level.player2 SetWeaponAmmoStock( "flash_grenade", 3 );
		wait 0.1;
		r1 = level.player [[ fn ]]();
		r2 = level.player2 [[ fn ]]();
		report( "flash lesson break test (both had 3; player 1 threw one, player 2 none): player 1 " + r1 + ", player 2 " + r2 );
		check( r1 && !r2, "each player's flash lesson goes by his own throw" );
	}
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ Wolverines!

smokebtr()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	wait 2;
	smoke = "smoke_grenade_american";
	fn = hint_break( "hint_get_smoke" );
	if ( isdefined( fn ) )
	{
		foreach ( player in level.players )
			if ( !player HasWeapon( smoke ) )
				player GiveWeapon( smoke );
		level.player SetWeaponAmmoStock( smoke, 2 );
		level.player2 SetWeaponAmmoStock( smoke, 0 );
		wait 0.1;
		r1 = level.player [[ fn ]]();
		r2 = level.player2 [[ fn ]]();
		report( "get-smoke hint break test (player 1 has 2, player 2 none): player 1 " + r1 + ", player 2 " + r2 );
		check( r1 && !r2, "each player's get-smoke hint goes by his own smoke" );
	}
	flag_on( "start_house_destroyer" );
	wait 1;
	flag_on( "got_visual_on_crash" );
	wait 8;	// Raptor's line, then the level waits for a smoke grenade
	level.player2 SetOffhandSecondaryClass( "smoke" );
	level.player2 SetWeaponAmmoStock( smoke, 2 );
	level.qa_smoke_seen = 0;
	level thread watch_smoke_reaction();
	report( "player 2 throws a smoke grenade (his pad's LB); player 1 " + level.player GetWeaponAmmoStock( smoke ) + " smoke, player 2 " + level.player2 GetWeaponAmmoStock( smoke ) );
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 256 0 0 0" );
	wait 0.6;
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0 0" );
	for ( t = 0; t < 10 && !level.qa_smoke_seen; t += 0.1 )
		wait 0.1;
	report( "after the throw: player 2 has " + level.player2 GetWeaponAmmoStock( smoke ) + " smoke left; the level saw it: " + level.qa_smoke_seen + " (" + level.qa_smoke_how + "), flag threw_smoke " + flag( "threw_smoke" ) );
	check( level.player2 GetWeaponAmmoStock( smoke ) < 2, "player 2 threw a smoke grenade" );
	check( level.qa_smoke_seen, "the level reacts to player 2's smoke (too far from the BMP, or the BMP smoked)" );
	report( "done" );
}

watch_smoke_reaction()
{
	level.qa_smoke_how = "nothing";
	msg = level waittill_any_return( "btr_smoke_too_far", "btr_smoke_starting", "btr_smoked", "threw_smoke" );
	level.qa_smoke_how = msg;
	level.qa_smoke_seen = 1;
}

// ------------------------------------------------------------------------------------------------ Of Their Own Accord, the Barrett

barrettp2()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	wait 2;
	trigger = GetEnt( "barrett_trigger", "targetname" );
	turret = GetEnt( "turret2", "targetname" );
	check( isdefined( trigger ) && isdefined( turret ), "the Barrett's trigger and turret are there" );
	if ( !isdefined( trigger ) || !isdefined( turret ) )
	{
		report( "done" );
		return;
	}
	o1 = level.player.origin;
	o2 = level.player2.origin;
	trigger notify( "trigger", level.player2 );
	wait 1.5;
	owner = turret GetTurretOwner();
	who = "nobody";
	if ( isdefined( owner ) )
		who = maps\_mw2coop_qa::player_name( owner );
	report( "player 2 used the Barrett: on it " + who + "; player 1 moved " + int( Distance( level.player.origin, o1 ) ) + ", player 2 moved " + int( Distance( level.player2.origin, o2 ) ) );
	check( isdefined( owner ) && owner == level.player2, "the player who used the Barrett (player 2) is on it" );
	// off again: the user's Use key (X on his pad)
	if ( isdefined( owner ) )
	{
		pad = "mw2coop_qa_pad1";
		if ( owner == level.player2 )
			pad = "mw2coop_qa_pad2";
		SetDvar( pad, "0 0 0 0 16384 0 0 0" );
		wait 0.8;
		SetDvar( pad, "0 0 0 0 0 0 0 0" );
		wait 2;
		owner = turret GetTurretOwner();
		report( "after his Use key: on the Barrett " + isdefined( owner ) );
	}
	wait 1;
	trigger notify( "trigger", level.player );
	wait 2;
	owner = turret GetTurretOwner();
	who = "nobody";
	if ( isdefined( owner ) )
		who = maps\_mw2coop_qa::player_name( owner );
	report( "player 1 used the Barrett: on it " + who + "; player 2 holds " + level.player2 GetCurrentWeapon() + " (player 1 " + level.player GetCurrentWeapon() + ")" );
	check( isdefined( owner ) && owner == level.player, "player 1 is on the Barrett" );
	check( level.player2 GetCurrentWeapon() != "none", "player 2 keeps a weapon in his hands while player 1 is on the Barrett" );
	// player 2 fires his weapon on his pad's right trigger: it fires only if his weapons were not turned off with player 1's
	wait 1;
	clip = level.player2 GetCurrentWeaponClipAmmo();
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 255 0" );
	wait 1.2;
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0 0" );
	wait 0.5;
	report( "player 2 pulls his trigger while player 1 is on the Barrett: clip " + clip + " -> " + level.player2 GetCurrentWeaponClipAmmo() );
	check( level.player2 GetCurrentWeaponClipAmmo() < clip, "player 2 can fire while player 1 is on the Barrett" );
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ Second Sun

dcempwalk()
{
	level.mw2coop_no_follow = true;
	wait 6;
	base = level.player GetPlayerAngles();
	s1 = 0;
	s2 = 0;
	// a direction both can walk in (player 1 stood against something on the street start)
	foreach ( turn in StrTok( "0 180 90 270", " " ) )
	{
		angles = ( 0, base[ 1 ] + int( turn ), 0 );
		level.player SetPlayerAngles( angles );
		level.player2 SetPlayerAngles( angles );
		wait 0.2;
		SetDvar( "mw2coop_qa_pad1", "0 32767 0 0 0 0 0 0" );
		SetDvar( "mw2coop_qa_pad2", "0 32767 0 0 0 0 0 0" );
		wait 1;
		s1 = 0;
		s2 = 0;
		for ( i = 0; i < 10; i++ )
		{
			v = level.player GetVelocity();
			s1 += Length( ( v[ 0 ], v[ 1 ], 0 ) );
			v = level.player2 GetVelocity();
			s2 += Length( ( v[ 0 ], v[ 1 ], 0 ) );
			wait 0.1;
		}
		SetDvar( "mw2coop_qa_pad1", "0 0 0 0 0 0 0 0" );
		SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0 0" );
		s1 = s1 / 10;
		s2 = s2 / 10;
		report( "walking forward on the street (turned " + turn + "): player 1 " + int( s1 ) + " units/s, player 2 " + int( s2 ) + " units/s" );
		if ( s1 > 50 && s2 > 50 )
			break;
		wait 0.5;
	}
	check( s1 > 50 && s2 > 50, "both walked" );
	check( s1 > 50 && s2 > s1 * 0.9 && s2 < s1 * 1.1, "both walk at the street's slowed speed (within 10 %)" );
	report( "done" );
}

// ------------------------------------------------------------------------------------------------ maps\_mw2coop: weapons during a down, friendly fire

downsync()
{
	level.mw2coop_no_follow = true;
	maps\_mw2coop_qa::wait_intro();
	wait 3;
	weapon = undefined;
	foreach ( w in level.player GetWeaponsListPrimaries() )
		if ( WeaponClass( w ) != "pistol" )
			weapon = w;
	check( isdefined( weapon ), "player 1 has a primary weapon that is no pistol" );
	if ( !isdefined( weapon ) )
	{
		report( "done" );
		return;
	}
	level.player TakeWeapon( weapon );
	wait 1;
	report( "player 1 lost " + weapon + ": player 2 has it " + level.player2 HasWeapon( weapon ) );
	level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
	wait 1.5;
	report( "player 2 downed " + maps\_mw2coop::is_downed( level.player2 ) + "; the level gives player 1 " + weapon );
	level.player GiveWeapon( weapon );
	wait 1.5;
	level.player2 notify( "revive_success" );
	level.player2 ent_flag_clear( "coop_downed" );
	wait 2;
	report( "player 2 revived (downed " + maps\_mw2coop::is_downed( level.player2 ) + "): he has " + weapon + " " + level.player2 HasWeapon( weapon ) );
	check( level.player2 HasWeapon( weapon ), "the weapon the level gave player 1 while player 2 was down reaches player 2" );
	report( "done" );
}

ffscale()
{
	if ( GetDvar( "mw2coop_qa_phase" ) == "ffscale_restarted" )
	{
		wait 1;
		report( "after the restart: g_friendlyfireDamageScale " + GetDvar( "g_friendlyfireDamageScale" ) + " (mw2coop_ff '" + GetDvar( "mw2coop_ff" ) + "')" );
		check( GetDvarFloat( "g_friendlyfireDamageScale" ) == 1, "friendly fire is on again after a restart that came while it was off" );
		SetDvar( "mw2coop_qa_phase", "done" );
		report( "done" );
		return;
	}
	maps\_mw2coop_qa::wait_intro();
	wait 2;
	SetSavedDvar( "g_friendlyfireDamageScale", 0 );
	report( "friendly fire on (mw2coop_ff '" + GetDvar( "mw2coop_ff" ) + "'), scale left at " + GetDvar( "g_friendlyfireDamageScale" ) + " as the Team Player ride leaves it; the mission is failed" );
	SetDvar( "mw2coop_qa_phase", "ffscale_restarted" );
	level thread maps\_mw2coop::mission_failed();
}

ffhop()
{
	if ( GetDvar( "mw2coop_qa_phase" ) == "ffhop_next" )
	{
		wait 1;
		report( "next level " + level.script + ": g_friendlyfireDamageScale " + GetDvar( "g_friendlyfireDamageScale" ) + " (mw2coop_ff '" + GetDvar( "mw2coop_ff" ) + "')" );
		check( GetDvarFloat( "g_friendlyfireDamageScale" ) == 1, "friendly fire is on in the next level after a level that left it off" );
		SetDvar( "mw2coop_qa_phase", "done" );
		report( "done" );
		return;
	}
	maps\_mw2coop_qa::wait_intro();
	wait 2;
	SetSavedDvar( "g_friendlyfireDamageScale", 0 );
	report( "level " + level.script + ": scale left at " + GetDvar( "g_friendlyfireDamageScale" ) + "; the mission is completed" );
	SetDvar( "mw2coop_qa_phase", "ffhop_next" );
	maps\_mw2coop::mission_complete();
}
