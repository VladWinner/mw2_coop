// mw2coop QA: scripted checks that run inside the game on both PCs (lockstep, so they must be
// deterministic). Chosen before launch with the dvar mw2coop_qa on BOTH machines:
//   intro    - player 2 joins player 1 at the range, same weapons, gets the rifle with player 1
//   restart  - reach a checkpoint, down both players, expect a restart at that checkpoint
//   end      - (launch with "start end") run the ending, expect both machines on the next mission
//   resume   - (qa_pair.sh resume @continue) both machines start at the saved checkpoint
//   god      - with god mode on, heavy damage downs neither player
//   ride     - (invasion) player 2 rides in player 1's vehicle and gets out with him
//   turret   - (roadkill, start "ride") hand the Humvee turret from player 1 to player 2
//   board    - (roadkill, start "convoy") first to board is the gunner, the other rides, swaps
//   rideend  - (roadkill, start "ride_later") both get off at the end of the ride and the crash
//   rkintro  - (roadkill, default start) both players in Shepherd's scene, together afterwards
//   leash    - player 2 far away comes back to player 1
//   snow     - (cliffhanger, start "snowmobile") own snowmobiles; the ending waits for player 2
//   snowpick - (cliffhanger, start "icepick") player 2 takes player 1's snowmobile first
//   climb    - (cliffhanger, default start) both players climb the ice wall on their own lanes,
//              make the big jump, Soap catches both, both climb to the top (bot input)
//   spawn    - (any map/start) where player 2 is during the first 12 s
//   takedown - (favela, default start) both ride the opening car (player 2 on the back seat),
//              both duck before Makarov's shots, both get out next to each other
//   takedown2 - same, but player 2 does not duck: Makarov hits him, not player 1
//   crashsite - (dcburning, start "crash") the same crash site at the end of dcburning
//   dcemp    - (dcemp, default start) both wake up in the crashed Black Hawk, both get a rifle,
//              both float at the ISS, both come back to the wreck and are let out
//   street   - (dcemp, start "street") the falling helicopters: the street is safe only once both
//              players have hidden (the bot moves them into the hiding place)
//   nrend    - (airport, start "escape") the ending at the van: Makarov shoots both players
//   nrend2   - same, player 1 at the van's door (the close ending), player 2 a little away
//   knife    - (ending, start "wakeup") the knife fight: both wake up, player 1 strikes Shepherd
//              and plays the scene, player 2 goes down and watches; knife2: player 2 strikes
//   oilrig   - (oilrig, default start) both ride an SDV underwater, both surface; player 1 does
//              the stealth kill, then both are pulled out of the water. oilrig2: player 2 kills
//   loadout  - (any map) after the level's loadout both players hold the same weapon
//   cover    - (airport, start "stairs") player 2 fires at Makarov's squad: the cover is blown,
//              Makarov shoots both players and the mission fails for both
//   boat     - (af_chase, default start) the Endgame boat chase: player 1 boards and drives,
//              player 2 rides as the gunner beside Price; the bot drives after the enemy boat.
//              boat2: player 2 boards first and drives
// Results go to the dvar mw2coop_qa_log, which the DLL writes to mw2coop.log on each PC.
#include common_scripts\utility;
#include maps\_utility;

main()
{
	// a scenario that ends the mission marks itself done, so the next map does not rerun it
	if ( GetDvar( "mw2coop_qa_phase" ) == "done" )
		return;
	level.mw2coop_qa_n = 0;
	maps\_mw2coop::state( "qa scenario " + GetDvar( "mw2coop_qa" ) + " started" );
	wait 1.5;
	maps\_mw2coop::state( "qa running" );	// let start points, regroup and weapon sync run
	switch ( GetDvar( "mw2coop_qa" ) )
	{
		case "intro":
			intro();
			break;
		case "restart":
			restart();
			break;
		case "end":
			ending();
			break;
		case "resume":
			resume();
			break;
		case "god":
			god();
			break;
		case "ride":
			ride();
			break;
		case "turret":
			turret();
			break;
		case "board":
			board();
			break;
		case "rideend":
			rideend();
			break;
		case "rkintro":
			rkintro();
			break;
		case "leash":
			leash();
			break;
		case "snow":
			snow();
			break;
		case "snowpick":
			snowpick();
			break;
		case "spawn":
			spawn_audit();
			break;
		case "takedown":
		case "takedown2":
			takedown();
			break;
		case "oilrig":
		case "oilrig2":
			oilrig();
			break;
		case "dcemp":
		case "crashsite":
			dcemp();
			break;
		case "street":
			street();
			break;
		case "nrend":
		case "nrend2":
			nrend();
			break;
		case "knife":
		case "knife2":
			knife();
			break;
		case "body":
			body();
			break;
		case "cover":
			cover();
			break;
		case "loadout":
			loadout();
			break;
		case "boat":
		case "boat2":
			boat();
			break;
		case "look":
			look();
			break;
		case "targets":
			targets();
			break;
		case "pickups":
			pickups();
			break;
		case "climb":
			// in maps\_mw2coop_climb_qa, loaded on Cliffhanger only (it names maps\_climb, which
			// other maps' zones do not have)
			if ( isdefined( level.mw2coop_qa_climb ) )
				[[ level.mw2coop_qa_climb ]]();
			else
				report( "climb: not on Cliffhanger" );
			break;
		default:
			report( "unknown scenario " + GetDvar( "mw2coop_qa" ) );
	}
}

report( text )
{
	level.mw2coop_qa_n++;
	SetDvar( "mw2coop_qa_log", level.mw2coop_qa_n + ": " + text );
	wait 0.3;	// the DLL polls every 250 ms
}

check( ok, text )
{
	if ( ok )
		report( "PASS " + text );
	else
		report( "FAIL " + text );
}

weapons_text( player )
{
	text = "";
	foreach ( weapon in player GetWeaponsListAll() )
		text += weapon + " ";
	return text;
}

common_checks()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	check( !is_specialop(), "campaign mode, not Spec Ops" );
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 160, "player 2 next to player 1 (" + int( d ) + " units)" );
	report( "start point " + level.start_point + ", p1 weapons: " + weapons_text( level.player ) + "| p2 weapons: " + weapons_text( level.player2 ) );
}

intro()
{
	common_checks();
	check( level.player2 GetWeaponsListAll().size == level.player GetWeaponsListAll().size, "same weapon count" );
	// what picking the rifle off the range table does to player 1
	level.player GiveWeapon( "m4_grunt" );
	level.player SwitchToWeapon( "m4_grunt" );
	wait 1;
	check( level.player2 HasWeapon( "m4_grunt" ), "player 2 got the rifle with player 1" );
	report( "done" );
}

restart()
{
	phase = GetDvar( "mw2coop_qa_phase" );
	if ( phase == "" )
	{
		common_checks();
		flag_set( "firing_range_hip_and_ads_done" );
		wait 1;
		check( GetDvar( "start" ) == "timed_ads", "checkpoint timed_ads reached (start dvar '" + GetDvar( "start" ) + "')" );
		SetDvar( "mw2coop_qa_phase", "after_restart" );
		report( "downing player 2" );
		level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
		wait 1;
		check( level.player2 ent_flag( "coop_downed" ), "player 2 is down, not dead" );
		wait level.player_downed_death_buffer_time + 1;
		report( "downing player 1" );
		level.player DoDamage( level.player.health + 500, level.player.origin );
		wait 1;
		check( flag( "coop_alldowned" ), "both down -> all downed" );
		report( "waiting for the restart" );
		return;
	}
	if ( phase == "after_restart" )
	{
		SetDvar( "mw2coop_qa_phase", "" );
		check( level.start_point == "timed_ads", "restarted at checkpoint timed_ads (" + level.start_point + ")" );
		common_checks();
		check( !flag( "coop_alldowned" ), "players are up again" );
		report( "done" );
	}
}

ending()
{
	if ( GetDvar( "mw2coop_qa_phase" ) == "after_end" )
	{
		SetDvar( "mw2coop_qa_phase", "" );
		check( level.script == "roadkill", "next mission loaded (" + level.script + ")" );
		common_checks();
		report( "done" );
		return;
	}
	common_checks();
	SetDvar( "mw2coop_qa_phase", "after_end" );
	flag_set( "player_exiting_course_00" );
	wait 1;
	flag_set( "player_exiting_course" );
	wait 1;
	flag_set( "player_exiting_course_02" );
	report( "ending running" );
	wait 15;
	report( "FAIL still on " + level.script + " 15 s after the ending" );
}

// Continued session (qa_pair.sh resume @continue): both machines start at the saved checkpoint.
resume()
{
	common_checks();
	check( isdefined( level.start_point ) && level.start_point != "default", "level started at the saved checkpoint '" + level.start_point + "'" );
	report( "done" );
}

// God mode (on by default, see maps\_mw2coop::god_mode()): heavy damage downs nobody.
god()
{
	common_checks();
	check( maps\_mw2coop::god_mode_on(), "god mode on (mw2coop_god '" + GetDvar( "mw2coop_god" ) + "')" );
	wait 0.5;
	level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
	level.player DoDamage( level.player.health + 500, level.player.origin );
	wait 1;
	check( IsAlive( level.player ) && IsAlive( level.player2 ), "both players alive after heavy damage" );
	check( !level.player ent_flag( "coop_downed" ) && !level.player2 ent_flag( "coop_downed" ), "neither player downed" );
	check( level.player.health > 0 && level.player2.health > 0, "health " + level.player.health + " / " + level.player2.health );
	report( "done" );
}

// Passenger ride (Wolverines! opening: start point default on invasion): player 2 rides on a
// free seat of player 1's vehicle and gets out with him.
ride()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !level.player IsLinked() && t < 30 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( level.player IsLinked(), "player 1 seated" );
	wait 1.5;
	check( level.player2 IsLinked(), "player 2 seated too" );
	check( Distance( level.player.origin, level.player2.origin ) < 120, "player 2 in the same vehicle (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	wait 6;
	check( Distance( level.player.origin, level.player2.origin ) < 120, "still together while driving (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	t = 0;
	while ( level.player IsLinked() && t < 120 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( !level.player IsLinked(), "player 1 got out" );
	wait 1.5;
	check( !level.player2 IsLinked(), "player 2 got out" );
	check( Distance( level.player.origin, level.player2.origin ) < 160, "player 2 next to player 1 (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	report( "done" );
}

// Turret hand-over experiment (roadkill, start point "ride": player 1 starts on the Humvee
// turret). Takes player 1 off with a second UseBy and puts player 2 on.
turret()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !isdefined( level.player_turret ) && t < 20 )
	{
		wait 0.5;
		t += 0.5;
	}
	turret = level.player_turret;
	check( isdefined( turret ), "player turret exists" );
	wait 2;
	check( turret GetTurretOwner() == level.player, "player 1 is the gunner" );

	level.player EnableTurretDismount();
	turret UseBy( level.player );
	wait 0.5;
	owner = turret GetTurretOwner();
	check( !isdefined( owner ), "second UseBy takes player 1 off the turret" );
	report( "player 1 using a turret: " + level.player IsUsingTurret() + ", linked: " + level.player IsLinked() );

	level.player2 PlayerLinkToDelta( level.player_humvee, "tag_player", 0.35, 360, 360, 45, 30, true );
	turret MakeUsable();
	turret UseBy( level.player2 );
	turret MakeUnusable();
	level.player2 DisableTurretDismount();
	wait 0.5;
	check( turret GetTurretOwner() == level.player2, "player 2 is the gunner now" );
	report( "player 2 using a turret: " + level.player2 IsUsingTurret() + ", linked: " + level.player2 IsLinked() );
	wait 3;
	check( turret GetTurretOwner() == level.player2, "player 2 still on the turret after 3 s" );
	report( "done" );
}

// Team Player boarding (roadkill, start point "convoy"): player 2 reaches the seat first and
// becomes the gunner, player 1 boards the rear seat, the convoy leaves, the seats swap and swap
// back (called directly: a script cannot hold the Use button), both stay in the moving Humvee.
board()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !isdefined( level.chair ) && t < 60 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( isdefined( level.chair ), "glowing seat spawned" );
	flag_set( "player_climbs_stairs" );	// a trigger player 1 would touch on the stairs: the convoy moment
	// player 2 waits at the seat until someone boards
	t = 0;
	while ( !isdefined( level.mw2coop_gunner ) && t < 90 )
	{
		if ( isdefined( level.chair ) )
			level.player2 SetOrigin( level.chair.origin + ( 0, 0, -10 ) );
		wait 0.1;
		t += 0.1;
	}
	check( isdefined( level.mw2coop_gunner ) && level.mw2coop_gunner == level.player2, "player 2 boarded first and is the gunner" );
	t = 0;
	while ( !( isdefined( level.player_turret ) && isdefined( level.player_turret GetTurretOwner() ) ) && t < 15 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( isdefined( level.player_turret ) && level.player_turret GetTurretOwner() == level.player2, "player 2 on the turret" );
	check( !flag( "player_gets_in" ), "convoy waits for player 1" );
	wait 1;
	level.player SetOrigin( level.player_humvee GetTagOrigin( "tag_seat_rb_attach" ) + ( 0, 0, -10 ) );
	wait 1;
	check( level.player IsLinked(), "player 1 boarded the rear seat" );
	check( flag( "player_gets_in" ), "convoy leaves once both are in" );
	wait 2;

	level.player_humvee maps\_mw2coop_roadkill::swap( level.mw2coop_roadkill_seat );
	wait 1;
	check( level.player_turret GetTurretOwner() == level.player && level.player2 IsLinked(), "swap: player 1 gunner, player 2 seated" );
	level.player_humvee maps\_mw2coop_roadkill::swap( level.mw2coop_roadkill_seat );
	wait 1;
	check( level.player_turret GetTurretOwner() == level.player2 && level.player IsLinked(), "swap back: player 2 gunner, player 1 seated" );

	wait 6;
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 150 && level.player IsLinked() && level.player2 IsLinked(), "both riding the moving Humvee (" + int( d ) + " units apart)" );
	report( "done" );
}

// Team Player ride end (roadkill, start point "ride_later": player 1 starts on the turret):
// player 2 rides on a seat, both get off when the ride ends, player 2 lands next to player 1.
rideend()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 3;
	check( isdefined( level.player_turret ) && level.player_turret GetTurretOwner() == level.player, "player 1 on the turret" );
	check( Distance( level.player.origin, level.player_turret.origin ) < 150, "player 1 at the turret (" + int( Distance( level.player.origin, level.player_turret.origin ) ) + " units)" );
	check( level.player2 IsLinked(), "player 2 seated" );
	report( "t 3: player 1 " + int( Distance( level.player.origin, level.player_turret.origin ) ) + " units from the turret, player 2 " + int( Distance( level.player2.origin, level.player_turret.origin ) ) );
	t = 0;
	while ( !flag( "player_is_dismounted" ) && t < 180 )
	{
		wait 0.5;
		t += 0.5;
		if ( t == int( t / 2 ) * 2 && t <= 12 || t == int( t / 10 ) * 10 )
			report( "t " + t + ": turret " + level.player IsUsingTurret() + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " units, linked " + level.player IsLinked() + "/" + level.player2 IsLinked()
				+ ", flags " + flag_text( "lead_vehicle_speeds_up" ) + flag_text( "resume_the_path" ) + flag_text( "player_is_pushing_truck" ) + flag_text( "player_goes_in_reverse" ) + flag_text( "player_vehicle_wipes_out" ) );
	}
	check( flag( "player_is_dismounted" ), "ride ended in the crash (" + t + " s)" );
	wait 1;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both off the Humvee" );
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 200, "player 2 next to player 1 after the crash (" + int( d ) + " units)" );
	report( "done" );
}

flag_text( name )
{
	if ( !isdefined( level.flag[ name ] ) )
		return "?";
	if ( flag( name ) )
		return "1";
	return "0";
}

// QA probe, started from maps\_mw2coop::main(): when player 1's link changes in the first 3 s.
link_probe()
{
	text = "";
	last = -1;
	for ( i = 0; i < 60; i++ )
	{
		now = 0;
		if ( level.player IsLinked() )
			now = 1;
		if ( now != last )
			text += " frame " + i + ": " + now + " @" + int( level.player.origin[ 0 ] ) + "," + int( level.player.origin[ 1 ] );
		last = now;
		wait 0.05;
	}
	maps\_mw2coop::state( "link probe:" + text );
}

// Team Player opening (roadkill, default start): during Shepherd's "get on the line" scene both
// players are on the view rig; afterwards both are free and together.
rkintro()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	both = false;
	while ( !flag( "get_on_the_line" ) && t < 90 )
	{
		if ( level.player IsLinked() && level.player2 IsLinked() )
			both = true;
		wait 0.25;
		t += 0.25;
	}
	check( flag( "get_on_the_line" ), "Shepherd's scene ended (" + t + " s)" );
	check( both, "both players were in the scene" );
	wait 1;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both free after the scene" );
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 200, "player 2 next to player 1 (" + int( d ) + " units)" );
	report( "done" );
}

// Leash (S.S.D.D.): player 2 is put 4000 units away; within leash_time() + 2 s he is back
// next to player 1.
leash()
{
	common_checks();
	level.player2 SetOrigin( level.player.origin + ( 4000, 0, 0 ) );
	wait 1;
	check( Distance( level.player.origin, level.player2.origin ) > 3000, "player 2 is far away (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	wait maps\_mw2coop::leash_time() + 1;
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 200, "leash brought player 2 back (" + int( d ) + " units)" );
	report( "done" );
}

// Cliffhanger snowmobiles (start point "snowmobile"): each player on his own snowmobile; at the
// helicopter the ending waits while player 2 is still on his snowmobile and goes on once he is
// off next to player 1. (A script cannot drive, so the ride itself is skipped by setting the
// level's flags.)
snow()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 3;
	check( isdefined( level.player.vehicle ), "player 1 on a snowmobile" );
	check( isdefined( level.player2.vehicle ) && ( !isdefined( level.player.vehicle ) || level.player2.vehicle != level.player.vehicle ), "player 2 on his own snowmobile" );
	foreach ( name in strtok( "player_reaches_hilltop there_is_chopper ending_heli_flies_in", " " ) )
	{
		qa_flag_set( name );
		wait 1;
	}
	level.player player_dismount_vehicle();
	wait 8;	// the radio lines between the flags
	SetDvar( "mw2coop_qa_phase", "done" );
	qa_flag_set( "end_begins" );
	wait 5;
	check( !level.mw2coop.completing, "ending waits while player 2 is on his snowmobile" );
	level.player2 player_dismount_vehicle();
	wait 1;
	level.player2 maps\_mw2coop::teleport_to_partner( level.player );
	t = 0;
	while ( !level.mw2coop.completing && t < 15 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( level.mw2coop.completing, "mission ends once player 2 is there (" + t + " s)" );
	report( "done" );
}

qa_flag_set( name )
{
	if ( !isdefined( level.flag[ name ] ) )
		flag_init( name );
	flag_set( name );
}

// Cliffhanger, start point "icepick": after Soap MacTavish's icepick fight (level.price in the scripts) two snowmobiles wait; player 2
// takes the one the level made for player 1, player 1 takes the other, and the level goes on
// with player 1's as level.player_snowmobile.
snowpick()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !flag( "player_snowmobile_available" ) && t < 90 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_snowmobile_available" ), "snowmobiles available (" + t + " s)" );
	wait 1;
	check( isdefined( level.mw2coop_partner_snowmobile ), "second snowmobile spawned" );
	level.player2 player_mount_vehicle( level.player_snowmobile );
	wait 1;
	level.player player_mount_vehicle( level.mw2coop_partner_snowmobile );
	t = 0;
	while ( !flag( "player_rides_snowmobile" ) && t < 10 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( flag( "player_rides_snowmobile" ), "the level goes on (player_rides_snowmobile)" );
	check( isdefined( level.player.vehicle ) && level.player_snowmobile == level.player.vehicle, "level.player_snowmobile is player 1's" );
	check( isdefined( level.player2.vehicle ) && level.player2.vehicle != level.player.vehicle, "player 2 on the other snowmobile" );
	report( "done" );
}

// Spawn audit (any map, any start point): where player 2 ends up after the level start, over
// 12 s. PASS when he is alive, up, within 300 units of player 1 and not far below him, and
// either standing on ground or held by something (linked).
bias_group( player )
{
	group = player GetThreatBiasGroup();
	if ( !isdefined( group ) )
		return "none";
	return group;
}

spawn_audit()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	starts = "";
	if ( isdefined( level.start_functions ) )
		foreach ( s in level.start_functions )
			starts += s[ "name" ] + " ";
	mode = "undefined";
	if ( isdefined( level.level_mode ) )
		mode = level.level_mode;
	report( "start point " + level.start_point + ", level_mode " + mode + ", starts: " + starts );
	samples = [];
	samples[ 0 ] = 0.5;
	samples[ 1 ] = 3;
	samples[ 2 ] = 10.5;
	waited = 0;
	foreach ( at in samples )
	{
		wait at - waited;
		waited = at;
		report( "t+" + ( at + 1.5 ) + " " + spawn_text() );
	}
	p1 = level.player;
	p2 = level.player2;
	d = Distance( p1.origin, p2.origin );
	dz = p2.origin[ 2 ] - p1.origin[ 2 ];
	check( IsAlive( p2 ) && !maps\_mw2coop::is_downed( p2 ), "player 2 alive and up" );
	check( d < 300, "player 2 near player 1 (" + int( d ) + " units)" );
	check( dz > -150, "player 2 not below player 1 (dz " + int( dz ) + ")" );
	check( maps\_mw2coop::is_standing( p2 ) || p2 IsLinked(), "player 2 on ground or held" );
	kit_checks();
	report( "done" );
}

spawn_text()
{
	p1 = level.player;
	p2 = level.player2;
	return "start " + level.start_point + ", d " + int( Distance( p1.origin, p2.origin ) ) + ", dz " + int( p2.origin[ 2 ] - p1.origin[ 2 ] )
		+ ", p1 " + int( p1.origin[ 0 ] ) + "," + int( p1.origin[ 1 ] ) + "," + int( p1.origin[ 2 ] ) + " linked " + p1 IsLinked() + " standing " + maps\_mw2coop::is_standing( p1 )
		+ ", p2 linked " + p2 IsLinked() + " standing " + maps\_mw2coop::is_standing( p2 ) + " alive " + IsAlive( p2 );
}

// Takedown opening car (default start). Names no favela script (they are not in other zones):
// the bot ducks through the crouch command's notify, as NotifyOnPlayerCommand would send it.
takedown()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 2;
	p1 = level.player;
	p2 = level.player2;
	check( p1 IsLinked() && p2 IsLinked(), "both in the car (linked " + p1 IsLinked() + " " + p2 IsLinked() + ")" );
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( d > 20 && d < 80, "player 2 on the back seat (eyes " + int( d ) + " units apart)" );
	flag_wait( "car_getting_shot" );
	if ( GetDvar( "mw2coop_qa" ) == "takedown2" )
	{
		p1 notify( "go_crouch" );
		for ( t = 0; t < 8 && !isdefined( p2.mw2coop_shot ); t += 0.1 )
			wait 0.1;
		check( isdefined( p2.mw2coop_shot ) && !isdefined( p1.mw2coop_shot ), "Makarov hits player 2 (standing), not player 1 (ducking)" );
		report( "done" );
		return;
	}
	foreach ( player in level.players )
		player notify( "go_crouch" );
	wait 1;
	check( isdefined( p1.mw2coop_ducking ) && p1.mw2coop_ducking && isdefined( p2.mw2coop_ducking ) && p2.mw2coop_ducking, "both duck" );
	flag_wait( "start_chase" );
	check( !isdefined( p1.mw2coop_shot ) && !isdefined( p2.mw2coop_shot ), "nobody shot while ducking" );
	foreach ( player in level.players )
		player notify( "go_stand" );
	for ( t = 0; t < 10 && ( p1 IsLinked() || p2 IsLinked() ); t += 0.25 )
		wait 0.25;
	wait 0.5;
	check( !p1 IsLinked() && !p2 IsLinked(), "both out of the car" );
	d = Distance( p1.origin, p2.origin );
	check( d < 120 && abs( p1.origin[ 2 ] - p2.origin[ 2 ] ) < 30, "out next to each other (" + int( d ) + " units)" );
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// The Only Easy Day... opening (default start). Names no oilrig script (not in other zones).
// The strike is forced through player.mw2coop_qa_melee (the bot cannot aim at the guard).
oilrig()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	flag_wait( "player_attached_to_sdv" );
	wait 1;
	check( p1 IsLinked() && p2 IsLinked(), "both on an SDV (linked " + p1 IsLinked() + " " + p2 IsLinked() + ")" );
	d = Distance( p1.origin, p2.origin );
	report( "underwater, players " + int( d ) + " units apart" );
	flag_wait( "player_is_done_swimming" );
	wait 2;
	d = Distance( p1.origin, p2.origin );
	check( p1 IsLinked() && p2 IsLinked() && d < 600, "both surfaced and swimming (" + int( d ) + " units apart, p1 " + p1.origin + " p2 " + p2.origin + ")" );
	killer = p1;
	other = p2;
	if ( GetDvar( "mw2coop_qa" ) == "oilrig2" )
	{
		killer = p2;
		other = p1;
	}
	wait 3;
	killer.mw2coop_qa_melee = true;
	for ( t = 0; t < 60 && !flag( "player_done_being_helped_from_water" ); t += 0.25 )
		wait 0.25;
	check( flag( "player_done_being_helped_from_water" ), "the killer is out of the water" );
	for ( t = 0; t < 40 && !isdefined( other.mw2coop_out_of_water ); t += 0.25 )
		wait 0.25;
	check( isdefined( other.mw2coop_out_of_water ), "the other player is pulled out after him (" + t + " s)" );
	wait 1;
	d = Distance( p1.origin, p2.origin );
	check( !p1 IsLinked() && !p2 IsLinked() && d < 300 && abs( p1.origin[ 2 ] - p2.origin[ 2 ] ) < 30, "both out on the platform together (" + int( d ) + " units, p1 " + p1.origin + " linked " + p1 IsLinked() + ", p2 " + p2.origin + " linked " + p2 IsLinked() + ")" );
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// Second Sun opening (default start). Names no dcemp script (not in other zones).
dcemp()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	flag_wait( "crash_actors_ready" );
	wait 0.5;
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( p1 IsLinked() && p2 IsLinked() && d > 16 && d < 90, "both in the wreck (eyes " + int( d ) + " units apart)" );
	flag_wait( "give_player_weapon" );
	wait 1;
	check( p1 HasWeapon( "m4m203_eotech" ) && p2 HasWeapon( "m4m203_eotech" ), "both got a rifle" );
	if ( level.script != "dcemp" )
	{
		// dcburning ends here: the crash site is its last scene
		flag_wait( "end_sequence_starting" );
		check( p1 IsLinked() && p2 IsLinked(), "both still in the wreck at the end" );
		report( "done" );
		return;
	}
	flag_wait( "iss_fx" );
	wait 3;
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( p1 IsLinked() && p2 IsLinked() && d < 150, "both at the ISS (" + int( d ) + " units apart)" );
	flag_wait( "iss_done" );
	wait 4;
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( p1 IsLinked() && p2 IsLinked() && d < 120, "both back in the wreck (" + int( d ) + " units apart)" );
	for ( t = 0; t < 60 && ( p1 IsLinked() || p2 IsLinked() ); t += 0.25 )
		wait 0.25;
	wait 1;
	d = Distance( p1.origin, p2.origin );
	check( !p1 IsLinked() && !p2 IsLinked() && d < 150, "both out of the wreck (" + int( d ) + " units, after " + t + " s)" );
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// Second Sun street (start "street"). Names no dcemp script (not in other zones).
street()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	hide = getEntArrayWithFlag( "street_player_hide" );
	check( hide.size > 0, "hiding place trigger found (" + hide.size + ")" );
	if ( !hide.size )
		return;
	spot = hide[ 0 ].origin;
	foreach ( guy in level.team )
		guy ent_flag_wait( "street_hide" );
	report( "team hidden, hiding place at " + spot );
	level.mw2coop_no_follow = true;	// the bot teleports player 1: player 2 must not follow
	p1 SetOrigin( PlayerPhysicsTrace( spot + ( 0, 0, 40 ), spot - ( 0, 0, 200 ) ) );
	wait 2;
	check( isdefined( p1.mw2coop_street_hidden ) && !flag( "street_safe" ), "player 1 hidden, street not safe while player 2 is out" );
	p2 SetOrigin( PlayerPhysicsTrace( spot + ( 24, 0, 40 ), spot + ( 24, 0, -200 ) ) );
	for ( t = 0; t < 5 && !flag( "street_safe" ); t += 0.1 )
		wait 0.1;
	check( flag( "street_safe" ), "street safe once both are hidden" );
	wait 3;
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// No Russian ending (start "escape"). Names no airport script (not in other zones): the bot puts
// both players by the van's ending node and watches Makarov's shots.
nrend()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	node = getstruct( "escape_ending_node", "targetname" );
	check( isdefined( node ), "ending node found" );
	level.mw2coop_no_follow = true;
	// follow Makarov along the escape (he waits for the player), then stand by the van
	for ( t = 0; t < 150 && !flag( "escape_sequence_go" ); t += 0.5 )
	{
		foreach ( i, player in level.players )
		{
			if ( player IsLinked() || !isdefined( level.makarov ) )
				continue;
			if ( !flag( "escape_sequence_reach" ) )
			{
				back = AnglesToForward( ( 0, level.makarov.angles[ 1 ], 0 ) ) * -140;
				right = AnglesToRight( ( 0, level.makarov.angles[ 1 ], 0 ) ) * ( 40 * ( i * 2 - 1 ) );
				at = level.makarov.origin + back + right;
				if ( Distance( player.origin, at ) > 220 )
					player SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 100 ) ) );
			}
			else if ( GetDvar( "mw2coop_qa" ) == "nrend2" && player == level.player && isdefined( level.escape_van_dummy ) )
			{
				// where the close ending's player rig starts, facing the van
				van = level.escape_van_dummy;
				anime = level.scr_anim[ "player_ending" ][ "end_player_shot" ];
				at = GetStartOrigin( van GetTagOrigin( "origin_animate_jnt" ), van GetTagAngles( "origin_animate_jnt" ), anime );
				if ( Distance( player.origin, at ) > 20 )
					player SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 100 ) ) );
				player SetPlayerAngles( VectorToAngles( ( van.origin[ 0 ], van.origin[ 1 ], at[ 2 ] ) - at ) );
			}
			else
			{
				right = AnglesToRight( ( 0, node.angles[ 1 ], 0 ) );
				at = node.origin + right * ( 50 * ( i * 2 - 1 ) );
				if ( Distance( player.origin, at ) > 60 )
					player SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 100 ) ) );
			}
		}
		wait 0.5;
	}
	check( flag( "escape_sequence_go" ), "the ending starts (" + t + " s)" );
	for ( t = 0; t < 60 && ( !isdefined( level.mw2coop_ending_left ) || level.mw2coop_ending_left > 0 ); t += 0.25 )
		wait 0.25;
	check( isdefined( level.mw2coop_ending_left ) && level.mw2coop_ending_left == 0, "Makarov shot both players (" + t + " s)" );
	wait 3;
	check( p1 IsLinked() && p2 IsLinked(), "both down (linked " + p1 IsLinked() + " " + p2 IsLinked() + ")" );
	report( "health p1 " + p1.health + " p2 " + p2.health );
	report( "done" );
}

// Endgame knife fight (ending map, start "wakeup"). Names no knife fight script (not in other
// zones): the bot strikes through player.mw2coop_qa_melee, holds the crawl and Use buttons
// through player.mw2coop_qa_hold and throws with the "throw" notify. Only the striker is moved
// (to Shepherd); the other player is left where he got up.
knife()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	striker = level.player;
	if ( GetDvar( "mw2coop_qa" ) == "knife2" )
		striker = level.player2;
	other = level.player;
	if ( striker == level.player )
		other = level.player2;
	foreach ( player in level.players )
	{
		player.mw2coop_qa_hold = [];
		player.mw2coop_qa_hold[ "use" ] = false;
	}
	wait 2;
	d = Distance( level.player GetEye(), level.player2 GetEye() );
	check( level.player IsLinked() && level.player2 IsLinked() && d < 140, "both wake up in the wreck (" + int( d ) + " units apart)" );
	flag_wait( "player_standing" );
	wait 2;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both up after the wake-up" );
	level.mw2coop_no_follow = true;
	for ( t = 0; t < 60 && !isdefined( level.shepherd ); t += 0.5 )
		wait 0.5;
	check( isdefined( level.shepherd ), "Shepherd there" );
	// Shepherd at the truck is hidden until the stumbling Shepherd walked there: a strike now
	// must not count (the scene would play with an invisible Shepherd)
	for ( t = 0; t < 2 && shepherd_hidden(); t += 0.25 )
		knife_strike_try( striker );
	check( !flag( "turn_buckle_start" ), "no strike while Shepherd is still hidden (hidden " + shepherd_hidden() + ")" );
	// the striker waits near the truck where he cannot see the stumbler (he stays the lead:
	// nearer to Shepherd), so only the other player's sight can start him
	for ( t = 0; t < 20 && !isdefined( level.shepherd_stumble ); t += 0.5 )
		wait 0.5;
	wait_spot = undefined;
	if ( isdefined( level.shepherd_stumble ) )
		wait_spot = sight_spot( level.shepherd_stumble, level.shepherd.origin, "150 250 350", false );
	check( isdefined( wait_spot ), "a spot by the truck without sight of the stumbling Shepherd" );
	if ( isdefined( wait_spot ) )
		striker SetOrigin( wait_spot );
	// a player walking on sets the map's trigger flag shepherd_can_run; the bot does not walk
	if ( shepherd_hidden() && flag_exist( "shepherd_can_run" ) && !flag( "shepherd_can_run" ) )
	{
		report( "QA sets shepherd_can_run (the bot does not walk)" );
		flag_set( "shepherd_can_run" );
	}
	// the stumbler runs off when a player sees him: the other player goes where he would see
	// him coming over the dunes, so the co-op sight check is what starts him
	level.mw2coop_qa_stumbler_runs = false;
	level thread stumbler_runs_watch();
	spot = undefined;
	if ( isdefined( level.shepherd_stumble ) )
		spot = sight_spot( level.shepherd_stumble, level.shepherd_stumble.origin, "150 250 350 500 700", true );
	check( isdefined( spot ), "a spot where the stumbling Shepherd can be seen" );
	if ( isdefined( spot ) )
	{
		other SetOrigin( spot );
		other SetPlayerAngles( VectorToAngles( level.shepherd_stumble GetEye() - other GetEye() ) );
		report( "other player at " + spot + ", " + int( Distance( spot, level.shepherd_stumble.origin ) ) + " units from the stumbler; sight: lead" + sight_text( level.mw2coop_lead, level.shepherd_stumble ) + " / other" + sight_text( other, level.shepherd_stumble ) );
	}
	for ( t = 0; t < 10 && shepherd_hidden() && !level.mw2coop_qa_stumbler_runs; t += 0.5 )
		wait 0.5;
	other thread watch_stumbler();
	check( level.mw2coop_qa_stumbler_runs, "the stumbling Shepherd runs off when the other player sees him (" + t + " s)" );
	for ( t = 0; t < 90 && shepherd_hidden(); t += 0.5 )
		wait 0.5;
	check( !shepherd_hidden(), "the stumbling Shepherd reached the truck (" + t + " s)" );
	for ( t = 0; t < 60 && !flag( "turn_buckle_start" ); t += 0.25 )
		knife_strike_try( striker );
	check( flag( "turn_buckle_start" ), "a player strikes Shepherd (" + t + " s)" );
	check( level.mw2coop_lead == striker, "the striker plays the scene" );
	wait 2.5;
	check( icon_hidden( striker ) && icon_hidden( other ) && GetDvar( "mw2coop_hide_names" ) == "1", "no partner icon or name on either screen" );
	check( isdefined( other.laststand ) && other.laststand && isdefined( other.mw2coop_awake ) && !other.mw2coop_awake, "the other player is down and out" );
	d = Distance( other.origin, level.shepherd.origin );
	check( d < 400, "and lies near the fight (" + int( d ) + " units from Shepherd)" );
	level thread knife_watch_report( other );

	// crawl to the revolver: alternate the two crawl buttons
	report( "crawl" );
	side = "left";
	for ( t = 0; t < 200 && !flag( "player_looks_at_knife" ) && !flag( "player_uses_knife" ); t += 0.15 )
	{
		striker.mw2coop_qa_hold[ "left" ] = side == "left";
		striker.mw2coop_qa_hold[ "right" ] = side == "right";
		if ( side == "left" )
			side = "right";
		else
			side = "left";
		wait 0.15;
	}
	striker.mw2coop_qa_hold[ "left" ] = false;
	striker.mw2coop_qa_hold[ "right" ] = false;
	report( "knife: tap Use (" + t + " s)" );
	level.mw2coop_qa_knife_out = false;
	level thread knife_out_watch();
	// pull the knife: many quick Use presses
	for ( t = 0; t < 120 && !level.mw2coop_qa_knife_out && !flag( "shepherd_killed" ); t += 0.1 )
	{
		striker.mw2coop_qa_hold[ "use" ] = !striker.mw2coop_qa_hold[ "use" ];
		wait 0.1;
	}
	striker.mw2coop_qa_hold[ "use" ] = false;
	report( "throw (" + t + " s)" );
	// aim at Shepherd and throw
	other.mw2coop_saw_kill = false;
	for ( t = 0; t < 60 && !flag( "shepherd_killed" ); t += 0.1 )
	{
		if ( other.mw2coop_awake )
			other.mw2coop_saw_kill = true;
		if ( isdefined( level.shepherd ) )
			striker SetPlayerAngles( VectorToAngles( level.shepherd GetTagOrigin( "tag_eye" ) - striker GetEye() ) );
		if ( flag( "player_aims_knife_at_shepherd" ) )
			striker notify( "throw" );
		wait 0.1;
	}
	check( flag( "shepherd_killed" ), "Shepherd killed by the striker's knife (" + t + " s)" );
	check( other.mw2coop_awake, "the other player is awake as Shepherd dies" );
	wait 3;
	check( other.mw2coop_awake, "and still awake 3 s later" );
	wait 27;	// Price wakes up: the witness stays awake
	check( other.laststand && IsAlive( other ), "the other player watched the whole fight, down" );
	report( "done" );
}

// shep_trace_passed()'s four traces from a player to the stumbler: fraction and surface.
sight_text( player, stumbler )
{
	text = "";
	for ( offset = 0; offset <= 48; offset += 16 )
	{
		trace = BulletTrace( player.origin + ( 0, 0, 32 ), stumbler GetEye() + ( 0, 0, offset ), false, undefined );
		text += " " + int( trace[ "fraction" ] * 100 ) + trace[ "surfacetype" ];
	}
	return text;
}

// self: a player; he keeps his eyes on the running stumbler (screenshots of him).
watch_stumbler()
{
	for ( t = 0; t < 12 && shepherd_hidden() && isdefined( level.shepherd_stumble ); t += 0.05 )
	{
		self SetPlayerAngles( VectorToAngles( level.shepherd_stumble GetEye() - self GetEye() ) );
		wait 0.05;
	}
}

icon_hidden( player )
{
	return !isdefined( player.friendlyIcon ) || player.friendlyIcon.alpha == 0;
}

// Price's eyes, the passenger's body's eyes and the passenger's own eyes in the boat's frame
// (forward, right, up), with the poses.
boat_poses_text( other )
{
	text = "Price " + level.price.a.boat_pose + " eye " + boat_local( level.price GetTagOrigin( "tag_eye" ) ) + bones_text( level.price );
	if ( isdefined( level.mw2coop_boat_body ) )
		text += ", body " + level.mw2coop_boat_body.mw2coop_pose + " eye " + boat_local( level.mw2coop_boat_body GetTagOrigin( "tag_eye" ) ) + bones_text( level.mw2coop_boat_body );
	text += ", passenger eye " + boat_local( other GetEye() );
	// where the guns and the view point, degrees from the bow, to the left positive
	text += "; yaw: Price gun " + gun_yaw( level.price ) + " (his aim " + int( level.price.a.boatAimYaw ) + ")";
	if ( isdefined( level.mw2coop_boat_body ) )
		text += ", body gun " + gun_yaw( level.mw2coop_boat_body );
	return text + ", passenger view " + view_yaw( other );
}

view_yaw( player )
{
	return int( AngleClamp180( player GetPlayerAngles()[ 1 ] - level.players_boat.angles[ 1 ] ) );
}

gun_follows_view( player )
{
	return isdefined( level.mw2coop_boat_body ) && abs( AngleClamp180( gun_yaw( level.mw2coop_boat_body ) - view_yaw( player ) ) ) < 8;
}

gun_yaw( ent )
{
	return int( AngleClamp180( ent GetTagAngles( "tag_flash" )[ 1 ] - level.players_boat.angles[ 1 ] ) );
}

// Bones every human model has: root, pelvis, head.
bones_text( ent )
{
	return " root " + boat_local( ent GetTagOrigin( "tag_origin" ) ) + " pelvis " + boat_local( ent GetTagOrigin( "j_mainroot" ) ) + " head " + boat_local( ent GetTagOrigin( "j_head" ) );
}

boat_local( pos )
{
	boat = level.players_boat;
	d = pos - boat.origin;
	return int( VectorDot( d, AnglesToForward( boat.angles ) ) ) + " " + int( VectorDot( d, AnglesToRight( boat.angles ) ) ) + " " + int( VectorDot( d, AnglesToUp( boat.angles ) ) );
}

// Price's eyes from the boat's long axis, to the right positive.
price_side()
{
	boat = level.players_boat;
	return VectorDot( level.price GetTagOrigin( "tag_eye" ) - boat.origin, AnglesToRight( boat.angles ) );
}

// The passenger's eyes on the other side of the boat's long axis from Price's (his "left" pose
// lies on the right tube: QA boat measured it).
across_from_price( other )
{
	boat = level.players_boat;
	side = VectorDot( other GetEye() - boat.origin, AnglesToRight( boat.angles ) );
	return side * price_side() < 0 && abs( side ) > 8;
}

// The player's distance from the boat's long axis, to the right positive.
boat_side( player )
{
	boat = level.players_boat;
	return VectorDot( player.origin - boat.origin, AnglesToRight( boat.angles ) );
}

stumbler_runs_watch()
{
	level waittill( "run_shep_run" );
	level.mw2coop_qa_stumbler_runs = true;
}

// A floor spot at one of the distances from center where the stumbler is (or is not) in
// shep_trace_passed()'s sight.
sight_spot( stumbler, center, dists, want_sight )
{
	foreach ( dist in strtok( dists, " " ) )
	{
		for ( yaw = 0; yaw < 360; yaw += 20 )
		{
			at = center + AnglesToForward( ( 0, yaw, 0 ) ) * int( dist );
			floor = PlayerPhysicsTrace( at + ( 0, 0, 150 ), at - ( 0, 0, 300 ) );
			if ( abs( floor[ 2 ] - center[ 2 ] ) > 150 )
				continue;
			if ( sees_stumbler( floor, stumbler ) == want_sight )
				return floor;
		}
	}
	return undefined;
}

sees_stumbler( origin, stumbler )
{
	for ( offset = 0; offset <= 48; offset += 16 )
	{
		trace = BulletTrace( origin + ( 0, 0, 32 ), stumbler GetEye() + ( 0, 0, offset ), false, undefined );
		if ( trace[ "fraction" ] >= 1 || trace[ "surfacetype" ] == "none" )
			return true;
	}
	return false;
}

shepherd_hidden()
{
	return isdefined( level.mw2coop_shepherd_hidden ) && level.mw2coop_shepherd_hidden;
}

// One strike attempt: in front of Shepherd, looking at his eyes, melee (0.25 s).
knife_strike_try( striker )
{
	fwd = AnglesToForward( ( 0, level.shepherd.angles[ 1 ], 0 ) );
	at = level.shepherd.origin + fwd * 70;
	if ( Distance( striker.origin, at ) > 30 )
		striker SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 80 ) ) );
	striker SetPlayerAngles( VectorToAngles( level.shepherd GetTagOrigin( "tag_eye" ) - striker GetEye() ) );
	striker.mw2coop_qa_melee = true;
	wait 0.1;
	striker.mw2coop_qa_melee = undefined;
	wait 0.15;
}

knife_out_watch()
{
	level waittill( "knife_pulled_out" );
	level.mw2coop_qa_knife_out = true;
}

// Every 10 s: how far the other player's view is from the action and whether he sees it.
knife_watch_report( other )
{
	level endon( "mw2coop_knife_witness_up" );
	for ( ;; )
	{
		wait 10;
		if ( !isdefined( level.shepherd ) )
			continue;
		eye = other GetEye();
		focus = level.shepherd GetTagOrigin( "tag_eye" );
		sees = BulletTracePassed( eye, focus, false, undefined );
		dot = VectorDot( AnglesToForward( other GetPlayerAngles() ), VectorNormalize( focus - eye ) );
		report( "witness: awake " + other.mw2coop_awake + ", " + int( Distance( eye, focus ) ) + " units from Shepherd, line " + sees + ", facing " + int( dot * 100 ) + "%" );
	}
}

// QA body: put player 2 in front of player 1, facing him (the bodies come from _mw2coop);
// screenshots show how each sees the other.
body()
{
	wait 3;
	angles = level.player GetPlayerAngles();
	yaw = angles[ 1 ];
	forward = AnglesToForward( ( 0, yaw, 0 ) );
	level.player2 SetOrigin( level.player.origin + forward * 120 );
	level.player SetPlayerAngles( ( 0, yaw, 0 ) );
	level.player2 SetPlayerAngles( ( 0, yaw + 180, 0 ) );
	report( "body: models set, player 2 in front of player 1" );
	report( "body: weapons " + level.player GetCurrentWeapon() + " / " + level.player2 GetCurrentWeapon() );
	wait 15;
	level.player2 SetStance( "crouch" );
	report( "body: player 2 crouches" );
	wait 25;
	report( "done" );
}


// Endgame boat chase (af_chase, default start). Names no chase script (not in other zones): the
// bot boards through the jetty trigger and drives with level.mw2coop_qa_boat_autopilot.
boat()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	driver = level.player;
	if ( GetDvar( "mw2coop_qa" ) == "boat2" )
		driver = level.player2;
	other = maps\_mw2coop::other_player( driver );
	trigger = GetEnt( "boat_mount", "targetname" );
	check( isdefined( trigger ), "the jetty's boat trigger" );
	wait 2;
	driver SetOrigin( PlayerPhysicsTrace( trigger.origin + ( 0, 0, 40 ), trigger.origin - ( 0, 0, 200 ) ) );
	for ( t = 0; t < 3 && !flag( "player_on_boat" ); t += 0.25 )
		wait 0.25;
	if ( !flag( "player_on_boat" ) )
	{
		report( "QA sets off the boat trigger (standing in it did not)" );
		trigger notify( "trigger", driver );
	}
	for ( t = 0; t < 10 && !flag( "player_on_boat" ); t += 0.25 )
		wait 0.25;
	check( flag( "player_on_boat" ), "on the boat (" + t + " s)" );
	wait 1;
	check( level.mw2coop_lead == driver, "the boarding player drives" );
	check( isdefined( driver.drivingVehicle ) && driver.drivingVehicle == level.players_boat, "and is at the wheel" );
	d = Distance( other.origin, level.players_boat.origin );
	// he is seated once Price has settled in the boat (up to 5 s)
	for ( t = 0; t < 7 && !isdefined( level.mw2coop_boat_seat ); t += 0.25 )
		wait 0.25;
	d = Distance( other.origin, level.players_boat.origin );
	check( other IsLinked() && d < 150, "the other player rides in the boat (" + int( d ) + " units from it, seated after " + t + " s)" );
	if ( isdefined( level.mw2coop_qa_boat_autopilot ) )
		level.players_boat thread [[ level.mw2coop_qa_boat_autopilot ]]( 60 );
	// the body's eyes (forward right up in the boat's frame) every 0.5 s
	text = "body samples:";
	for ( t = 0; t < 3; t += 0.5 )
	{
		if ( isdefined( level.mw2coop_boat_body ) )
			text += " " + boat_local( level.mw2coop_boat_body GetTagOrigin( "tag_eye" ) );
		else
			text += " -";
		wait 0.5;
	}
	report( text );
	report( "poses: " + boat_poses_text( other ) );
	check( gun_follows_view( other ), "his body's gun points where he looks (gun " + gun_yaw( level.mw2coop_boat_body ) + ", view " + view_yaw( other ) + ")" );
	check( abs( view_yaw( other ) ) < 10, "he starts looking ahead over the bow (" + view_yaw( other ) + ")" );
	check( across_from_price( other ), "the passenger lies across from Price (" + int( boat_side( other ) ) + ", Price " + int( price_side() ) + ")" );
	// he looks as far right, then left, as he can (screenshots: his body's gun follows)
	foreach ( turn in strtok( "-60 60", " " ) )
	{
		report( "passenger looks " + turn );
		for ( t = 0; t < 3; t += 0.05 )
		{
			other SetPlayerAngles( ( 0, level.players_boat.angles[ 1 ] - 36 + int( turn ), 0 ) );
			wait 0.05;
		}
	}
	check( gun_follows_view( other ), "after looking about, the gun still follows (gun " + gun_yaw( level.mw2coop_boat_body ) + ", view " + view_yaw( other ) + ")" );
	// the level's triggers that would turn Price over (af_chase_code::price_position_switch())
	// leave him on his tube in co-op: set them all off
	switches = GetEntArray( "price_position_switch", "targetname" );
	foreach ( s in switches )
		s notify( "trigger", level.mw2coop_lead );
	wait 3;
	report( "poses: " + boat_poses_text( other ) );
	check( switches.size > 0 && level.price.a.boat_pose == "left" && !isdefined( level.price.scripted_boat_pose ), "Price keeps his tube through the level's " + switches.size + " position triggers" );
	check( across_from_price( other ), "the passenger still lies across from him (" + int( boat_side( other ) ) + ", Price " + int( price_side() ) + ")" );
	price_turns = 0;
	// 30 s: the QA autopilot is slower than a player, and by 35 to 45 s Shepherd's boat gets
	// away and the level fails the mission as it would for a player that slow
	for ( t = 0; t < 30; t += 10 )
	{
		// Price's root and eyes against the tag, and the body's eyes: what moves while riding
		for ( s = 0; s < 10; s += 2 )
		{
			wait 2;
			if ( level.price.a.boat_pose != "left" )
				price_turns++;
			if ( isdefined( level.mw2coop_boat_body ) )
				report( "drift: Price " + level.price.a.boat_pose + " root " + boat_local( level.price.origin ) + " eye " + boat_local( level.price GetTagOrigin( "tag_eye" ) ) + ", body " + level.mw2coop_boat_body.mw2coop_pose + " eye " + boat_local( level.mw2coop_boat_body GetTagOrigin( "tag_eye" ) ) );
		}
		report( "ride " + ( t + 10 ) + " s: boat speed " + int( level.players_boat Vehicle_GetSpeed() ) + ", passenger " + int( Distance( other.origin, level.players_boat.origin ) ) + " units from it, linked " + other IsLinked() + ", health " + other.health + ", enemy boat " + int( Distance( level.players_boat.origin, level.enemy_boat.origin ) ) );
	}
	check( IsAlive( other ) && other IsLinked(), "the passenger rode along" );
	check( price_turns == 0, "Price stayed on his tube the whole ride (" + price_turns + " samples off it)" );
	report( "done" );
}

// The level's loadout (maps\_loadout): player 2 has player 1's weapons and holds the same one.
loadout()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 5;
	kit_checks();
	report( "done" );
}

// What the level gave player 1, player 2 has too: weapons (maps\_mw2coop::sync_weapons), the one
// in his hands, the second grenade class (src/mirror.c), the enemies' threat bias group (mirror)
// and threat bias and accuracy against him (maps\_mw2coop::players_threatbias,
// players_attackeraccuracy).
kit_checks()
{
	p1 = level.player;
	p2 = level.player2;
	a = p1 GetCurrentWeapon();
	b = p2 GetCurrentWeapon();
	report( "player 1: " + a + " | " + weapons_text( p1 ) );
	report( "player 2: " + b + " | " + weapons_text( p2 ) );
	check( a == b, "both hold " + a + " (player 2: " + b + ")" );
	missing = "";
	foreach ( weapon in p1 GetWeaponsListAll() )
		if ( !p2 HasWeapon( weapon ) )
			missing += weapon + " ";
	check( missing == "", "player 2 has all of player 1's weapons (missing: " + missing + ")" );
	c1 = p1 GetOffhandSecondaryClass();
	c2 = p2 GetOffhandSecondaryClass();
	check( c1 == c2, "same second grenade class " + c1 + " (player 2: " + c2 + ")" );
	g1 = bias_group( p1 );
	g2 = bias_group( p2 );
	check( g1 == g2, "same threat bias group '" + g1 + "' (player 2: '" + g2 + "')" );
	report( "threatbias " + p1.threatbias + "/" + p2.threatbias + ", attackeraccuracy " + p1.attackeraccuracy + "/" + p2.attackeraccuracy );
}

// --------------------------------------------------------------------------------------------
// S.S.D.D. (trainer): each player takes his own weapons (maps\_mw2coop_trainer). Start point ""
// tests the rifle, "frag" the grenade box, "pit" the pistol. A bot cannot press Use here (console
// +activate, +forward and +gostand reached neither copy's player, 2026-10-21), so a table pickup
// is done as the engine does it (the weapon goes to him, the item goes away) and the box through
// its own trigger; the owner checked the real pickup by hand.
pickups()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	start = GetDvar( "start" );
	if ( start == "frag" )
	{
		level.mw2coop_no_follow = true;
		pickup_frags();
		return;
	}
	if ( start == "pit" )
	{
		level.mw2coop_no_follow = true;
		pickup_table( "pickup_sidearm", "deserteagle", "pistol" );
		report( "done" );
		return;
	}
	flag_wait( "firing_range_initialized" );
	wait 1;
	check( !level.player HasWeapon( "m4_grunt" ) && !level.player2 HasWeapon( "m4_grunt" ), "no rifles at the range yet" );
	// both inside the sandbag bay, player 1 on the left (maps\_mw2coop_trainer::range_places)
	start_ent = GetEnt( "player_start_range", "targetname" );
	right = AnglesToRight( ( 0, start_ent.angles[ 1 ], 0 ) );
	side1 = int( VectorDot( level.player.origin - start_ent.origin, right ) );
	side2 = int( VectorDot( level.player2.origin - start_ent.origin, right ) );
	check( side1 < side2 - 30, "player 1 left of player 2 at the range (" + side1 + ", " + side2 + " from the start, to the right)" );
	check( Distance2D( level.player2.origin, start_ent.origin ) < 8 && Distance2D( level.player.origin, start_ent.origin - right * 48 ) < 8, "both on their places in the sandbag bay" );
	check( abs( level.player.origin[ 2 ] - level.player2.origin[ 2 ] ) < 4, "both on the floor (" + int( level.player.origin[ 2 ] ) + ", " + int( level.player2.origin[ 2 ] ) + ")" );
	// player 2 was brought along to the range: he must stand where he can walk away
	report( "at the range: player 1 " + vec_int( level.player.origin ) + ", player 2 " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	level.mw2coop_no_follow = true;
	flag_wait( "foley_anim_pickup_weapon" );
	wait 2;
	pickup_table( "pickup_rifle", "m4_grunt", "rifle" );
	for ( t = 0; t < 90 && !flag( "player_picked_up_rifle" ); t += 0.5 )
		wait 0.5;
	check( flag( "player_picked_up_rifle" ), "the drill goes on once both have rifles (" + t + " s)" );
	report( "done" );
}

// Player 2 takes the weapon first, then player 1 the one that lies there again.
pickup_table( name, weapon, what )
{
	for ( t = 0; t < 60 && !pickup_ready( name ); t += 0.5 )
		wait 0.5;
	check( pickup_ready( name ), "a " + what + " on the table" );
	if ( !pickup_ready( name ) )
		return;
	first = level.mw2coop_pickups[ name ];
	qa_take( level.player2, first, weapon );
	check( level.player2 HasWeapon( weapon ) && !level.player HasWeapon( weapon ), "player 2 takes the " + what + ", player 1 has none (" + weapons_text( level.player ) + ")" );
	wait 1;
	check( pickup_ready( name ) && level.mw2coop_pickups[ name ].model != "" && level.mw2coop_pickups[ name ].model != "_obj", "another " + what + " lies on the table (" + pickup_model( name ) + ")" );
	if ( pickup_ready( name ) )
		qa_take( level.player, level.mw2coop_pickups[ name ], weapon );
	check( level.player HasWeapon( weapon ) && level.player2 HasWeapon( weapon ), "player 1 takes his own " + what );
	wait 1;
	check( !pickup_ready( name ), "no third " + what + " on the table" );
}

pickup_model( name )
{
	if ( !pickup_ready( name ) )
		return "none";
	return level.mw2coop_pickups[ name ].model;
}

pickup_ready( name )
{
	return isdefined( level.mw2coop_pickups ) && isdefined( level.mw2coop_pickups[ name ] );
}

// The box's own trigger (trainer.gsc spawn_frags()), set off by each player looking at the box.
pickup_frags()
{
	flag_wait( "frags_have_been_spawned" );
	box = GetEnt( "grenade_box", "targetname" );
	trigger = GetEnt( "frag_trigger", "script_noteworthy" );
	foreach ( n, player in level.players )
	{
		qa_stand_by( player, box.origin );
		for ( i = 0; i < 10 && player GetWeaponAmmoStock( "fraggrenade" ) < 3; i++ )
		{
			player SetPlayerAngles( VectorToAngles( box.origin - player GetEye() ) );
			trigger notify( "trigger", player );
			wait 0.5;
		}
		check( player HasWeapon( "fraggrenade" ) && player GetWeaponAmmoStock( "fraggrenade" ) >= 3, "player " + ( n + 1 ) + " takes frags from the box (" + player GetWeaponAmmoStock( "fraggrenade" ) + ")" );
	}
	report( "done" );
}

qa_stand_by( player, at )
{
	away = ( player.origin[ 0 ] - at[ 0 ], player.origin[ 1 ] - at[ 1 ], 0 );
	if ( Length( away ) < 1 )
		away = ( 1, 0, 0 );
	player SetOrigin( ( at[ 0 ], at[ 1 ], player.origin[ 2 ] ) + VectorNormalize( away ) * 40 );
	wait 0.1;
}

// A table pickup as the engine does it: he stands by the weapon, it goes to him (with the
// item's full ammo), the item goes; then he walks back to where he stood.
qa_take( player, item, weapon )
{
	back = player.origin;
	qa_stand_by( player, item.origin );
	player GiveWeapon( weapon );
	player GiveMaxAmmo( weapon );
	player SwitchToWeapon( weapon );
	item Delete();
	wait 0.5;
	player SetOrigin( back );
	wait 0.5;
}

vec_int( v )
{
	return int( v[ 0 ] ) + " " + int( v[ 1 ] ) + " " + int( v[ 2 ] );
}

// --------------------------------------------------------------------------------------------
// Who the enemies fight (any map, a start point with a fight): over 45 s, every 0.5 s, whose
// player each axis AI has as its enemy, and the hits each player takes from axis AI.
targets()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	foreach ( player in level.players )
	{
		player.mw2coop_qa_hits = 0;
		player thread count_hits();
	}
	on1 = 0;
	on2 = 0;
	other = 0;
	samples = 0;
	most = 0;
	for ( t = 0; t < 45; t += 0.5 )
	{
		ai = GetAIArray( "axis" );
		most = max( most, ai.size );
		foreach ( guy in ai )
		{
			if ( !isdefined( guy.enemy ) )
				continue;
			samples++;
			if ( guy.enemy == level.player )
				on1++;
			else if ( guy.enemy == level.player2 )
				on2++;
			else
				other++;
		}
		wait 0.5;
	}
	report( "enemy samples " + samples + " (up to " + most + " axis AI): on player 1 " + on1 + ", on player 2 " + on2 + ", on allies " + other );
	report( "hits from axis AI: player 1 " + level.player.mw2coop_qa_hits + ", player 2 " + level.player2.mw2coop_qa_hits + "; players " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	check( on2 > 0, "enemies take player 2 as their enemy too" );
	check( level.player2.mw2coop_qa_hits > 0 || on2 == 0, "and hit him" );
	report( "done" );
}

count_hits()
{
	self endon( "death" );
	for ( ;; )
	{
		self waittill( "damage", amount, attacker );
		if ( isdefined( attacker ) && IsAI( attacker ) && attacker.team == "axis" )
			self.mw2coop_qa_hits++;
	}
}

// --------------------------------------------------------------------------------------------
// Bodies check (any map): the players stand 120 units apart facing each other for 20 s, for
// screenshots of each one's body on the other's screen.
look()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait 3;
	fwd = AnglesToForward( ( 0, level.player GetPlayerAngles()[ 1 ], 0 ) );
	spot = PlayerPhysicsTrace( level.player.origin + fwd * 120 + ( 0, 0, 48 ), level.player.origin + fwd * 120 - ( 0, 0, 96 ) );
	level.player2 SetOrigin( spot );
	for ( t = 0; t < 20; t += 0.1 )
	{
		level.player SetPlayerAngles( VectorToAngles( level.player2 GetEye() - ( 0, 0, 20 ) - level.player GetEye() ) );
		level.player2 SetPlayerAngles( VectorToAngles( level.player GetEye() - ( 0, 0, 20 ) - level.player2 GetEye() ) );
		if ( t == 2 )
			report( "facing each other: player 2 model " + level.player2.model + ", player 1 model " + level.player.model );
		wait 0.1;
	}
	report( "done" );
}

// No Russian's cover (airport, start "stairs"): player 2's shots at Makarov's squad blow it (the
// level only checks IsPlayer( attacker )); Makarov then shoots both and the mission fails for
// both (maps\_mw2coop_airport::makarov_kills_players). Names no airport script.
cover()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	for ( t = 0; t < 30 && !isdefined( level.makarov ); t += 0.5 )
		wait 0.5;
	check( isdefined( level.makarov ), "Makarov there" );
	wait 3;
	for ( i = 0; i < 6 && !flag( "friendly_fire_warning" ); i++ )
	{
		level.makarov DoDamage( 1, level.makarov.origin, level.player2 );
		wait 0.5;
	}
	check( flag( "friendly_fire_warning" ), "player 2's shots at Makarov's squad blew the cover (" + i + " hits)" );
	SetDvar( "mw2coop_qa_phase", "done" );	// the failure restarts the level
	for ( t = 0; t < 40 && !level.mw2coop.failing; t += 0.5 )
		wait 0.5;
	check( level.mw2coop.failing, "Makarov's squad turned on them and the mission failed for both (" + t + " s)" );
	report( "done" );
}
