// mw2coop: Takedown (favela) for two players. Called from mw2coop/patches/maps/favela.gsc.patch;
// outside a co-op campaign session it does nothing.
//
// Opening car ride: player 1 rides on tag_passenger (player_rides_vehicle); player 2 rides on a
// free seat of the same car (in the spawn audit he stood in the street while the car left) and
// gets out next to player 1 when the level takes player 1 out (player_exits_vehicle).
#include common_scripts\utility;
#include maps\_utility;

// self: the car. max_ang: player 1's view arc.
partner_rides( max_ang )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	seat = self maps\_mw2coop::seat_partner_in_vehicle( level.player2, max_ang, max_ang, 45, 20, "tag_passenger" );
	if ( isdefined( seat ) )
		level thread partner_gets_out( seat );
}

partner_gets_out( seat )
{
	level waittill( "exiting_vehicle" );
	wait 1.6;	// player 1 is moved to the exit point over 1.5 s
	seat maps\_mw2coop::partner_leaves_seat();
}
