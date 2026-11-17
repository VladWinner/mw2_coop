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

// --------------------------------------------------------------------------------------------
// The rope as the other player sees it (owner, 2026-11-17: "on the way down the players see second hands on each other"). The level's rigs are
// first-person hands (viewhands_player_tf141) at the rappeller's eye: shown during the hook-up and the knife kill, they hung beside the rappeller's
// own body, which went down the rope upright in its standing pose (QA rappelview frames). Now the rigs and the knife are hidden from the other
// player, the rappeller's own body is off for the scene, and the other player sees a stand-in: the rappeller's body mesh on Price's skeleton
// (maps\_mw2coop_bodies::rider_model: the owner's rule for a player in an AI animation) with his head, playing Price's animations of the same scene
// at the rig's moment (the hook-up, the jump down the rope, the knife kill), its eyes kept where the rappeller's camera is.
#using_animtree( "generic_human" );

rope_skeleton()
{
	return "body_desert_tf141_assault_a";	// Price's body in this level (character_hero_desert_price)
}

// af_caves_rappel_behavior(), once the rigs and the rope exist. who: the rappeller.
partner_view( who, rig, ending_rig )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) || !isdefined( who.mw2coop_body ) || !isdefined( who.mw2coop_head ) )
		return;
	other = maps\_mw2coop::other_player( who );
	if ( !isdefined( other ) )
		return;
	rig hideOnClient( other );
	ending_rig hideOnClient( other );

	body = Spawn( "script_model", who.origin );
	body SetModel( maps\_mw2coop_bodies::rider_model( who.mw2coop_body, rope_skeleton() ) );
	body Attach( who.mw2coop_head, "", true );
	body hideOnClient( who );
	body DontCastShadows();
	body UseAnimTree( #animtree );
	level.mw2coop_rope_body = body;	// QA rappelview
	who maps\_utility::SetModelFunc( undefined );
	maps\_mw2coop::state( "af_caves: the rappeller seen by the other player as his body on Price's skeleton" );

	hookup = rig maps\_utility::getanim( "rappel_hookup" );
	far = rig maps\_utility::getanim( "rappel_far" );
	kill = ending_rig maps\_utility::getanim( "rappel_kill" );
	p_setup = level.scr_anim[ "price" ][ "pri_rappel_setup" ];
	p_jump = level.scr_anim[ "price" ][ "pri_rappel_jump" ];
	p_kill = level.scr_anim[ "price" ][ "pri_rappel_kill" ];
	phase = "";
	knife = undefined;
	for ( ;; )
	{
		if ( !isdefined( rig ) || !isdefined( ending_rig ) || !isalive( who ) )
			break;
		// again every frame: the level shows the rigs later (Show, which clears the per-player hide marks)
		rig hideOnClient( other );
		ending_rig hideOnClient( other );
		k = ending_rig GetAnimTime( kill );
		f = rig GetAnimTime( far );
		if ( k > 0.001 )
		{
			if ( phase != "kill" )
			{
				body ClearAnim( %root, 0 );
				body SetAnim( p_kill, 1, 0, 0 );
				knife = Spawn( "script_model", body.origin );
				knife SetModel( "weapon_parabolic_knife" );
				knife LinkTo( body, "tag_inhand", ( 0, 0, 0 ), ( 0, 0, 0 ) );
				knife hideOnClient( who );
				phase = "kill";
			}
			body SetAnimTime( p_kill, min( k, 0.999 ) );
			pace( body, who GetEye(), ending_rig GetTagAngles( "tag_player" )[ 1 ] );
		}
		else if ( f > 0.001 )
		{
			if ( phase != "down" )
			{
				body ClearAnim( %root, 0 );
				body SetAnim( p_jump, 1, 0, 0 );
				phase = "down";
			}
			// the descent's part of Price's jump: off the ledge at the start, the last of it his landing on the guard
			body SetAnimTime( p_jump, rope_jump_from() + ( rope_jump_to() - rope_jump_from() ) * min( f / 0.94, 1 ) );
			pace( body, who GetEye(), rig GetTagAngles( "tag_player" )[ 1 ] );
		}
		else
		{
			if ( phase != "hook" )
			{
				body ClearAnim( %root, 0 );
				body SetAnim( p_setup, 1, 0, 0 );
				phase = "hook";
			}
			body SetAnimTime( p_setup, min( rig GetAnimTime( hookup ), 0.999 ) );
			pace( body, who GetEye(), rig GetTagAngles( "tag_player" )[ 1 ] );
		}
		wait 0.05;
		if ( phase == "kill" && !who IsLinked() )
			break;
	}
	if ( isdefined( knife ) )
		knife Delete();
	body Delete();
	level.mw2coop_rope_body = undefined;
	if ( isdefined( who ) && IsAlive( who ) )
		who maps\_mw2coop_bodies::give_body( maps\_mw2coop::body_model );
}

// where Price's jump anim is at the top and at the bottom of the player's descent (fractions of the anim)
rope_jump_from()
{
	return 0.2;
}

rope_jump_to()
{
	return 0.85;
}

// self-less: the stand-in turned to yaw with its eyes at eye
pace( body, eye, yaw )
{
	body.angles = ( 0, yaw, 0 );
	tag = "tag_eye";
	at = body GetTagOrigin( tag );
	if ( !isdefined( at ) )
		at = body GetTagOrigin( "j_head" );
	body.origin = body.origin + ( eye - at );
}

// af_caves_rappel_behavior(), right after the knife of the kill is shown: a first-person prop like the rigs
hide_from_partner( ent, who )
{
	if ( !maps\_mw2coop::is_campaign_coop() || !isdefined( level.player2 ) )
		return;
	other = maps\_mw2coop::other_player( who );
	if ( isdefined( other ) )
		ent hideOnClient( other );
}
