// mw2coop: Cliffhanger for two players. Called from mw2coop/patches/maps/
// cliffhanger_snowmobile.gsc.patch; outside a co-op campaign session every entry point does
// what the original code did.
//
// Snowmobile escape: player 2 gets a snowmobile of his own next to player 1's (the game's
// snowmobile driving code, maps\_snowmobile_drive, already handles a second player). The
// helicopter ending starts when either player reaches it (player 2 gets a few seconds to come
// alongside). Both players are tougher for the ride.
#include common_scripts\utility;
#include maps\_utility;
#using_animtree( "generic_human" );

// self: player 1's snowmobile, just spawned from spawner. mount_now: start points that put
// the players on their snowmobiles right away.
partner_snowmobile( spawner, mount_now )
{
	if ( !maps\_mw2coop::is_campaign_coop() )
		return;
	// player 1's spawner spawns once; the map's second player snowmobile spawner (used by the
	// developer start points) gives player 2 his
	second = GetEnt( "playermobile_spawner", "targetname" );
	if ( isdefined( second ) )
		spawner = second;
	// the spawner is moved next to player 1's snowmobile, then put back
	org = spawner.origin;
	angles = spawner.angles;
	spawner.origin = self.origin + AnglesToRight( self.angles ) * 90;
	spawner.angles = self.angles;
	partner = spawner spawn_vehicle();
	spawner.origin = org;
	spawner.angles = angles;
	if ( !isdefined( partner ) )
	{
		maps\_mw2coop::state( "cliffhanger: no second snowmobile spawned" );
		return;
	}
	level.mw2coop_partner_snowmobile = partner;
	level thread tougher_players();
	level thread snowmobile_looks();
	partner thread maps\_snowmobile_drive::drive_vehicle();
	maps\_mw2coop::state( "cliffhanger: snowmobile for player 2 spawned" );
	if ( isdefined( mount_now ) && mount_now && isdefined( level.player2 ) && IsAlive( level.player2 ) )
		level.player2 player_mount_vehicle( partner );
}

// self: the snowmobile the level gave player 1. Returns the player who mounted (player 1), as
// the original "vehicle_mount" wait did. In co-op either player may take either snowmobile;
// the one player 1 rides becomes level.player_snowmobile.
wait_player1_mounts()
{
	if ( !maps\_mw2coop::is_campaign_coop() )
	{
		self waittill( "vehicle_mount", other );
		return other;
	}
	while ( !isdefined( level.player.vehicle ) )
		wait 0.05;
	if ( level.player.vehicle != self )
	{
		level.mw2coop_partner_snowmobile = self;
		level.player_snowmobile = level.player.vehicle;
		maps\_mw2coop::state( "cliffhanger: player 1 took the second snowmobile" );
	}
	return level.player;
}

// After "end_begins" (a player at the helicopter): the level fades out 2.5 s later and goes on
// to the next mission, as in single player (COD-MOD left this ending as it was). Player 2 gets up
// to 3 seconds to come alongside; the ending goes on either way. Playtest 2026-10-23: waiting for
// player 2 to get off his snowmobile never ended (nobody gets off), both drove into the
// helicopter and something in the level failed the mission ten seconds later.
wait_partner_at_end()
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	for ( t = 0; t < 3; t += 0.1 )
	{
		if ( !IsAlive( level.player2 ) || Distance( level.player2.origin, level.player.origin ) < 1500 )
			break;
		wait 0.1;
	}
	maps\_mw2coop::state( "cliffhanger: the helicopter ending starts" );
}

// The snowmobile escape, owner 2026-10-23: more health for both players ("в конце скалолаза
// увеличить здоровье"). 250 instead of 100; set again if something puts it back (a revive).
tougher_players()
{
	level endon( "mw2coop_level_end" );
	if ( isdefined( level.mw2coop_tougher ) )
		return;
	level.mw2coop_tougher = true;
	maps\_mw2coop::state( "cliffhanger: both players have 250 health for the snowmobile escape" );
	for ( ;; )
	{
		foreach ( player in level.players )
			if ( IsAlive( player ) && player.maxhealth < 250 && !maps\_mw2coop::is_downed( player ) )
			{
				ratio = player.health / player.maxhealth;
				player.maxhealth = 250;
				player.health = int( 250 * ratio );
				if ( player.health < 1 )
					player.health = 1;
			}
		wait 1;
	}
}

// Each rider's snowmobile is the first-person model (cockpit, hands, no body), which is all the
// other player saw (owner's playtest, 2026-10-23: "the texture of the snowmobile disappears, a
// simplified one appears, and I see only his hands"). The other player gets the level's own
// full third-person snowmobile in its place, linked to the vehicle; the first-person one is hidden
// from him.
snowmobile_looks()
{
	if ( isdefined( level.mw2coop_snow_looks ) )
		return;
	level.mw2coop_snow_looks = true;
	for ( ;; )
	{
		wait 0.25;
		foreach ( player in level.players )
		{
			vehicle = player.vehicle;
			if ( !isdefined( vehicle ) || isdefined( vehicle.mw2coop_copy ) || !isdefined( vehicle.snowmobile_3rdPersonModel ) )
				continue;
			other = maps\_mw2coop::other_player( player );
			if ( !isdefined( other ) )
				continue;
			vehicle thread snowmobile_copy( player, other );
		}
	}
}

// self: a snowmobile a player rides. The third-person look for the other player.
snowmobile_copy( rider, other )
{
	copy = Spawn( "script_model", self.origin );
	copy SetModel( self.snowmobile_3rdPersonModel );
	copy.angles = self.angles;
	copy LinkTo( self, "tag_origin", ( 0, 0, 0 ), ( 0, 0, 0 ) );
	copy DontCastShadows();
	copy hideOnClient( rider );
	self hideOnClient( other );
	self.mw2coop_copy = copy;
	self thread snowmobile_rider( rider, other );
	maps\_mw2coop::state( "cliffhanger: " + self.snowmobile_3rdPersonModel + " for the other player's view" );
	self waittill_either( "vehicle_dismount", "death" );
	self.mw2coop_copy = undefined;
	copy Delete();
	if ( isdefined( self ) )
		self showOnClient( other );
}

// The rider's own body stays where he got on (the level hides it from the other player and
// leaves the snowmobile empty: owner, 2026-10-23, "where is the player itself?"). A body of his
// own (co-op body and head) sits on the seat instead; the rider never sees it, and his own model
// is taken off for the ride.
// self: the snowmobile.
// The rider's pose, as the level's AI driver steering straight (animscripts\snowmobile::
// snowmobile_loop_driver). His co-op body's multiplayer skeleton twists these animations (QA
// snowview, 2026-10-24: an arm thrown up, the body bent across the seat; no multiplayer pose sits
// on a seat with the hands on the grips and the feet on the footrests), so the rider wears Soap's
// skeleton with his own body's mesh (maps\_mw2coop_bodies::rider_model; owner: "take Soap's ready
// skeleton and use it").
rider_pose()
{
	self UseAnimTree( #animtree );
	self SetAnimKnobAll( %sm_turn, %body, 1, 0 );
	self SetAnim( %snowmobile_driver_driving_idle, 1, 0 );
	self SetAnimKnob( %snowmobile_driver_lean_L2R, 1, 0 );
	self SetAnimTime( %snowmobile_driver_lean_L2R, 0.5 );
}

snowmobile_rider( rider, other )
{
	body = Spawn( "script_model", self GetTagOrigin( "tag_driver" ) );
	// his own body's mesh on Soap's skeleton, and his head
	body SetModel( maps\_mw2coop_bodies::rider_model( rider.mw2coop_body ) );
	body Attach( rider.mw2coop_head, "", true );
	body.angles = self GetTagAngles( "tag_driver" );
	body LinkTo( self, "tag_driver", ( 0, 0, 0 ), ( 0, 0, 0 ) );
	body rider_pose();
	body hideOnClient( rider );
	self.mw2coop_rider = body;	// QA snowview
	rider maps\_utility::SetModelFunc( undefined );
	self waittill_either( "vehicle_dismount", "death" );
	body Delete();
	if ( isdefined( rider ) && IsAlive( rider ) )
		rider maps\_mw2coop_bodies::give_body( maps\_mw2coop::body_model );
}
