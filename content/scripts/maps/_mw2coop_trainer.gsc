// mw2coop: S.S.D.D. (trainer) for two players. Called from the patch of maps/trainer.gsc; names
// only the common scripts (it is loaded with the trainer zone alone).
//
// Weapons on the range (owner, 2026-10-21): each player takes his own, as he would alone. In
// COD-MOD one player could take the rifle off the table and the other could not; mw2coop first
// handed the taken weapon to both. Now another weapon of the same kind lies in its place until
// every player has taken one, and the level goes on once both have them.
#include common_scripts\utility;
#include maps\_utility;

// The weapon lying on the table (targetname) is taken by one player: another lies in its place,
// glowing and with the same parts hidden, until every player has that weapon.
own_pickup( targetname, weapon, hidden_parts )
{
	level endon( "mw2coop_level_end" );
	item = GetEnt( targetname, "targetname" );
	if ( !isdefined( item ) )
		return;
	if ( !isdefined( level.mw2coop_pickups ) )
		level.mw2coop_pickups = [];
	level.mw2coop_pickups[ targetname ] = item;
	classname = item.classname;
	model = item.model;	// before the level makes it glow (glow() puts "_obj" on the model)
	origin = item.origin;
	angles = item.angles;
	respawns = 0;
	for ( ;; )
	{
		wait 0.05;
		if ( isdefined( item ) )
		{
			// whoever has one already would pick the new one up as ammo at once (QA: it was gone
			// within a second, and player 2's place in the bay is by the table): the range's
			// ammo is endless anyway (never_run_out_of_ammo), so his is kept full while it lies
			// there, and a player with full ammo cannot take it
			if ( respawns > 0 )
				foreach ( player in level.players )
					if ( player HasWeapon( weapon ) && Distance( player.origin, origin ) < 128 )
						player GiveMaxAmmo( weapon );
			continue;
		}
		if ( everyone_has( weapon ) )
			break;
		foreach ( player in level.players )
			if ( player HasWeapon( weapon ) )
				player GiveMaxAmmo( weapon );
		item = Spawn( classname, origin, 1 );	// suspended: it stays on the table
		item.angles = angles;
		item ItemWeaponSetAmmo( 999, 999 );
		respawns++;
		// a spawned weapon has no model name yet: glow() alone made it "_obj", nothing to see
		// (owner took the rifle in the QA run: no second one on the table)
		item SetModel( model );
		item glow();
		if ( isdefined( hidden_parts ) )
			foreach ( part in hidden_parts )
				item HidePart( part );
		level.mw2coop_pickups[ targetname ] = item;
		maps\_mw2coop::state( "trainer: another " + weapon + " on the table (" + respawns + ")" );
	}
	level.mw2coop_pickups[ targetname ] = undefined;
}

// The parts firing_range_hip_and_ads() hides on the rifle on the table.
rifle_hidden_parts()
{
	return strtok( "TAG_THERMAL_SCOPE TAG_ACOG_2 TAG_EOTECH TAG_HEARTBEAT TAG_M203 TAG_RED_DOT TAG_SHOTGUN TAG_SILENCER", " " );
}

everyone_has( weapon )
{
	foreach ( player in level.players )
		if ( !player HasWeapon( weapon ) )
			return false;
	return true;
}

// The drill waits until every player has at least count frag grenades.
everyone_has_frags( count )
{
	foreach ( player in level.players )
		if ( player GetWeaponAmmoStock( "fraggrenade" ) < count )
			return false;
	return true;
}

// The firing range's start (player_start_range) is one man's place in the sandbag bay. In co-op
// player 1 stands 48 units left of it and player 2 on it, both facing downrange, both inside the
// sandbags (owner, 2026-10-21: player 2 was put behind the bags). Measured from the start in QA
// pickups: the bags in front are 40 to 55 units ahead from 90 units left up to the start, the
// rifle table 10 to 18 units ahead on its right, a post just behind it. After the level start's
// regroup (maps\_mw2coop::regroup_after_start), which would otherwise move player 2 again.
range_places( start )
{
	if ( !isdefined( level.player2 ) )
		return;
	level endon( "mw2coop_level_end" );
	while ( !flag_exist( "mw2coop_regrouped" ) )
		wait 0.05;
	flag_wait( "mw2coop_regrouped" );
	right = AnglesToRight( ( 0, start.angles[ 1 ], 0 ) );
	level.player SetOrigin( on_floor( start.origin - right * 48 ) );
	level.player SetPlayerAngles( start.angles );
	level.player2 SetOrigin( on_floor( start.origin ) );
	level.player2 SetPlayerAngles( start.angles );
	maps\_mw2coop::state( "trainer: both in the sandbag bay at the range" );
}

on_floor( at )
{
	return PlayerPhysicsTrace( at + ( 0, 0, 48 ), at - ( 0, 0, 96 ) );
}
