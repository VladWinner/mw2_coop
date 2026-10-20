// mw2coop: Cliffhanger for two players. Called from mw2coop/patches/maps/
// cliffhanger_snowmobile.gsc.patch; outside a co-op campaign session every entry point does
// what the original code did.
//
// Snowmobile escape: player 2 gets a snowmobile of his own next to player 1's (the game's
// snowmobile driving code, maps\_snowmobile_drive, already handles a second player). The
// helicopter at the end waits until player 2 has arrived too.
#include common_scripts\utility;
#include maps\_utility;

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

// After "end_begins" (player 1 at the helicopter): wait for player 2 to get off his
// snowmobile next to player 1.
wait_partner_at_end()
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	said = false;
	for ( ;; )
	{
		p2 = level.player2;
		if ( !IsAlive( p2 ) )
			return;
		if ( !isdefined( p2.vehicle ) && Distance( p2.origin, level.player.origin ) < 400 )
			break;
		if ( !said )
		{
			maps\_mw2coop::state( "cliffhanger: the helicopter waits for player 2" );
			said = true;
		}
		wait 0.1;
	}
	maps\_mw2coop::state( "cliffhanger: both players at the helicopter" );
}
