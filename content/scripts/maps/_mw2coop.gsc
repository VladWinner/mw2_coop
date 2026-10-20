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
	// The player a one-player scene plays on: scenes the mod hands to either player (the knife
	// fight) read level.mw2coop_lead where they read level.player. Single player too.
	level.mw2coop_lead = level.player;

	if ( !is_campaign_coop() )
		return;

	level.mw2coop = SpawnStruct();
	level.mw2coop.failing = false;
	level.mw2coop.completing = false;
	level.mw2coop.checkpoint = -1;
	if ( !isdefined( level.mw2coop_weapon_sync ) )
		level.mw2coop_weapon_sync = "leader";	// "leader": player 1's changes go to player 2; "both": gains go both ways

	PrecacheString( &"SCRIPT_CHECKPOINT" );
	PrecacheString( &"MW2COOP_STUNNED" );	// the mod's own, from its DLL (src/localize.c)

	// maps\_coop (revive) and kill_wrapper() read this Spec Ops flag in any co-op session;
	// maps\_specialops creates it, the campaign does not. Set when the level ends, as in Spec Ops,
	// so the revive HUD and downed-player threads stop.
	if ( !flag_exist( "special_op_terminated" ) )
		flag_init( "special_op_terminated" );

	state( "level " + level.script + ", start dvar '" + GetDvar( "start" ) + "', qa '" + GetDvar( "mw2coop_qa" ) + "'" );

	thread no_saves();
	thread regroup_after_start();
	thread follow_leader_teleports();
	thread sync_weapons();
	thread track_checkpoints();
	thread watch_failure();
	if ( god_mode_on() )
		thread god_mode();

	if ( GetDvar( "mw2coop_qa" ) != "" )
	{
		thread maps\_mw2coop_qa::link_probe();
		thread maps\_mw2coop_qa::main();
	}
}

// --------------------------------------------------------------------------------------------
// God mode for testing: on unless the host sets "mw2coop_god 0" before coopLaunch (the DLL
// sends the value to the client, so both lockstep machines agree). Players take no damage,
// so they are never downed; scripted deaths and mission failures still happen.

god_mode_on()
{
	return GetDvar( "mw2coop_god" ) != "0";
}

god_mode()
{
	state( "god mode on (mw2coop_god 0 before coopLaunch turns it off)" );
	for ( ;; )
	{
		foreach ( player in level.players )
		{
			if ( !IsAlive( player ) )
				continue;
			player EnableInvulnerability();	// level scripts turn it off at times; put it back every frame
			if ( player.health < player.maxhealth )
				player.health = player.maxhealth;
		}
		wait 0.05;
	}
}

// --------------------------------------------------------------------------------------------
// Messages to the outside: the DLL logs mw2coop_state on both machines and acts on
// mw2coop_cmd on the host only. GetTime() makes every request a new value.

state( text )
{
	SetDvar( "mw2coop_state", text );
}

// The DLL reads mw2coop_cmd every 250 ms, so requests are queued and sent 0.3 s apart.
request( what )
{
	if ( !isdefined( level.mw2coop_requests ) )
	{
		level.mw2coop_requests = [];
		level thread send_requests();
	}
	level.mw2coop_requests[ level.mw2coop_requests.size ] = what;
	level notify( "mw2coop_request" );
}

send_requests()
{
	for ( ;; )
	{
		while ( level.mw2coop_requests.size == 0 )
			level waittill( "mw2coop_request" );
		what = level.mw2coop_requests[ 0 ];
		level.mw2coop_requests = array_remove_index( level.mw2coop_requests, 0 );
		SetDvar( "mw2coop_cmd", what + " @" + GetTime() );
		wait 0.3;
	}
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

// Start points move player 1 at the end of the first frame. If player 1 is not standing on
// ground yet (a scene, a climb, a vehicle), wait until he is, or until a level script seats
// player 2 somewhere itself.
regroup_after_start()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.1;
	for ( waited = 0; waited < 120; waited += 0.25 )
	{
		if ( level.player2 IsLinked() )
			return;
		if ( level.player2 teleport_to_partner( level.player ) )
		{
			state( "regrouped at start point " + level.start_point );
			return;
		}
		wait 0.25;
	}
	state( "no safe spot next to player 1 within 120 s" );
}

// When a script moves player 1 far in one step (teleport, start point), bring player 2 along.
follow_leader_teleports()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.15;
	last = level.player.origin;
	far_time = 0;
	follow = false;
	for ( ;; )
	{
		wait 0.05;
		now = level.player.origin;
		// a seated or otherwise linked player 2 rides along with whatever holds him; a level
		// whose players go separate ways for a while (the Cliffhanger climb lanes) sets
		// level.mw2coop_no_follow
		if ( DistanceSquared( now, last ) > 512 * 512 && !level.player2 IsLinked() && !isdefined( level.mw2coop_no_follow ) )
			follow = true;
		last = now;
		// player 1 may land in a scene or in the air: follow once he stands somewhere
		if ( follow && !level.player2 IsLinked() && level.player2 teleport_to_partner( level.player ) )
		{
			follow = false;
			state( "followed a teleport of player 1" );
		}

		// Leash: a slow scripted move of player 1 (a blended link, a scene) or simply drifting
		// apart leaves player 2 far behind; after a while bring him over. Only while both are
		// free and up; a level can turn it off with level.mw2coop_no_leash.
		if ( leash_applies() && DistanceSquared( now, level.player2.origin ) > leash_distance() * leash_distance() )
			far_time += 0.05;
		else
			far_time = 0;
		if ( far_time >= leash_time() && level.player2 teleport_to_partner( level.player ) )
		{
			far_time = 0;
			state( "leash: player 2 brought to player 1" );
		}
	}
}

leash_distance()
{
	return 3000;
}

leash_time()
{
	return 8;	// seconds
}

leash_applies()
{
	if ( isdefined( level.mw2coop_no_leash ) && level.mw2coop_no_leash )
		return false;
	if ( isdefined( level.mw2coop_no_follow ) )
		return false;
	if ( level.player IsLinked() || level.player2 IsLinked() )
		return false;
	return !is_downed( level.player ) && !is_downed( level.player2 );
}

// self: the player to move. Tries free spots beside and behind the partner, each with a clear
// line from him and floor at his height. Moves nobody and returns false while the partner is
// not standing on ground (linked, falling, climbing): his origin may be over a drop, and a
// player put there falls (Cliffhanger). Returns true once moved.
teleport_to_partner( partner )
{
	if ( !IsAlive( partner ) || !is_standing( partner ) )
		return false;
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
	chest = partner.origin + ( 0, 0, 48 );
	foreach ( offset in offsets )
	{
		spot = partner.origin + offset;
		// a clear line from the partner (ignoring the partner's own body) ...
		if ( !BulletTracePassed( chest, spot + ( 0, 0, 48 ), false, partner ) )
			continue;
		// ... and floor under the spot, at about the partner's height
		ground = PlayerPhysicsTrace( spot + ( 0, 0, 48 ), spot - ( 0, 0, 96 ) );
		if ( abs( ground[ 2 ] - partner.origin[ 2 ] ) > 40 )
			continue;
		self SetOrigin( ground );
		self SetPlayerAngles( yaw );
		return true;
	}
	// nowhere free: share the spot he stands on, the players push apart
	self SetOrigin( partner.origin );
	self SetPlayerAngles( yaw );
	return true;
}

// Free and on ground. IsOnGround() reads 0 for player 2 on both machines in co-op (seen in the
// spawn audit), so ground under the feet is traced too.
is_standing( player )
{
	if ( player IsLinked() )
		return false;
	if ( player IsOnGround() )
		return true;
	below = PlayerPhysicsTrace( player.origin + ( 0, 0, 8 ), player.origin - ( 0, 0, 24 ) );
	return player.origin[ 2 ] - below[ 2 ] < 4;
}

// self: the player to move. teleport_to_partner(), retried for up to timeout seconds (the
// partner may still be getting out of a vehicle or a scene).
teleport_to_partner_when_safe( partner, timeout )
{
	for ( waited = 0; waited < timeout; waited += 0.1 )
	{
		if ( self teleport_to_partner( partner ) )
			return true;
		wait 0.1;
	}
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

// Level loadouts (maps\_loadout) set first-person hands on level.player only. Without hands the
// client's own player cannot draw a weapon: the engine raises "no viewmodel set" and drops the
// co-op session. Player 2 always wears player 1's hands, also when a level changes them.
sync_viewmodel()
{
	hands = level.player GetViewModel();
	if ( hands != "" && level.player2 GetViewModel() != hands )
		level.player2 SetViewModel( hands );
}

sync_weapons()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	sync_viewmodel();	// before any weapon reaches player 2
	wait 0.15;	// after start points hand out weapons
	sync_viewmodel();
	copy_weapons( level.player2, level.player );
	state( "player 2 weapons copied from player 1" );
	prev1 = level.player GetWeaponsListAll();
	prev2 = level.player2 GetWeaponsListAll();
	for ( ;; )
	{
		wait 0.25;
		sync_viewmodel();
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
	request( "checkpoint " + level.script + " " + cp.start );	// the host saves it for mw2coop_continue
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
	// own thread: mission_failed() sends mw2coop_level_end, which would end this thread
	level thread mission_failed();
}

// Replaces MissionFailed() in a co-op campaign session.
mission_failed()
{
	if ( level.mw2coop.failing || level.mw2coop.completing )
		return;
	level.mw2coop.failing = true;
	level notify( "mw2coop_level_end" );
	flag_set( "special_op_terminated" );
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
// ChangeLevel() does nothing in a co-op session: its exit handler (rva 0x170e60) returns early
// when gameMode is "so". The host's DLL starts the next mission the way the lobby does
// (coop_mapName + coopLaunch, which also sends the client there). The game's records
// (highestMission, difficulty) are local-player profile data and are not written.
// Not handled yet: _nextmission()'s cliffhanger -> airport special case.

next_level_name()
{
	settings = level.missionSettings;
	index = settings maps\_endmission::getLevelIndex( level.script );
	// This build's _nextmission() completes airport right after cliffhanger ("always skip
	// airport for RUSSIAN PC BUILD", with no build check). The owner wants No Russian played:
	// co-op always goes on with airport.
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
	flag_set( "special_op_terminated" );
	level notify( "nextmission" );
	level.nextmission = true;
	foreach ( player in level.players )
		player EnableInvulnerability();
	next = next_level_name();
	SetDvar( "start", "" );
	state( "mission complete " + level.script + ", next '" + next + "'" );
	request( "nextmap " + next );	// the host saves it as progress and launches it
}

// --------------------------------------------------------------------------------------------
// Vehicles: level scripts seat player 1 only. These seat the partner on a free seat of the
// same vehicle (an AI seat the vehicle has not filled), linked the way the level links
// player 1, and take him out next to player 1 again.

// self: vehicle. A sittag no rider uses, rear seats first; skip_tag: player 1's seat.
free_seat_tag( skip_tag )
{
	if ( !isdefined( self.vehicletype ) || !isdefined( level.vehicle_aianims ) || !isdefined( level.vehicle_aianims[ self.vehicletype ] ) )
		return undefined;
	seats = level.vehicle_aianims[ self.vehicletype ];
	for ( pos = seats.size - 1; pos > 0; pos-- )	// never the driver (0)
	{
		if ( !isdefined( seats[ pos ].sittag ) || ( isdefined( skip_tag ) && seats[ pos ].sittag == skip_tag ) )
			continue;
		if ( isdefined( self.usedPositions ) && isdefined( self.usedPositions[ pos ] ) && self.usedPositions[ pos ] )
			continue;
		if ( IsSubStr( seats[ pos ].sittag, "turret" ) )
			continue;
		self.usedPositions[ pos ] = true;	// keep AI riders off it
		self.mw2coop_seat_pos = pos;
		return seats[ pos ].sittag;
	}
	return undefined;
}

// The player on a vehicle turret shared by two players (a level sets level.mw2coop_gunner).
vehicle_gunner()
{
	if ( isdefined( level.mw2coop_gunner ) && IsAlive( level.mw2coop_gunner ) )
		return level.mw2coop_gunner;
	return level.player;
}

other_player( player )
{
	if ( player == level.player )
		return level.player2;
	return level.player;
}

// self: vehicle. Returns the seat entity (pass it to partner_leaves_seat() or release_seat()).
//   skip_tag: player 1's seat (default tag_passenger; "" skips none); weapons: usable on the seat.
seat_partner_in_vehicle( player, right, left, up, down, skip_tag, weapons )
{
	if ( !isdefined( player ) || !IsAlive( player ) )
		return undefined;
	if ( !isdefined( right ) )
		right = 90;
	if ( !isdefined( left ) )
		left = 90;
	if ( !isdefined( up ) )
		up = 40;
	if ( !isdefined( down ) )
		down = 40;
	if ( !isdefined( skip_tag ) )
		skip_tag = "tag_passenger";

	tag = self free_seat_tag( skip_tag );
	// a level can name a squadmate who gives up his seat (level.mw2coop_spare_rider)
	evicted = "";
	if ( !isdefined( tag ) && isdefined( level.mw2coop_spare_rider ) )
	{
		evicted = " spare " + level.mw2coop_spare_rider + ": " + self evict_rider( level.mw2coop_spare_rider );
		tag = self free_seat_tag( skip_tag );
	}
	seat = spawn_tag_origin();
	seat.mw2coop_vehicle = self;
	seat.mw2coop_arcs = [];
	seat.mw2coop_arcs[ 0 ] = right;
	seat.mw2coop_arcs[ 1 ] = left;
	seat.mw2coop_arcs[ 2 ] = up;
	seat.mw2coop_arcs[ 3 ] = down;
	seat.mw2coop_weapons = isdefined( weapons ) && weapons;
	if ( isdefined( tag ) )
	{
		seat.mw2coop_pos = self.mw2coop_seat_pos;
		// same offset as maps\_utility::player_rides_shotgun_in_humvee() uses on tag_passenger
		seat LinkTo( self, tag, ( -5, 10, -34 ), ( 0, 0, 0 ) );
		state( "partner seated on " + tag + evicted );
	}
	else
	{
		seat LinkTo( self, "tag_body", ( -50, 0, 10 ), ( 0, 0, 0 ) );
		state( "partner: no free seat (" + self seats_text() + ")" + evicted + ", riding in the back" );
	}
	seat link_to_seat( player );
	return seat;
}

// self: seat. Puts player on it (also used when two players swap places).
link_to_seat( player )
{
	self.mw2coop_player = player;
	player AllowCrouch( false );
	player AllowProne( false );
	if ( self.mw2coop_weapons )
		player EnableWeapons();
	else
		player DisableWeapons();
	player PlayerLinkTo( self, "tag_origin", 0.8, self.mw2coop_arcs[ 0 ], self.mw2coop_arcs[ 1 ], self.mw2coop_arcs[ 2 ], self.mw2coop_arcs[ 3 ] );
}

// self: seat. The player gets off where he is; the seat is freed.
release_seat()
{
	player = self.mw2coop_player;
	if ( isdefined( self.mw2coop_vehicle ) && isdefined( self.mw2coop_pos ) && isdefined( self.mw2coop_vehicle.usedPositions ) )
		self.mw2coop_vehicle.usedPositions[ self.mw2coop_pos ] = false;
	if ( isdefined( player ) && IsAlive( player ) )
	{
		org = player.origin;
		player Unlink();
		player SetOrigin( org );
		player EnableWeapons();
		player AllowCrouch( true );
		player AllowProne( true );
	}
	self Delete();
}

// self: seat from seat_partner_in_vehicle().
partner_leaves_seat( delay )
{
	if ( isdefined( delay ) )
		wait delay;
	player = self.mw2coop_player;
	if ( isdefined( self.mw2coop_vehicle ) && isdefined( self.mw2coop_pos ) && isdefined( self.mw2coop_vehicle.usedPositions ) )
		self.mw2coop_vehicle.usedPositions[ self.mw2coop_pos ] = false;
	if ( isdefined( player ) && IsAlive( player ) )
	{
		player Unlink();
		player EnableWeapons();
		player AllowCrouch( true );
		player AllowProne( true );
		player thread teleport_to_partner_when_safe( other_player( player ), 5 );
	}
	self Delete();
}

// self: vehicle. "type: pos=tag used ..." for the log.
seats_text()
{
	if ( !isdefined( self.vehicletype ) )
		return "no vehicletype";
	if ( !isdefined( level.vehicle_aianims ) || !isdefined( level.vehicle_aianims[ self.vehicletype ] ) )
		return self.vehicletype + ": no seat anims";
	text = self.vehicletype + ":";
	seats = level.vehicle_aianims[ self.vehicletype ];
	for ( pos = 0; pos < seats.size; pos++ )
	{
		tag = "?";
		if ( isdefined( seats[ pos ].sittag ) )
			tag = seats[ pos ].sittag;
		used = isdefined( self.usedPositions ) && isdefined( self.usedPositions[ pos ] ) && self.usedPositions[ pos ];
		text += " " + pos + "=" + tag;
		if ( used )
			text += "*";
		if ( isdefined( self.riders ) )
			foreach ( guy in self.riders )
				if ( isdefined( guy ) && isdefined( guy.vehicle_position ) && guy.vehicle_position == pos )
					text += "(" + rider_name( guy ) + ")";
	}
	return text;
}

rider_name( guy )
{
	if ( isdefined( guy.animname ) )
		return guy.animname;
	if ( isdefined( guy.script_noteworthy ) )
		return guy.script_noteworthy;
	if ( isdefined( guy.targetname ) )
		return guy.targetname;
	return "ai";
}

// self: vehicle. Removes the rider with this name (a squadmate the level does not need)
// so a player can take his seat. The vehicle frees the seat when the rider is gone
// (maps\_vehicle_aianim::guy_deathhandle); done here too in case that runs later.
evict_rider( animname )
{
	if ( !isdefined( self.riders ) )
		return "no riders";
	names = "";
	foreach ( guy in self.riders )
	{
		if ( isdefined( guy ) )
			names += rider_name( guy ) + " ";
		// animname, or the spawner's script_noteworthy while spawn functions have not run yet
		if ( !isdefined( guy ) || rider_name( guy ) != animname )
			continue;
		pos = guy.vehicle_position;
		self.riders = array_remove( self.riders, guy );
		if ( isdefined( guy.magic_bullet_shield ) )
			guy stop_magic_bullet_shield();
		guy Delete();
		if ( isdefined( pos ) && isdefined( self.usedPositions ) )
			self.usedPositions[ pos ] = false;
		return "removed";
	}
	return "not among " + names;
}

// Campaign players have no body model (Spec Ops gives them coop_body_* models, which only the
// so_* zones hold), so there is no "tag_origin" on them for effects or links. self: player.
// Returns a tag_origin model that follows him, for code that plays effects on a player's tag.
player_tag_ent()
{
	if ( !isdefined( self.mw2coop_tag_ent ) )
	{
		ent = spawn_tag_origin();
		ent.origin = self.origin;
		ent.angles = self.angles;
		ent LinkTo( self );
		self.mw2coop_tag_ent = ent;
	}
	return self.mw2coop_tag_ent;
}
