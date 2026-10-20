// mw2coop QA: scripted checks that run inside the game on both PCs (lockstep, so they must be
// deterministic). Chosen before launch with the dvar mw2coop_qa on BOTH machines:
//   intro    - player 2 joins player 1 at the range, same weapons, gets the rifle with player 1
//   restart  - reach a checkpoint, down both players, expect a restart at that checkpoint
//   end      - (launch with "start end") run the ending, expect both machines on the next mission
//   resume   - (qa_pair.sh resume @continue) both machines start at the saved checkpoint
//   god      - with god mode on, heavy damage downs neither player
// Results go to the dvar mw2coop_qa_log, which the DLL writes to mw2coop.log on each PC.
#include common_scripts\utility;
#include maps\_utility;

main()
{
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
