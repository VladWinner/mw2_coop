// mw2coop: Just Like Old Times for two players. Called from mw2coop/patches/maps/af_caves.gsc.patch
// and af_caves_code.gsc.patch; outside a co-op campaign session every entry point does what the
// original code did.
//
// The opening rappel (af_caves_code.gsc af_caves_rappel_behavior) was a scene of level.player alone:
// he hooked up, rode the rappel animation with his own rig and killed the guard below with the knife;
// player 2 stayed at the top. Now whoever uses the hook-up first rappels and kills the guard as the
// level did. When his scene is over the hook-up is free again, and the other player rappels the same
// way with a rig of his own: the guards are dead by then, so he ends the descent on the ledge
// beside the first (second_done), without the knife kill.
#include common_scripts\utility;
#include maps\_utility;

// rappel_player_rappel_setup(), where it deleted the use trigger. who: this rappel's player.
trigger_used( trigger, who )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
	{
		trigger Delete();
		return;
	}
	level.mw2coop_rappel_first = who;
	if ( !isdefined( level.flag[ "mw2coop_cave_down" ] ) )
		flag_init( "mw2coop_cave_down" );
	level thread down_fallback();
	trigger trigger_off();
}

// The checkpoint "barracks" is reached when both players are down the rope (cave_down); should
// the other not follow, it is reached a minute and a half after the first one's scene.
down_fallback()
{
	level endon( "mw2coop_level_end" );
	flag_wait( "end_of_rappel_scene" );
	wait 90;
	cave_down();
}

cave_down()
{
	if ( isdefined( level.flag[ "mw2coop_cave_down" ] ) )
		flag_set( "mw2coop_cave_down" );
}

// af_caves_rappel_behavior(), at the top. True for the second player's rappel.
is_second( who )
{
	return isdefined( level.mw2coop_rappel_first ) && level.mw2coop_rappel_first != who;
}

// The player who rappelled first: the guard below him is the one who shoots a player who did not
// brake in time or was too slow to kill.
first_rappeller()
{
	if ( isdefined( level.mw2coop_rappel_first ) )
		return level.mw2coop_rappel_first;
	return level.player;
}

// af_caves_rappel_behavior(), where its rope is spawned: the first player's rope hangs at the
// bottom of its animation, the second player's rope replaces it.
rope_made( rope )
{
	if ( isdefined( level.mw2coop_rappel_rope ) )
		level.mw2coop_rappel_rope Delete();
	level.mw2coop_rappel_rope = rope;
}

// player_gets_groundref_and_opens_fov(): the _utility function of the same name links player 1.
link_fov( who, time, rig, tag, arc_right, arc_left, arc_top, arc_bottom )
{
	who endon( "stop_opening_fov" );
	wait time;
	who PlayerLinkToDelta( rig, tag, 1, arc_right, arc_left, arc_top, arc_bottom, true );
}

// af_caves_rappel_behavior(), where the level set its flag "rappel_end" (the descent's ending part).
rappel_end_set( who )
{
	who.mw2coop_rappel_end = true;
	flag_set( "rappel_end" );
}

// af_caves_rappel_behavior(): is this player's descent in its ending part (the level's flag
// "rappel_end" is the first player's, and stays set for the second).
rappel_ending( who )
{
	return isdefined( who.mw2coop_rappel_end );
}

// rappel_player_rappel_setup(), after the first player's scene: the hook-up is free again for
// the other player. first: the player who rappelled.
partner_turn( trigger, first )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	level endon( "mw2coop_level_end" );
	other = maps\_mw2coop::other_player( first );
	if ( !isdefined( other ) || !isalive( other ) )
	{
		cave_down();
		return;
	}

	railing = GetEnt( "rappel_hookup", "targetname" );
	glowing = GetEnt( "rappel_hookup_glowing", "targetname" );
	if ( isdefined( railing ) && isdefined( glowing ) )
	{
		railing Hide();
		glowing Show();
	}
	trigger trigger_on();
	maps\_mw2coop::state( "af_caves: the hook-up is free for the second player" );
	for ( ;; )
	{
		trigger waittill( "trigger", who );
		if ( who != other || who IsThrowingGrenade() || who IsSwitchingWeapon() )
			continue;
		break;
	}
	trigger Delete();
	if ( isdefined( railing ) && isdefined( glowing ) )
	{
		railing Show();
		glowing Hide();
	}
	maps\_mw2coop::state( "af_caves: the second player rappels" );
	maps\af_caves_code::af_caves_rappel_behavior( who );
}

// af_caves_rappel_behavior(), after the second player's descent: no guard is left to kill, so he
// comes off the rope onto the ledge where the first player's scene ended and has his things back.
second_done( who, ending_rig, rig, old_weapon )
{
	node = GetEnt( "guard_assassinate", "script_noteworthy" );
	who PlayerSetGroundReferenceEnt( undefined );
	who Unlink();
	if ( isdefined( node ) )
		who SetOrigin( node.origin );
	if ( who HasWeapon( "rappel_knife" ) )
		who TakeWeapon( "rappel_knife" );
	ending_rig Delete();
	rig Delete();
	who EnableWeapons();
	who EnableOffhandWeapons();
	who EnableWeaponSwitch();
	who AllowCrouch( true );
	who AllowProne( true );
	if ( old_weapon != "none" && who HasWeapon( old_weapon ) )
		who SwitchToWeapon( old_weapon );
	SetSavedDvar( "compass", 1 );
	SetSavedDvar( "ammoCounterHide", 0 );
	SetSavedDvar( "actionSlotsHide", 0 );
	SetSavedDvar( "hud_showStance", 1 );
	SetSavedDvar( "hud_drawhud", 1 );
	cave_down();
	who thread maps\_mw2coop::teleport_to_partner_when_safe( maps\_mw2coop::other_player( who ), 3 );
	maps\_mw2coop::state( "af_caves: the second player is down" );
}

// --------------------------------------------------------------------------------------------
// The cave exit (af_caves_backhalf.gsc AAA_sequence_airstrip, patched by tools/gen_seek_patches.py): the control room blows up behind the
// players. Player 1 hears the self-destruct alarm, is knocked down onto a view of his own (crouched, slow, no sprint or jump, invulnerable, shell
// shocked for 17 s), blacks out, comes to at the cave mouth (cave_exit_playerview_01) on that view, which tilts over as he passes out again, and
// wakes up at the same place, held still until the second blackout lifts. Player 2 kept running, unprotected and unshocked, and was left in the
// cave behind the rubble that closes it. Now he goes through the same moments, on a view of his own, beside player 1.

collapse_coop()
{
	return maps\_mw2coop::is_campaign_coop() && isdefined( level.player2 ) && IsAlive( level.player2 ) && !maps\_mw2coop::is_downed( level.player2 );
}

// The alarm, a sound played for one client: on player 2's own machine too (on one machine, the split screen, player 1's is heard already).
partner_local_sound( alias )
{
	if ( !collapse_coop() || GetDvarInt( "mw2coop_native_split" ) )
		return;
	level.player2 PlayLocalSound( alias );
}

// Knocked down where he is: on a tag of his own at his place, with player 1's view angles (the cave mouth's, rolled over).
collapse_partner_link( p1_tag )
{
	if ( !collapse_coop() )
		return;
	p2 = level.player2;
	tag = spawn_tag_origin();
	tag.origin = p2.origin + ( 0, 0, 10 );
	tag.angles = p1_tag.angles;
	level.mw2coop_collapse_tag = tag;
	p2 PlayerLinkToBlend( tag, "tag_player", 1 );
	p2 AllowSprint( false );
	p2 SetMoveSpeedScale( 0.2 );
	p2 EnableInvulnerability();
	maps\_mw2coop::state( "cave exit collapse: player 2 knocked down " + int( Distance( p2.origin, level.player.origin ) ) + " units from player 1" );
}

collapse_partner_shock( time )
{
	if ( !collapse_coop() )
		return;
	p2 = level.player2;
	p2 ShellShock( "af_cave_collapse", time );
	p2 AllowStand( false );
	p2 AllowProne( false );
	p2 AllowSprint( false );
	p2 AllowJump( false );
	p2 AllowCrouch( true );
}

// Player 1 is put at the cave mouth while the screen is black: player 2 beside him, on his own tag.
collapse_partner_to_exit( view )
{
	if ( !collapse_coop() || !isdefined( level.mw2coop_collapse_tag ) )
		return;
	p2 = level.player2;
	spot = maps\_mw2coop::free_spot_beside( view.origin, view.angles, level.player );
	level.mw2coop_collapse_spot = spot;
	tag = level.mw2coop_collapse_tag;
	tag.origin = spot;
	p2 Unlink();
	p2 SetOrigin( spot );
	p2 PlayerLinkToBlend( tag, "tag_player", 1 );
	maps\_mw2coop::state( "cave exit collapse: player 2 at the cave mouth, " + int( Distance( spot, view.origin ) ) + " units from player 1's place" );
}

// His view tilts over with player 1's as they pass out again.
collapse_partner_rotate()
{
	if ( !collapse_coop() || !isdefined( level.mw2coop_collapse_tag ) )
		return;
	tag = level.mw2coop_collapse_tag;
	tag RotateTo( tag.angles + ( 40, 0, -70 ), 12, 5, 5 );
}

// The second blackout: off the tag, standing at his place beside player 1, facing out of the cave, held still until it lifts (the level lets both go).
collapse_partner_blackout( view )
{
	if ( !collapse_coop() )
		return;
	p2 = level.player2;
	spot = level.mw2coop_collapse_spot;
	if ( !isdefined( spot ) )
		spot = maps\_mw2coop::free_spot_beside( view.origin, view.angles, level.player );
	p2 Unlink();
	p2 SetOrigin( spot );
	p2 SetPlayerAngles( view.angles );
	p2 FreezeControls( true );
	p2 AllowStand( true );
	p2 AllowSprint( true );
	p2 AllowJump( true );
	p2 AllowCrouch( true );
	p2 AllowProne( true );
	if ( isdefined( level.mw2coop_collapse_tag ) )
		level.mw2coop_collapse_tag Delete();
	maps\_mw2coop::state( "cave exit collapse: player 2 held at the cave mouth for the blackout" );
}
