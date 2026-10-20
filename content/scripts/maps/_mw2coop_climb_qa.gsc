// mw2coop QA, Cliffhanger ice climb (scenario "climb", default start). maps\_mw2coop_qa starts it
// through level.mw2coop_qa_climb, set by maps\_mw2coop_climb: this file names maps\_climb, which
// exists only in Cliffhanger's zone, so no script loaded on every map may name this file.
#include common_scripts\utility;
#include maps\_utility;

// Cliffhanger ice climb (default start): a bot plays both players. Swings go through the climb's
// co-op input (self.mw2coop_qa_hold, read by maps\_mw2coop_climb::swing_pressed in place of the
// player's attack/aim buttons); walking and running are velocity pushes. Both players must get
// up the wall on their own lanes, make the big jump, be caught by Soap and reach the top.
climb()
{
	maps\_mw2coop_qa::check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	foreach ( player in level.players )
		player thread climb_bot();
	for ( t = 0; t < 420 && !flag( "reached_top" ); t += 5 )
	{
		maps\_mw2coop_qa::report( "t " + t + " can_save " + flag( "can_save" ) + " | p1 " + climb_text( level.player ) + " | p2 " + climb_text( level.player2 ) );
		wait 5;
	}
	foreach ( player in level.players )
		maps\_mw2coop_qa::check( player ent_flag_exist( "reached_top" ) && player ent_flag( "reached_top" ), maps\_mw2coop_climb::player_name( player ) + " reached the top" );
	foreach ( player in level.players )
		maps\_mw2coop_qa::check( player ent_flag_exist( "player_was_caught" ) && player ent_flag( "player_was_caught" ), "Soap caught " + maps\_mw2coop_climb::player_name( player ) );
	maps\_mw2coop_qa::check( flag( "reached_top" ), "the level goes on (reached_top)" );
	wait 3;
	d = Distance( level.player.origin, level.player2.origin );
	maps\_mw2coop_qa::check( d < 400, "both players together at the top (" + int( d ) + " units)" );
	maps\_mw2coop_qa::report( "done" );
}

climb_text( p )
{
	text = int( p.origin[ 0 ] ) + "," + int( p.origin[ 1 ] ) + "," + int( p.origin[ 2 ] ) + " linked " + p IsLinked() + " " + p.mw2coop_qa_step;
	foreach ( name in strtok( "player_in_position_to_climb player_starts_climbing climb_pullup player_preps_for_jump climb_big_jump player_makes_the_jump player_hangs_on player_was_caught final_climb reached_top", " " ) )
		if ( p ent_flag_exist( name ) && p ent_flag( name ) )
			text += " " + name;
	return text;
}

// self: player
climb_bot()
{
	self endon( "death" );
	self.mw2coop_qa_hold = [];
	self.mw2coop_qa_hold[ "left" ] = false;
	self.mw2coop_qa_hold[ "right" ] = false;
	// walk up to Soap, who starts climbing once a player is there (the bot cannot find the way
	// along the ledge: after 6 s the QA sets the trigger's flag itself)
	self.mw2coop_qa_step = "to soap";
	for ( t = 0; t < 6 && !flag( "price_begins_climbing" ) && isdefined( level.price ); t += 0.05 )
	{
		if ( Distance( self.origin, level.price.origin ) > 90 )
			self climb_bot_walk( level.price.origin, 100 );
		wait 0.05;
	}
	if ( !flag( "price_begins_climbing" ) && self == level.player )
	{
		maps\_mw2coop_qa::report( "QA sets price_begins_climbing (bot cannot walk the ledge)" );
		flag_set( "price_begins_climbing" );
	}
	self.mw2coop_qa_step = "wait wall";
	flag_wait( "player_gets_on_wall" );

	// walk to the own lane until in position, then face the wall and swing
	self.mw2coop_qa_step = "walk";
	yaw_check = GetEnt( "player_climb_yaw_check", "targetname" );
	wall = maps\_climb::get_forward_from_ent( yaw_check );
	for ( t = 0; t < 20 && !self ent_flag( "player_in_position_to_climb" ); t += 0.05 )
	{
		self climb_bot_walk( self.mw2coop_climb_start, 120 );
		wait 0.05;
	}
	maps\_mw2coop_qa::report( maps\_mw2coop_climb::player_name( self ) + " in position " + self ent_flag( "player_in_position_to_climb" ) );
	self SetPlayerAngles( VectorToAngles( wall ) );
	self.mw2coop_qa_step = "climb";
	self.mw2coop_qa_hold[ "left" ] = true;
	self climb_bot_stabs( "player_preps_for_jump" );

	// over the top of the wall: run and jump the gap
	self.mw2coop_qa_step = "run";
	if ( self == level.player )
	{
		foreach ( name in strtok( "climb_jump_prep climb_big_jump climb_icepick_slide ramp_block_notsolid", " " ) )
			foreach ( trigger in maps\_mw2coop_climb::flag_triggers( name ) )
				maps\_mw2coop_qa::report( "trigger " + name + " " + trigger.classname + " at " + trigger.origin );
		node = GetEnt( "climb_jump_org", "targetname" );
		maps\_mw2coop_qa::report( "climb_jump_org " + node.origin + " " + node.angles + ", me " + self.origin + " " + self GetPlayerAngles() );
	}
	self.mw2coop_qa_hold[ "left" ] = false;
	self.mw2coop_qa_hold[ "right" ] = false;
	yaw = GetEnt( "big_jump_yaw", "targetname" );
	targ = GetEnt( yaw.target, "targetname" );
	jump = VectorToAngles( targ.origin - yaw.origin );
	jump = ( 0, jump[ 1 ], 0 );
	forward = AnglesToForward( jump );
	maps\_mw2coop_qa::report( "big_jump_yaw " + yaw.origin + " -> " + targ.origin );
	self.mw2coop_qa_hold[ "left" ] = false;
	wait 1;
	maps\_mw2coop_qa::report( maps\_mw2coop_climb::player_name( self ) + " over the top at " + self.origin );
	// to the start of the run-up (big_jump_yaw), player 2 a little behind
	start = yaw.origin;
	if ( self != level.player )
	{
		wait 1.5;
		start -= forward * 40;
	}
	for ( t = 0; t < 8 && Distance2D( self.origin, start ) > 8; t += 0.05 )
	{
		self climb_bot_walk( start, 80 );
		wait 0.05;
	}
	maps\_mw2coop_qa::report( maps\_mw2coop_climb::player_name( self ) + " at the run-up " + self.origin );
	// the run-up is narrow: run along the big_jump_yaw line, steering back onto it
	for ( t = 0; t < 30 && !self ent_flag( "player_makes_the_jump" ); t += 0.05 )
	{
		if ( !self IsLinked() )
		{
			self SetPlayerAngles( jump );
			off = self.origin - yaw.origin;
			off = ( off[ 0 ], off[ 1 ], 0 );
			side = off - forward * VectorDot( off, forward );
			vel = self GetVelocity();
			self SetVelocity( forward * 200 - side * 4 + ( 0, 0, vel[ 2 ] ) );
		}
		wait 0.05;
	}
	maps\_mw2coop_qa::report( maps\_mw2coop_climb::player_name( self ) + " jumped " + self ent_flag( "player_makes_the_jump" ) );

	// hang on until Soap has him, then climb to the top
	self.mw2coop_qa_step = "hang";
	self.mw2coop_qa_hold[ "left" ] = true;
	self.mw2coop_qa_hold[ "right" ] = true;
	self ent_flag_wait( "final_climb" );
	self.mw2coop_qa_step = "final";
	self.mw2coop_qa_hold[ "right" ] = false;
	self climb_bot_stabs( "reached_top" );
	self.mw2coop_qa_step = "top";
	self.mw2coop_qa_hold[ "left" ] = false;
	self.mw2coop_qa_hold[ "right" ] = false;
}

// self: climbing player. Swing arm after arm (press the arm to swing while holding the other)
// until the flag is set.
climb_bot_stabs( until )
{
	while ( !self ent_flag( until ) )
	{
		g = self.mw2coop_arm_globals;
		if ( !isdefined( g ) || !self IsLinked() )
		{
			wait 0.05;
			continue;
		}
		arm = g.current_arm;
		other = maps\_climb::get_other_arm( arm );
		self.mw2coop_qa_hold[ other ] = true;
		self.mw2coop_qa_hold[ arm ] = false;
		wait 0.2;
		self.mw2coop_qa_hold[ arm ] = true;
		for ( t = 0; t < 3 && isdefined( self.mw2coop_arm_globals ) && self.mw2coop_arm_globals == g && g.current_arm == arm && !self ent_flag( until ); t += 0.05 )
			wait 0.05;
	}
}

// self: player. One frame of walking towards target (a velocity push, height kept); stops at an
// edge (no floor within 40 units below a step ahead).
climb_bot_walk( target, speed )
{
	to = target - self.origin;
	to = VectorNormalize( ( to[ 0 ], to[ 1 ], 0 ) );
	ahead = self.origin + to * 24;
	floor = PlayerPhysicsTrace( ahead + ( 0, 0, 24 ), ahead - ( 0, 0, 40 ) );
	vel = self GetVelocity();
	if ( floor[ 2 ] < self.origin[ 2 ] - 36 )
	{
		self SetVelocity( ( 0, 0, vel[ 2 ] ) );
		return false;
	}
	self SetVelocity( to * speed + ( 0, 0, vel[ 2 ] ) );
	return true;
}
