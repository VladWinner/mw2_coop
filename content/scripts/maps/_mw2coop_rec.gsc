// Session recorder: what both players do and where they are, for bug reports from real games.
// Each line goes to this machine's mw2coop\mw2coop.log as "rec: <ms> <text>" through
// setdvar mw2coop_rec, which the DLL writes at once (src/control.c). Both machines run it in
// lockstep, so the host's and the client's lines should match; a difference is a desync.
//   every second, each player: position, view, health, weapon, stance, ground, link, turret, down
//   at once: buttons (use, attack, aim, melee, frag, secondary offhand), weapon, stance, link,
//   turret, ground (jump, landing), mantle, damage (amount, attacker), down, death

#include common_scripts\utility;
#include maps\_utility;

start()
{
	level endon( "mw2coop_level_end" );
	while ( !isdefined( level.players ) || level.players.size < 2 )
		wait 0.05;
	rec( "level " + level.script + " start '" + GetDvar( "start" ) + "' players " + level.players.size );
	foreach ( n, player in level.players )
	{
		player thread watch( "p" + ( n + 1 ) );
		player thread damage( "p" + ( n + 1 ) );
	}
	thread allies();
	for ( ;; )
	{
		foreach ( n, player in level.players )
			if ( isdefined( player ) && IsAlive( player ) )
				rec( "p" + ( n + 1 ) + " " + snapshot( player ) );
		wait 1;
	}
}

// Every 5 seconds the friendly AI: name, class and position (a duplicated squad mate shows here).
allies()
{
	level endon( "mw2coop_level_end" );
	for ( ;; )
	{
		ai = GetAIArray( "allies" );
		text = "allies " + ai.size;
		foreach ( guy in ai )
		{
			name = guy.classname;
			if ( isdefined( guy.script_friendname ) )
				name = guy.script_friendname;
			else if ( isdefined( guy.animname ) )
				name = guy.animname;
			text = text + " | " + name + " at " + vec( guy.origin );
		}
		rec( text );
		wait 5;
	}
}

rec( text )
{
	SetDvar( "mw2coop_rec", GetTime() + " " + text );
}

vec( v )
{
	return int( v[ 0 ] ) + " " + int( v[ 1 ] ) + " " + int( v[ 2 ] );
}

// self: a player
snapshot( player )
{
	angles = player GetPlayerAngles();
	return "at " + vec( player.origin ) + " yaw " + int( angles[ 1 ] ) + " pitch " + int( angles[ 0 ] ) + " hp " + player.health
		+ " w " + player GetCurrentWeapon() + " " + player GetStance() + ground_text( player IsOnGround() ) + link_text( player IsLinked() )
		+ turret_text( player IsUsingTurret() ) + down_text( maps\_mw2coop::is_downed( player ) );
}

ground_text( on )
{
	if ( on )
		return "";
	return " air";
}

link_text( on )
{
	if ( on )
		return " linked";
	return "";
}

turret_text( on )
{
	if ( on )
		return " turret";
	return "";
}

down_text( on )
{
	if ( on )
		return " down";
	return "";
}

// self: a player. Changes, checked every frame.
watch( who )
{
	self endon( "death" );
	level endon( "mw2coop_level_end" );
	names = strtok( "use attack ads melee frag offhand", " " );
	was = [];
	foreach ( name in names )
		was[ name ] = false;
	weapon = self GetCurrentWeapon();
	stance = self GetStance();
	linked = self IsLinked();
	turret = self IsUsingTurret();
	ground = self IsOnGround();
	mantle = self IsMantling();
	down = maps\_mw2coop::is_downed( self );
	for ( ;; )
	{
		wait 0.05;
		now = [];
		now[ "use" ] = self UseButtonPressed();
		now[ "attack" ] = self AttackButtonPressed();
		now[ "ads" ] = self AdsButtonPressed();
		now[ "melee" ] = self MeleeButtonPressed();
		now[ "frag" ] = self FragButtonPressed();
		now[ "offhand" ] = self SecondaryOffhandButtonPressed();
		foreach ( name in names )
			if ( now[ name ] != was[ name ] )
			{
				was[ name ] = now[ name ];
				if ( now[ name ] )
					rec( who + " press " + name + " at " + vec( self.origin ) );
				else
					rec( who + " release " + name );
			}
		if ( self GetCurrentWeapon() != weapon )
		{
			weapon = self GetCurrentWeapon();
			rec( who + " weapon " + weapon );
		}
		if ( self GetStance() != stance )
		{
			stance = self GetStance();
			rec( who + " stance " + stance );
		}
		if ( self IsLinked() != linked )
		{
			linked = self IsLinked();
			if ( linked )
				rec( who + " linked at " + vec( self.origin ) );
			else
				rec( who + " unlinked at " + vec( self.origin ) );
		}
		if ( self IsUsingTurret() != turret )
		{
			turret = self IsUsingTurret();
			if ( turret )
				rec( who + " on turret" );
			else
				rec( who + " off turret" );
		}
		if ( self IsOnGround() != ground )
		{
			ground = self IsOnGround();
			if ( ground )
				rec( who + " landed at " + vec( self.origin ) );
			else
				rec( who + " left the ground at " + vec( self.origin ) );
		}
		if ( self IsMantling() != mantle )
		{
			mantle = self IsMantling();
			if ( mantle )
				rec( who + " mantle at " + vec( self.origin ) );
		}
		if ( maps\_mw2coop::is_downed( self ) != down )
		{
			down = maps\_mw2coop::is_downed( self );
			if ( down )
				rec( who + " down at " + vec( self.origin ) );
			else
				rec( who + " up at " + vec( self.origin ) );
		}
	}
}

// self: a player
damage( who )
{
	level endon( "mw2coop_level_end" );
	self thread death( who );
	self endon( "death" );
	for ( ;; )
	{
		self waittill( "damage", amount, attacker, direction, point, type );
		from = "world";
		if ( isdefined( attacker ) )
		{
			if ( IsPlayer( attacker ) )
				from = "partner";
			else if ( isdefined( attacker.classname ) )
				from = attacker.classname;
		}
		if ( !isdefined( type ) )
			type = "?";
		rec( who + " damage " + amount + " " + type + " from " + from + " hp " + self.health );
	}
}

death( who )
{
	level endon( "mw2coop_level_end" );
	self waittill( "death" );
	rec( who + " death at " + vec( self.origin ) );
}
