// mw2coop: Team Player (roadkill) for two players. Called from mw2coop/patches/maps/
// roadkill_code.gsc.patch and roadkill.gsc.patch; outside a co-op campaign session every entry
// point does what the original code did.
//
// The player's Humvee: whoever reaches the glowing seat first is the gunner (the game's get-in
// animation and the 50 cal turret); the other boards the same door onto a rear seat; the convoy
// (flag "player_gets_in") leaves once both are in. The passenger holds Use for a second to swap
// places with the gunner. At the end of the ride both get out where they are.
#include common_scripts\utility;
#include maps\_utility;

swap_hold_time()
{
	return 1.0;	// seconds the passenger holds Use to swap
}

board_wait_time()
{
	return 30;	// seconds the convoy waits for the second player before seating him
}

// Seat view arcs for the passenger (right, left, up, down); weapons stay usable.
seat_arcs()
{
	arcs = [];
	arcs[ 0 ] = 120;
	arcs[ 1 ] = 120;
	arcs[ 2 ] = 45;
	arcs[ 3 ] = 30;
	return arcs;
}

// player_ride_vehicle(): the seat was triggered by user (a player) or by player 1 coming close.
boarding_gunner( user )
{
	level.mw2coop_gunner = level.player;
	if ( maps\_mw2coop::is_campaign_coop() && isdefined( user ) && IsPlayer( user ) )
		level.mw2coop_gunner = user;
	maps\_mw2coop::state( "roadkill: gunner is " + player_text( level.mw2coop_gunner ) );
	return level.mw2coop_gunner;
}

// player_ride_vehicle(), where it set "player_gets_in". self: the player's Humvee.
gunner_boarded()
{
	if ( !maps\_mw2coop::is_campaign_coop() )
	{
		flag_set( "player_gets_in" );
		return;
	}
	self thread partner_boards_then_go();
}

partner_boards_then_go()
{
	// the gunner's get-in animation ends on the turret
	while ( !isdefined( level.player_turret ) || !isdefined( level.player_turret GetTurretOwner() ) )
		wait 0.05;
	partner = maps\_mw2coop::other_player( maps\_mw2coop::vehicle_gunner() );
	if ( isdefined( partner ) && IsAlive( partner ) )
		self partner_boards( partner );
	flag_set( "player_gets_in" );
	self thread swap_seats();
}

// self: the player's Humvee. The same glowing seat again, for the second player.
partner_boards( partner )
{
	self HidePart( "tag_seat_rb_hide" );
	chair = Spawn( "script_model", ( 0, 0, 0 ) );
	chair SetModel( "vehicle_hummer_seat_rb_obj" );
	chair LinkTo( self, "tag_seat_rb_attach", ( 0, 0, 0 ), ( 0, 0, 0 ) );
	maps\_mw2coop::state( "roadkill: waiting for " + player_text( partner ) + " to board" );
	waited = 0;
	while ( waited < board_wait_time() )
	{
		if ( Distance( chair.origin, partner.origin ) < 80 && !maps\_mw2coop::is_downed( partner ) )
			break;
		wait 0.05;
		waited += 0.05;
	}
	chair Delete();
	self ShowPart( "tag_seat_rb_hide" );
	self seat_partner( partner );
	if ( waited >= board_wait_time() )
		maps\_mw2coop::state( "roadkill: " + player_text( partner ) + " seated after " + board_wait_time() + " s" );
}

// self: the player's Humvee.
seat_partner( partner )
{
	arcs = seat_arcs();
	level.mw2coop_roadkill_seat = self maps\_mw2coop::seat_partner_in_vehicle( partner, arcs[ 0 ], arcs[ 1 ], arcs[ 2 ], arcs[ 3 ], "", true );
}

// Start points that begin on the turret (player_turret_humvee). self: the player's Humvee.
partner_rides_now()
{
	if ( !maps\_mw2coop::is_campaign_coop() )
		return;
	if ( !isdefined( level.mw2coop_gunner ) )
		level.mw2coop_gunner = level.player;
	wait 0.05;
	partner = maps\_mw2coop::other_player( level.mw2coop_gunner );
	if ( isdefined( partner ) && IsAlive( partner ) )
		self seat_partner( partner );

	// Observed in co-op: the start point links player 1 in its first frame, before the Humvee is
	// moved to the start of its path; he stays behind (using the turret from afar) and the link
	// drops about 2 s later. Mount him again once the Humvee is in place.
	wait 1;
	gunner = maps\_mw2coop::vehicle_gunner();
	if ( isdefined( level.player_turret ) && Distance( gunner.origin, level.player_turret.origin ) > 150 )
	{
		gunner EnableTurretDismount();
		level.player_turret UseBy( gunner );
		wait 0.05;
		if ( gunner IsLinked() )
			gunner Unlink();
		wait 0.05;
		self mount_turret( gunner );
		maps\_mw2coop::state( "roadkill: " + player_text( gunner ) + " mounted the turret again after the start point" );
	}
	self thread swap_seats();
}

// self: the player's Humvee.
swap_seats()
{
	level endon( "mw2coop_ride_ends" );
	level endon( "ride_ends" );
	for ( ;; )
	{
		wait 0.05;
		seat = level.mw2coop_roadkill_seat;
		if ( !isdefined( seat ) || !isdefined( level.player_turret ) )
			return;
		passenger = seat.mw2coop_player;
		if ( !isdefined( passenger ) || !passenger UseButtonPressed() )
			continue;
		held = 0;
		while ( passenger UseButtonPressed() && held < swap_hold_time() )
		{
			wait 0.05;
			held += 0.05;
		}
		if ( held < swap_hold_time() )
			continue;
		self swap( seat );
		while ( passenger UseButtonPressed() )
			wait 0.05;	// let go first
	}
}

// self: the player's Humvee.
swap( seat )
{
	turret = level.player_turret;
	prev_gunner = maps\_mw2coop::vehicle_gunner();
	next_gunner = seat.mw2coop_player;

	// a second UseBy takes the gunner off the turret (it also unlinks him). Each step gets its
	// own server frame: linking the old gunner to the seat in the frame he leaves the turret
	// crashed both machines on the second swap (TeleportPlayer on an entity without a client,
	// rva 0x15f515).
	prev_gunner EnableTurretDismount();
	turret UseBy( prev_gunner );
	wait 0.05;
	if ( prev_gunner IsLinked() )
		prev_gunner Unlink();
	next_gunner Unlink();
	wait 0.05;

	// the passenger takes the turret
	self mount_turret( next_gunner );
	wait 0.05;

	// the old gunner takes the seat
	seat maps\_mw2coop::link_to_seat( prev_gunner );
	maps\_mw2coop::state( "roadkill: seats swapped, gunner is " + player_text( next_gunner ) );
}

// self: the player's Humvee. Puts player on its turret, as player_gets_on_turret() does.
mount_turret( player )
{
	turret = level.player_turret;
	player EnableWeapons();
	player AllowCrouch( true );
	player AllowProne( true );
	player PlayerLinkToDelta( self, "tag_player", 0.35, 360, 360, 45, 30, true );
	turret MakeUsable();
	turret SetMode( "manual" );
	turret UseBy( player );
	turret MakeUnusable();
	player SetPlayerAngles( ( 0, self.angles[ 1 ], 0 ) );
	player DisableTurretDismount();
	level.mw2coop_gunner = player;
}

// roadkill.gsc, right before the ride ends (player 1 is unlinked and the turret deleted there).
ride_ends()
{
	if ( !maps\_mw2coop::is_campaign_coop() )
		return;
	level notify( "mw2coop_ride_ends" );
	gunner = maps\_mw2coop::vehicle_gunner();
	if ( gunner != level.player && isdefined( level.player_turret ) )
	{
		org = gunner.origin;
		gunner EnableTurretDismount();
		level.player_turret UseBy( gunner );
		gunner Unlink();
		gunner SetOrigin( org );
	}
	seat = level.mw2coop_roadkill_seat;
	level.mw2coop_roadkill_seat = undefined;
	if ( isdefined( seat ) )
		seat maps\_mw2coop::release_seat();
	level.mw2coop_gunner = undefined;
}

player_text( player )
{
	if ( player == level.player )
		return "player 1";
	return "player 2";
}


// Shepherd's "get on the line" scene (shepherd_roams_battlefield): self is player 2, linked to
// player 1's view rig as well (the players have no bodies to collide).
share_scene_rig( rig, arc )
{
	self AllowProne( false );
	self AllowCrouch( false );
	self DisableWeapons();
	self PlayerLinkToDelta( rig, "tag_player", 1, arc, arc, arc, arc, 1 );
}

leave_scene_rig()
{
	self Unlink();
	self teleport_next_to_player1();
	self AllowProne( true );
	self AllowCrouch( true );
	self EnableWeapons();
}

teleport_next_to_player1()
{
	self thread maps\_mw2coop::teleport_to_partner_when_safe( level.player, 5 );
}
