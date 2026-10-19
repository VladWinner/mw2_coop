// mw2coop: campaign co-op framework for Call of Duty: Modern Warfare 2 (2009).
// Our own code, loaded through mw2coop's script loader.
//
// Co-op runs in LOCKSTEP: host and client both run every level script. Everything here must
// give the same result on both machines from the same game state. Never branch on anything
// local (local-only dvars, files, wall-clock time) when that changes game state.
//
// Hooks into the game (see mw2coop/patches):
//   maps\_load::main()                     -> main()
//   maps\_utility::is_specialop()          -> false in a co-op campaign session
//   maps\_utility::missionFailedWrapper()  -> mission_failed()
//   maps\_utility::nextmission()           -> mission_complete()
// Requests to the host's mw2coop DLL go through the dvar "mw2coop_cmd" (see request()).

#include common_scripts\utility;
#include maps\_utility;
#include maps\_hud_util;

// A co-op session on a campaign map: the campaign, played by two.
is_campaign_coop()
{
	if ( GetDvar( "coop" ) != "1" )
		return false;
	return !string_starts_with( ToLower( GetDvar( "mapname" ) ), "so_" );
}

// Level scripts call these before maps\_load::main().
//   flag_name: progress flag of the level; start_name: the level's start point that resumes there.
add_checkpoint( flag_name, start_name )
{
	if ( !isdefined( level.mw2coop_checkpoints ) )
		level.mw2coop_checkpoints = [];
	cp = SpawnStruct();
	cp.flag = flag_name;
	cp.start = start_name;
	level.mw2coop_checkpoints[ level.mw2coop_checkpoints.size ] = cp;
}

// Called from maps\_load::main() right after maps\_coop::main().
main()
{
	if ( !is_campaign_coop() )
		return;

	level.mw2coop = SpawnStruct();
	level.mw2coop.failing = false;
	level.mw2coop.completing = false;
	level.mw2coop.checkpoint = -1;
	if ( !isdefined( level.mw2coop_weapon_sync ) )
		level.mw2coop_weapon_sync = "leader";	// "leader": player 1's changes go to player 2; "both": gains go both ways

	PrecacheString( &"SCRIPT_CHECKPOINT" );

	state( "level " + level.script + ", start dvar '" + GetDvar( "start" ) + "', qa '" + GetDvar( "mw2coop_qa" ) + "'" );

	thread no_saves();
	thread regroup_after_start();
	thread follow_leader_teleports();
	thread sync_weapons();
	thread track_checkpoints();
	thread watch_failure();

	if ( GetDvar( "mw2coop_qa" ) != "" )
		thread maps\_mw2coop_qa::main();
}

// --------------------------------------------------------------------------------------------
// Messages to the outside: the DLL logs mw2coop_state on both machines and acts on
// mw2coop_cmd on the host only. GetTime() makes every request a new value.

state( text )
{
	SetDvar( "mw2coop_state", text );
}

request( what )
{
	SetDvar( "mw2coop_cmd", what + " @" + GetTime() );
}

// --------------------------------------------------------------------------------------------
// Saves: each PC would write its own save, and loading one would break lockstep. Progress is
// kept as checkpoints (level start points) instead.

no_saves()
{
	waittillframeend;	// maps\_load sets sv_saveOnStartMap after we are called
	SetSavedDvar( "sv_saveOnStartMap", false );
	if ( isdefined( level.flag ) && isdefined( level.flag[ "disable_autosaves" ] ) )
		flag_set( "disable_autosaves" );
}

// --------------------------------------------------------------------------------------------
// Keeping player 2 with player 1

// Start points move player 1 at the end of the first frame.
regroup_after_start()
{
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.1;
	level.player2 teleport_to_partner( level.player );
	state( "regrouped at start point " + level.start_point );
}

// When a script moves player 1 far in one step (teleport, start point), bring player 2 along.
follow_leader_teleports()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.15;
	last = level.player.origin;
	for ( ;; )
	{
		wait 0.05;
		now = level.player.origin;
		if ( DistanceSquared( now, last ) > 512 * 512 )
		{
			level.player2 teleport_to_partner( level.player );
			state( "followed a teleport of player 1" );
		}
		last = now;
	}
}

// self: the player to move. Tries free spots beside and behind the partner.
teleport_to_partner( partner )
{
	angles = partner GetPlayerAngles();
	yaw = ( 0, angles[ 1 ], 0 );
	forward = AnglesToForward( yaw );
	right = AnglesToRight( yaw );
	offsets = [];
	offsets[ offsets.size ] = right * 48;
	offsets[ offsets.size ] = right * -48;
	offsets[ offsets.size ] = forward * -48;
	offsets[ offsets.size ] = right * 64 + forward * -48;
	offsets[ offsets.size ] = right * -64 + forward * -48;
	from = partner.origin + ( 0, 0, 32 );
	foreach ( offset in offsets )
	{
		spot = partner.origin + offset + ( 0, 0, 32 );
		if ( DistanceSquared( PlayerPhysicsTrace( from, spot ), spot ) > 1 )
			continue;
		ground = PlayerPhysicsTrace( spot, spot - ( 0, 0, 160 ) );
		self SetOrigin( ground );
		self SetPlayerAngles( yaw );
		return true;
	}
	// nowhere free: share the spot, the players push apart
	self SetOrigin( partner.origin );
	self SetPlayerAngles( yaw );
	return false;
}

// --------------------------------------------------------------------------------------------
// Weapons: level scripts give and take weapons on level.player only.

is_downed( player )
{
	return player ent_flag_exist( "coop_downed" ) && player ent_flag( "coop_downed" );
}

give_weapon( player, weapon )
{
	player GiveWeapon( weapon );
	player GiveMaxAmmo( weapon );
	if ( player GetCurrentWeapon() == "none" && is_in_array( player GetWeaponsListPrimaries(), weapon ) )
		player SwitchToWeapon( weapon );
}

// Player 2 starts with exactly player 1's weapons.
copy_weapons( to, from )
{
	foreach ( weapon in to GetWeaponsListAll() )
		if ( !from HasWeapon( weapon ) )
			to TakeWeapon( weapon );
	foreach ( weapon in from GetWeaponsListAll() )
		if ( !to HasWeapon( weapon ) )
			give_weapon( to, weapon );
	current = from GetCurrentWeapon();
	if ( current != "none" && to HasWeapon( current ) )
		to SwitchToWeapon( current );
}

sync_weapons()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.15;	// after start points hand out weapons
	copy_weapons( level.player2, level.player );
	state( "player 2 weapons copied from player 1" );
	prev1 = level.player GetWeaponsListAll();
	prev2 = level.player2 GetWeaponsListAll();
	for ( ;; )
	{
		wait 0.25;
		now1 = level.player GetWeaponsListAll();
		now2 = level.player2 GetWeaponsListAll();
		// the revive system takes and returns weapons of a downed player: leave it alone
		if ( !is_downed( level.player ) && !is_downed( level.player2 ) )
		{
			foreach ( weapon in now1 )
				if ( !is_in_array( prev1, weapon ) && !level.player2 HasWeapon( weapon ) )
					give_weapon( level.player2, weapon );
			if ( level.mw2coop_weapon_sync == "both" )
				foreach ( weapon in now2 )
					if ( !is_in_array( prev2, weapon ) && !level.player HasWeapon( weapon ) )
						give_weapon( level.player, weapon );
			foreach ( weapon in prev1 )
				if ( !is_in_array( now1, weapon ) && level.player2 HasWeapon( weapon ) )
					level.player2 TakeWeapon( weapon );
		}
		prev1 = level.player GetWeaponsListAll();
		prev2 = level.player2 GetWeaponsListAll();
	}
}

// --------------------------------------------------------------------------------------------
// Checkpoints: the furthest progress flag reached sets the "start" dvar, so a restart resumes
// from that start point on both machines (the dvar is set by this script, so it matches).

track_checkpoints()
{
	if ( !isdefined( level.mw2coop_checkpoints ) )
		return;
	wait 0.1;	// level.start_point is chosen at the end of the first frame
	foreach ( index, cp in level.mw2coop_checkpoints )
		if ( isdefined( level.start_point ) && level.start_point == cp.start )
			level.mw2coop.checkpoint = index;
	foreach ( index, cp in level.mw2coop_checkpoints )
		thread checkpoint_wait( index, cp );
}

checkpoint_wait( index, cp )
{
	level endon( "mw2coop_level_end" );
	while ( !isdefined( level.flag[ cp.flag ] ) )
		wait 0.05;
	flag_wait( cp.flag );
	if ( index <= level.mw2coop.checkpoint || level.mw2coop.failing )
		return;
	level.mw2coop.checkpoint = index;
	SetDvar( "start", cp.start );
	state( "checkpoint " + cp.start );
	foreach ( player in level.players )
		player thread checkpoint_hud();
}

checkpoint_hud()
{
	hud = NewClientHudElem( self );
	hud.x = 0;
	hud.y = -60;
	hud.alignX = "center";
	hud.alignY = "middle";
	hud.horzAlign = "center";
	hud.vertAlign = "middle";
	hud.font = "objective";
	hud.fontscale = 1.5;
	hud.foreground = true;
	hud.hidewheninmenu = true;
	hud.alpha = 1;
	hud SetText( &"SCRIPT_CHECKPOINT" );
	wait 2;
	hud FadeOverTime( 1 );
	hud.alpha = 0;
	wait 1;
	hud Destroy();
}

// --------------------------------------------------------------------------------------------
// Failure: both players down (maps\_coop revive system) or a scripted mission failure.

watch_failure()
{
	level endon( "mw2coop_level_end" );
	while ( !isdefined( level.flag[ "coop_alldowned" ] ) )
		wait 0.05;
	flag_wait( "coop_alldowned" );
	mission_failed();
}

// Replaces MissionFailed() in a co-op campaign session.
mission_failed()
{
	if ( level.mw2coop.failing || level.mw2coop.completing )
		return;
	level.mw2coop.failing = true;
	level notify( "mw2coop_level_end" );
	state( "mission failed, restart at '" + GetDvar( "start" ) + "'" );
	foreach ( player in level.players )
	{
		player EnableInvulnerability();
		player FreezeControls( true );
		overlay = create_client_overlay( "black", 0, player );
		overlay FadeOverTime( 2 );
		overlay.alpha = 1;
	}
	wait 3;
	request( "restart" );	// the host runs fast_restart; the engine sends coopRestart to the client
}

// --------------------------------------------------------------------------------------------
// Mission end: replaces maps\_endmission::_nextmission() in a co-op campaign session.

next_level_name()
{
	settings = level.missionSettings;
	index = settings maps\_endmission::getLevelIndex( level.script );
	if ( !isdefined( index ) || index + 1 >= settings.levels.size )
		return "";
	return settings maps\_endmission::getLevelName( index + 1 );
}

mission_complete()
{
	if ( level.mw2coop.completing || level.mw2coop.failing )
		return;
	level.mw2coop.completing = true;
	level notify( "mw2coop_level_end" );
	level notify( "nextmission" );
	level.nextmission = true;
	foreach ( player in level.players )
		player EnableInvulnerability();
	next = next_level_name();
	SetDvar( "start", "" );
	state( "mission complete " + level.script + ", next '" + next + "'" );
	request( "nextmap " + next );
}
