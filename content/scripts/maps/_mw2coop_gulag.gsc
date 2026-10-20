// mw2coop: The Gulag for two players. Called from mw2coop/patches/maps/gulag_code.gsc.patch and
// gulag.gsc.patch; outside a co-op campaign session every entry point does nothing.
//
// Fly-in: player 1 rides the helicopter on a hidden view controller turret at tag_guy2
// (spawn_player_heli). Player 2 spawned at the map's player start, which hangs over nothing:
// he fell 67000 units in the spawn audit. He now gets his own view controller on another seat
// of the same helicopter (COD-MOD did the same with a second controller), looks where player 1
// is made to look, and gets off next to him after the landing.
#include common_scripts\utility;
#include maps\_utility;

// spawn_player_heli(), right after player 1 is linked to his view controller.
partner_rides_heli()
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	heli = level.player_heli;
	tag = heli maps\_mw2coop::free_seat_tag( "tag_guy2" );
	if ( !isdefined( tag ) )
		tag = "tag_guy1";
	controller = get_player_view_controller( heli, tag, ( 0, 0, -8 ) );
	if ( isdefined( level.view_org ) )
		controller SetTargetEntity( level.view_org );
	level.mw2coop_view_controller2 = controller;

	p2 = level.player2;
	p2 SetOrigin( heli GetTagOrigin( tag ) );
	p2 AllowProne( false );
	p2 AllowCrouch( false );
	p2 AllowSprint( false );
	p2 AllowJump( false );
	if ( isdefined( level.ground_ref ) )
		p2 PlayerSetGroundReferenceEnt( level.ground_ref );
	p2 PlayerLinkToDelta( controller, "TAG_aim", 1, 45, 45, 15, 45, true );
	maps\_mw2coop::state( "gulag: player 2 rides the helicopter on " + tag );
	level thread partner_view_follows_landing( controller );
}

// The fly-in turns player 1's view controller to the gulag centre for the landing.
partner_view_follows_landing( controller )
{
	controller endon( "death" );
	flag_wait( "player_goes_in_for_landing" );
	center = GetEnt( "gulag_center", "targetname" );
	if ( !isdefined( center ) )
		return;
	controller ClearTargetEntity();
	controller SetTargetEntity( center );
}

// gulag.gsc, after the landing deleted player 1's view controller.
partner_leaves_heli()
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	p2 = level.player2;
	if ( isdefined( level.mw2coop_view_controller2 ) )
		level.mw2coop_view_controller2 Delete();
	if ( p2 IsLinked() )
		p2 Unlink();
	p2 PlayerSetGroundReferenceEnt( undefined );
	p2 AllowProne( true );
	p2 AllowCrouch( true );
	p2 AllowSprint( true );
	p2 AllowJump( true );
	p2 thread maps\_mw2coop::teleport_to_partner_when_safe( level.player, 10 );
	maps\_mw2coop::state( "gulag: player 2 off the helicopter" );
}
