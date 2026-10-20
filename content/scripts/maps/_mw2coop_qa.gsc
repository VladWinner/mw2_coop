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
//   oilrig   - (oilrig, default start) both ride an SDV underwater, both surface; player 1 does
//              the stealth kill, then both are pulled out of the water. oilrig2: player 2 kills
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
spawn_audit()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
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
