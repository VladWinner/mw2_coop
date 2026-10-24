// mw2coop: The Gulag for two players. Called from mw2coop/patches/maps/gulag_code.gsc.patch and
// gulag.gsc.patch; outside a co-op campaign session every entry point does nothing.
//
// Fly-in: player 1 rides the helicopter on a hidden view controller turret at tag_guy2
// (spawn_player_heli). Player 2 spawned at the map's player start, which hangs over nothing:
// he fell 67000 units in the spawn audit. He now gets his own view controller at the same door,
// side by side with player 1 as COD-MOD seats them (tag_guy2, player 1 20 units one way, player 2
// 10 units the other), looks where player 1 is made to look, and gets off next to him after the
// landing. A seat at the other door (QA tour 2026-10-22) had him look at the cabin's floor.
#include common_scripts\utility;
#include maps\_utility;
#include maps\_anim;

// Where a player's view controller sits on tag_guy2 (player 1: 1, player 2: 2); player 1 alone
// keeps the level's own place.
door_offset( n )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return ( 0, 0, -8 );
	if ( n == 1 )
		return ( 20, 0, -8 );
	return ( -10, 0, -8 );
}

// spawn_player_heli(), right after player 1 is linked to his view controller.
partner_rides_heli()
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	heli = level.player_heli;
	controller = get_player_view_controller( heli, "tag_guy2", door_offset( 2 ) );
	if ( isdefined( level.view_org ) )
		controller SetTargetEntity( level.view_org );
	level.mw2coop_view_controller2 = controller;

	p2 = level.player2;
	p2 SetOrigin( heli GetTagOrigin( "tag_guy2" ) );
	p2 AllowProne( false );
	p2 AllowCrouch( false );
	p2 AllowSprint( false );
	p2 AllowJump( false );
	if ( isdefined( level.ground_ref ) )
		p2 PlayerSetGroundReferenceEnt( level.ground_ref );
	// the same view limits as player 1 (spawn_player_heli)
	p2 PlayerLinkToDelta( controller, "TAG_aim", 1, -35, 40, -35, 40, true );
	maps\_mw2coop::state( "gulag: player 2 rides the helicopter at player 1's door" );
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

// --------------------------------------------------------------------------------------------
// The cave-in chase out of the tunnels (gulag_ending_code.gsc): the collapse follows both players
// and buries whichever one it catches. Player 1's screen darkens on the level's black_overlay,
// player 2's on his own.

// The buried player: player 1 unless the chase caught the other one.
cavein_default()
{
	if ( !isdefined( level.mw2coop_cavein_player ) )
		level.mw2coop_cavein_player = level.player;
}

// The collapse is at org, player 1 chase_dist from it (flat). True when it reaches a player, who
// is then the one buried.
cavein_catches( org, chase_dist, reach )
{
	if ( chase_dist < reach )
	{
		level.mw2coop_cavein_player = level.player;
		return true;
	}
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) || !IsAlive( level.player2 ) )
		return false;
	if ( Distance2D( level.player2.origin, org ) >= reach )
		return false;
	level.mw2coop_cavein_player = level.player2;
	return true;
}

// A player in the volume that buries whoever runs the wrong way.
anyone_touching( volume )
{
	foreach ( player in level.players )
	{
		if ( player != level.player && !maps\_mw2coop::is_campaign_coop() )
			continue;
		if ( player IsTouching( volume ) )
		{
			level.mw2coop_cavein_player = player;
			return true;
		}
	}
	return false;
}

// With the level's black screen: player 2's own (on), or gone (off).
partner_black_screen( on )
{
	if ( isdefined( level.mw2coop_chase_black ) )
		level.mw2coop_chase_black Destroy();
	level.mw2coop_chase_black = undefined;
	if ( !on || !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	hud = maps\_hud_util::create_client_overlay( "black", 0, level.player2 );
	hud.alpha = 1;
	hud.foreground = false;
	level.mw2coop_chase_black = hud;
}

// Player 2's screen darkens by his own distance to the collapse at org, as player 1's does.
partner_chase_fade( org, max_black, black_range )
{
	if ( !isdefined( level.mw2coop_chase_black ) )
		return;
	alpha = 1 - ( ( Distance2D( level.player2.origin, org ) - max_black ) / black_range );
	level.mw2coop_chase_black.alpha = clamp( alpha, 0, 1 );
	level.mw2coop_chase_black FadeOverTime( 0.2 );
}

// --------------------------------------------------------------------------------------------
// The cellblock rappel (gulag_code.gsc player_rappels, cellblock_rappel_player): the level's
// player alone held the use button at the rope, rode the rope's animation with his own rig and
// came off at the bottom; player 2 stayed at the top (owner, 2026-10-24: "the rope: there may be
// problems like with the ice pick"). Now whoever uses the rope first rappels, and the other uses it
// after him: the same rope plays its animation again for him (rappel_rope_again), with a rig of
// his own. The level goes on as soon as the first is on the rope, as it did.

// player_rappels(): who goes down this time. Before co-op's second rappel the rope is not usable.
rappel_wait( trigger, flag_name )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
	{
		flag_wait( flag_name );
		return level.player;
	}
	for ( ;; )
	{
		trigger waittill( "trigger", who );
		if ( !isdefined( who ) || !IsPlayer( who ) || isdefined( who.mw2coop_rappelled ) || isdefined( level.mw2coop_rappelling ) )
			continue;
		who.mw2coop_rappelled = true;
		level.mw2coop_rappelling = who;
		flag_set( flag_name );
		return who;
	}
}

// player_rappels(), where it deleted the use trigger. self: the trigger. who: this rappel's player.
rappel_trigger_used( who )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
	{
		self Delete();
		return;
	}
	// the rope stays usable for the other player once the first is down
	self trigger_off();
	other = maps\_mw2coop::other_player( who );
	if ( isdefined( other.mw2coop_rappelled ) )
		self Delete();
}

// player_rappels(), where it deleted the rope's glowing outline. self: the outline. The first
// player only hides it: the other player's rope needs it again.
rappel_obj_done( who )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
	{
		self Delete();
		return;
	}
	other = maps\_mw2coop::other_player( who );
	if ( isdefined( other.mw2coop_rappelled ) )
		self Delete();
	else
		self Hide();
}

// cellblock_rappel_player(), after the first player is on the rope. self: level. When his scene is
// over the rope hangs ready again and the other player uses it, as the first did.
rappel_partner( ent )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	level endon( "mw2coop_level_end" );
	wait ent.unlink_time + 6;	// the scene is over: the first player is down
	level.mw2coop_rappelling = undefined;
	ent.rope_ent anim_first_frame_solo( ent.rope, ent.scene );
	if ( isdefined( ent.rope_obj ) )
		ent.rope_obj Show();
	trigger = GetEnt( "rappel_trigger", "script_noteworthy" );
	if ( !isdefined( trigger ) )
		return;
	trigger trigger_on();
	maps\_mw2coop::state( "gulag: the rope is free for the second player" );
	maps\gulag_code::player_rappels( ent );
}
