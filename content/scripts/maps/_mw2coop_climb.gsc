// mw2coop: the Cliffhanger ice climb and big jump for two players (maps\_climb, a common script).
// Called from mw2coop/patches/maps/_climb.gsc.patch, _climb_anim.gsc.patch and
// cliffhanger.gsc.patch. Outside a co-op campaign session every entry point does what the
// original code did.
//
// The level starts maps\_climb::climb_wall for every player at its own "player_climb" start
// (the map has two lanes), but the climb code works on level.player and on level flags. The
// patches turn it per player:
//  - level.player -> the climbing player in every function of the climb (patch `subst`s);
//  - the climb's flags (trigger volumes and the climb's own state) become per-player entity
//    flags here, fed by the same trigger volumes (pflag*, flag_monitor);
//  - the swing buttons are read from the player's usercmd (attack/ads), not from local keys,
//    so both lockstep machines see the same input;
//  - player 2 climbs his own lane; the shared spots at the top (pull-up, big jump, final
//    climb, getting up) are moved over by the distance between the two lanes (lane_*);
//  - big jump: Soap catches the players in turn (catch_queued).
#include common_scripts\utility;
#include maps\_utility;
#include maps\_anim;

climb_coop()
{
	return maps\_mw2coop::is_campaign_coop();
}

// --------------------------------------------------------------------------------------------
// Setup, from maps\_climb::climb_init() (replaces its "thread player_slides_off_cliff()").

climb_setup()
{
	level.mw2coop_qa_climb = maps\_mw2coop_climb_qa::climb;
	if ( !climb_coop() )
	{
		level.player thread maps\_climb::player_slides_off_cliff();
		return;
	}
	level.mw2coop_climb = SpawnStruct();
	c = level.mw2coop_climb;

	// flags fed by trigger volumes in the map: a player "has" the flag while he touches one
	// (flag_set_touching triggers), or from his first touch until the climb clears it again
	c.trigger_flags = [];
	foreach ( name in strtok( "player_in_position_to_climb climb_start climb_pullup climb_drop_down player_climbs_past_safe_point climb_jump_prep climb_big_jump climb_icepick_slide", " " ) )
	{
		triggers = flag_triggers( name );
		if ( !triggers.size )
		{
			maps\_mw2coop::state( "climb: no trigger for flag " + name + ", kept shared" );
			continue;
		}
		c.trigger_flags[ name ] = triggers;
	}
	// the climb's own state
	c.player_flags = [];
	foreach ( name in strtok( "we_care_about_right_icepick finished_climbing final_climb player_preps_for_jump player_makes_the_jump player_hangs_on player_was_caught price_caught_player climbing_dof fade_to_death reached_top player_starts_climbing", " " ) )
		c.player_flags[ name ] = true;
	foreach ( name, _ in c.trigger_flags )
		c.player_flags[ name ] = true;

	foreach ( player in level.players )
	{
		foreach ( name, _ in c.player_flags )
			if ( !player ent_flag_exist( name ) )
				player ent_flag_init( name );
		player thread flag_monitor();
		player thread maps\_climb::player_slides_off_cliff();
	}
	// the climb keeps the players on their lanes: no teleport to player 1 until both are up
	level.mw2coop_no_follow = true;
	thread end_of_climb();
	maps\_mw2coop::state( "climb: per-player flags ready, " + c.trigger_flags.size + " trigger flags" );
}

flag_triggers( name )
{
	found = [];
	triggers = getEntArrayWithFlag( name );
	foreach ( targetname in strtok( "flag_set flag_set_touching", " " ) )
		triggers = array_combine( triggers, GetEntArray( targetname, "targetname" ) );
	foreach ( trigger in triggers )
	{
		if ( !isdefined( trigger.script_flag ) || trigger.script_flag != name )
			continue;
		if ( !array_contains( found, trigger ) )
			found[ found.size ] = trigger;
	}
	return found;
}

is_touching_trigger( trigger )
{
	if ( IsSubStr( trigger.classname, "touching" ) )
		return true;
	return isdefined( trigger.targetname ) && trigger.targetname == "flag_set_touching";
}

// self: player. Sets his copy of each trigger flag while he touches its volume, as the level's
// flag triggers do for the shared flag.
flag_monitor()
{
	self endon( "death" );
	level endon( "reached_top" );
	c = level.mw2coop_climb;
	for ( ;; )
	{
		foreach ( name, triggers in c.trigger_flags )
		{
			touching = false;
			clears = false;
			cleared = false;
			foreach ( trigger in triggers )
			{
				if ( !isdefined( trigger ) )
					continue;
				if ( IsSubStr( trigger.classname, "clear" ) )
				{
					// flag_clear triggers clear the flag while touched
					if ( self IsTouching( trigger ) )
						cleared = true;
					continue;
				}
				if ( is_touching_trigger( trigger ) )
					clears = true;
				if ( self IsTouching( trigger ) )
					touching = true;
			}
			if ( cleared )
				touching = false;
			if ( touching && !self ent_flag( name ) )
				self ent_flag_set( name );
			else if ( !touching && ( clears || cleared ) && self ent_flag( name ) )
				self ent_flag_clear( name );
		}
		wait 0.05;
	}
}

end_of_climb()
{
	flag_wait( "reached_top" );
	level.mw2coop_no_follow = undefined;
	maps\_mw2coop::state( "climb: both players at the top" );
}

is_player_flag( name )
{
	return climb_coop() && isdefined( level.mw2coop_climb ) && isdefined( level.mw2coop_climb.player_flags[ name ] );
}

// --------------------------------------------------------------------------------------------
// Flags. self: the climbing player. A per-player flag set by one player also sets the shared
// flag (the rest of the level reads "a player did it"), except reached_top, which is set once
// every player has reached the top.

pflag( name )
{
	if ( !is_player_flag( name ) )
		return flag( name );
	return self ent_flag( name );
}

pflag_set( name )
{
	if ( !is_player_flag( name ) )
	{
		flag_set( name );
		return;
	}
	self ent_flag_set( name );
	if ( name == "reached_top" )
	{
		foreach ( player in level.players )
			if ( !player ent_flag( name ) )
			{
				maps\_mw2coop::state( "climb: " + player_name( self ) + " at the top, waiting for the other player" );
				return;
			}
	}
	if ( isdefined( level.flag[ name ] ) )
		flag_set( name );
}

pflag_clear( name )
{
	if ( !is_player_flag( name ) )
	{
		flag_clear( name );
		return;
	}
	self ent_flag_clear( name );
	foreach ( player in level.players )
		if ( player ent_flag( name ) )
			return;
	if ( isdefined( level.flag[ name ] ) )
		flag_clear( name );
}

pflag_wait( name )
{
	if ( !is_player_flag( name ) )
	{
		flag_wait( name );
		return;
	}
	self ent_flag_wait( name );
}

pflag_waitopen( name )
{
	if ( !is_player_flag( name ) )
	{
		flag_waitopen( name );
		return;
	}
	self ent_flag_waitopen( name );
}

player_name( player )
{
	if ( isdefined( level.player2 ) && player == level.player2 )
		return "player 2";
	return "player 1";
}

// --------------------------------------------------------------------------------------------
// Input. self: player. The climb read the mouse buttons and the j/k keys with ButtonPressed(),
// which reads this PC's keyboard: in lockstep the other machine would see different input. In
// co-op the swings come from the player's usercmd, the buttons the developers' comments name
// (left: attack, right: aim). QA can hold buttons for a player through self.mw2coop_qa_hold.

swing_pressed( arm, key )
{
	if ( !climb_coop() )
		return level.player ButtonPressed( key );
	if ( isdefined( self.mw2coop_qa_hold ) && isdefined( self.mw2coop_qa_hold[ arm ] ) && self.mw2coop_qa_hold[ arm ] )
		return true;
	if ( arm == "left" )
		return self AttackButtonPressed();
	return self AdsButtonPressed();
}

// ent: an arm of the climb. The extra keyboard keys (j, k) only outside co-op.
key_pressed( ent )
{
	if ( climb_coop() )
		return false;
	return ent.player ButtonPressed( ent.key );
}

// --------------------------------------------------------------------------------------------
// Lanes. Player 2 climbs the second lane; the single spots the climb uses at the top are moved
// for him by the distance between the lanes (across the wall, height kept).

// cave_main(): the climb start of the index-th player. The map has one "player_climb" start;
// the second player gets a lane beside it, on the side away from Soap's climb, where the wall
// has ice (or rock or snow: what the icepicks take) all the way up.
climb_start( starts, index )
{
	if ( isdefined( starts[ index ] ) )
		return starts[ index ];
	if ( isdefined( level.mw2coop_second_lane ) )
		return level.mw2coop_second_lane;
	base = starts[ 0 ];
	forward = AnglesToForward( ( 0, base.angles[ 1 ], 0 ) );
	right = AnglesToRight( ( 0, base.angles[ 1 ], 0 ) );
	away = 1;
	soap = GetEnt( "cliffhanger_cliff", "targetname" );
	if ( isdefined( soap ) && VectorDot( soap.origin - base.origin, right ) > 0 )
		away = -1;
	best = undefined;
	best_score = -1;
	dists = [];
	dists[ 0 ] = 72;
	dists[ 1 ] = 88;
	dists[ 2 ] = 56;
	dists[ 3 ] = 104;
	foreach ( dist in dists )
	{
		for ( side = 1; side >= -1; side -= 2 )
		{
			offset = right * ( dist * away * side );
			score = wall_score( base.origin + offset, forward );
			if ( score > best_score )
			{
				best_score = score;
				best = offset;
			}
		}
		if ( best_score >= 12 )
			break;
	}
	lane = SpawnStruct();
	lane.origin = base.origin + best;
	lane.angles = base.angles;
	level.mw2coop_second_lane = lane;
	maps\_mw2coop::state( "climb: second lane at " + best + " from the first, wall score " + best_score + "/12" );
	return lane;
}

// How many of 12 points up the wall in front of org the icepicks can take.
wall_score( org, forward )
{
	score = 0;
	for ( h = 0; h < 12; h++ )
	{
		at = org + ( 0, 0, h * 50 );
		trace = BulletTrace( at - forward * 40, at + forward * 120, false, undefined );
		if ( trace[ "fraction" ] < 1 && isdefined( trace[ "surfacetype" ] ) )
		{
			surface = trace[ "surfacetype" ];
			if ( surface == "ice" || surface == "rock" || surface == "snow" || surface == "plaster" )
				score++;
		}
	}
	return score;
}

// self: player, from climb_wall().
note_lane( start_org )
{
	self.mw2coop_climb_start = start_org;
}

lane_offset( player )
{
	if ( !climb_coop() || !isdefined( player ) || player == level.player )
		return ( 0, 0, 0 );
	if ( isdefined( player.mw2coop_lane_offset ) )
		return player.mw2coop_lane_offset;
	offset = undefined;
	if ( isdefined( player.mw2coop_climb_start ) && isdefined( level.player.mw2coop_climb_start ) )
	{
		d = player.mw2coop_climb_start - level.player.mw2coop_climb_start;
		offset = ( d[ 0 ], d[ 1 ], 0 );
	}
	if ( !isdefined( offset ) || Length( offset ) < 32 )
	{
		// start points past the wall: beside player 1, to the right of the jump
		node = GetEnt( "climb_jump_org", "targetname" );
		offset = AnglesToRight( ( 0, node.angles[ 1 ], 0 ) ) * 64;
	}
	player.mw2coop_lane_offset = offset;
	maps\_mw2coop::state( "climb: player 2 lane offset " + offset );
	return offset;
}

// Across the gap (big jump, Soap's catch, the second wall, getting up at the top) the lanes are
// side by side along that wall: player 2 is moved along climb_catch's right, to the side where
// that wall takes the icepicks.
far_offset( player )
{
	if ( !climb_coop() || !isdefined( player ) || player == level.player )
		return ( 0, 0, 0 );
	if ( isdefined( level.mw2coop_far_offset ) )
		return level.mw2coop_far_offset;
	far_wall = GetEnt( "climb_catch", "targetname" );
	forward = AnglesToForward( ( 0, far_wall.angles[ 1 ], 0 ) );
	right = AnglesToRight( ( 0, far_wall.angles[ 1 ], 0 ) );
	dist = Length( lane_offset( player ) );
	if ( dist < 56 )
		dist = 72;
	left_score = wall_score( far_wall.origin - right * dist, forward );
	right_score = wall_score( far_wall.origin + right * dist, forward );
	offset = right * dist;
	if ( left_score > right_score )
		offset = right * ( 0 - dist );
	level.mw2coop_far_offset = offset;
	maps\_mw2coop::state( "climb: player 2 across the gap at " + offset + " (wall score left " + left_score + ", right " + right_score + ")" );
	return offset;
}

// An entity standing for the named entity on the player's lane (player 1: the entity itself).
// side: "near" (the first wall) or "far" (across the gap). climb_catch is kept in co-op (see
// keep_or_delete), so the original is always there to copy.
lane_ent( name, player, side )
{
	ent = GetEnt( name, "targetname" );
	if ( !climb_coop() || !isdefined( player ) || player == level.player )
		return ent;
	if ( !isdefined( level.mw2coop_lane_ents ) )
		level.mw2coop_lane_ents = [];
	key = name + " " + side;
	if ( !isdefined( level.mw2coop_lane_ents[ key ] ) )
	{
		if ( side == "far" )
			offset = far_offset( player );
		else
			offset = lane_offset( player );
		copy = Spawn( "script_origin", ent.origin + offset );
		copy.angles = ent.angles;
		level.mw2coop_lane_ents[ key ] = copy;
	}
	return level.mw2coop_lane_ents[ key ];
}

lane_struct( name, player, side )
{
	s = getstruct( name, "targetname" );
	if ( !climb_coop() || !isdefined( player ) || player == level.player )
		return s;
	offset = lane_offset( player );
	if ( side == "far" )
		offset = far_offset( player );
	copy = SpawnStruct();
	copy.origin = s.origin + offset;
	copy.angles = s.angles;
	return copy;
}

// climb_catch is deleted after the first jump; in co-op player 2 still needs its place.
keep_or_delete( ent )
{
	if ( !climb_coop() )
		ent Delete();
}

// --------------------------------------------------------------------------------------------
// Things done once for the whole level, or for every player, where the climb did them once for
// level.player.

// cliff_scene_with_price(): put the players at the bottom of the wall, each in front of his lane.
teleport_to_cave()
{
	if ( !climb_coop() )
	{
		thread maps\_climb::teleport_to_cave();
		return;
	}
	foreach ( player in level.players )
		player thread maps\_climb::teleport_to_cave();
}

// self: player, in teleport_to_cave(). Player 2 stands beside player 1, on his lane's side.
cave_offset()
{
	if ( !climb_coop() || self == level.player )
		return ( 0, 0, 0 );
	offset = lane_offset( self );
	side = 40;
	if ( Length( offset ) < side )
		side = Length( offset );
	return VectorNormalize( offset ) * side;
}

death_triggers()
{
	if ( !climb_coop() )
	{
		thread maps\_climb::death_trigger();
		return;
	}
	foreach ( player in level.players )
		player thread maps\_climb::death_trigger();
}

give_icepick_ammo()
{
	foreach ( player in level.players )
	{
		if ( player HasWeapon( level.ice_pick_viewweapon ) )
		{
			player GiveMaxAmmo( level.ice_pick_viewweapon );
			player SetWeaponAmmoClip( level.ice_pick_viewweapon, 90 );
		}
	}
}

// player_big_jump(): Soap jumps the gap once, for whoever gets to the ledge first.
price_jump_once()
{
	if ( isdefined( level.mw2coop_price_jump ) )
		return;
	level.mw2coop_price_jump = true;
	thread maps\_climb::price_makes_the_jump( GetEnt( "climb_jump_org", "targetname" ) );
}

// On easy and normal the jump trigger is lowered, once.
lower_jump_trigger_once( trigger )
{
	if ( isdefined( trigger.mw2coop_lowered ) )
		return;
	trigger.mw2coop_lowered = true;
	trigger.origin += ( 0, 0, -40 );
}

// self: player. The climb's arm state, also on the rig for the rig's notetracks.
note_arm_globals( globals, model )
{
	level.arm_ent_globals = globals;
	self.mw2coop_arm_globals = globals;
	model.mw2coop_player = self;
	model.mw2coop_arm_globals = globals;
}

// self: a rig model (climbing arms or the big jump arms). Its player.
rig_player()
{
	if ( isdefined( self.mw2coop_player ) )
		return self.mw2coop_player;
	return level.player;
}

rig_arm_globals()
{
	if ( isdefined( self.mw2coop_arm_globals ) )
		return self.mw2coop_arm_globals;
	return level.arm_ent_globals;
}

// --------------------------------------------------------------------------------------------
// Big jump: Soap catches one player at a time. Notetrack "start_gaz" of a player's big jump
// rig (self: the rig's anim node, rig: the rig). If Soap is still catching the other player,
// this rig holds still where the player hangs on until Soap is free.

catch_queued( rig )
{
	if ( !climb_coop() )
	{
		self maps\_climb::gaz_catches_player( rig );
		return;
	}
	player = rig rig_player();
	if ( isdefined( level.mw2coop_catching ) )
	{
		maps\_mw2coop::state( "climb: " + player_name( player ) + " hangs on, Soap is catching the other player" );
		animation = rig getanim( "big_jump" );
		rig SetFlaggedAnim( "single anim", animation, 1, 0, 0 );
		while ( isdefined( level.mw2coop_catching ) )
			wait 0.05;
		if ( !isdefined( rig ) )
			return;
		rig SetFlaggedAnim( "single anim", animation, 1, 0, 1 );
	}
	level.mw2coop_catching = player;
	maps\_mw2coop::state( "climb: Soap catches " + player_name( player ) );
	self maps\_climb::gaz_catches_player( rig );
}

// From gaz_catches_player() once Soap's catch animation is over.
catch_done()
{
	level.mw2coop_catching = undefined;
}

// The player Soap is catching (price_catches_player, slowmo_gaz).
caught_player()
{
	if ( isdefined( level.mw2coop_catching ) )
		return level.mw2coop_catching;
	return level.player;
}

// self: player, at the end of his big jump. The climb clears "can_save" when the player falls to
// his death and then gives up on him; in co-op that is this player's own fall (fade_to_death).
climb_survived()
{
	if ( !climb_coop() )
		return flag( "can_save" );
	return !self ent_flag( "fade_to_death" );
}

// climb_init() creates the dvar climb_startdir as "up" with SetDvarIfUninitialized, but in this
// build GetDvar returns something else when the climb starts (QA log), so the arm animations
// stay empty until the first stab: the getting-on-the-wall animation never played (script
// errors). In co-op any value the climb does not know means "up", as climb_init() meant;
// single player is left as it is.
climb_start_dir()
{
	dir = GetDvar( "climb_startdir" );
	if ( !climb_coop() || dir == "up" || dir == "left" || dir == "right" )
		return dir;
	if ( !isdefined( level.mw2coop_startdir_noted ) )
	{
		level.mw2coop_startdir_noted = true;
		maps\_mw2coop::state( "climb: climb_startdir is '" + dir + "', climbing up" );
	}
	return "up";
}
