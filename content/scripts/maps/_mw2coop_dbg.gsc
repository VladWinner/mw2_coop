// mw2coop: debug tools for the developer console (src/control.c): dbg_ents [filter], dbg_players, dbg_flags [filter], dbg_ai [filter].
// The DLL puts a request "<counter> <command> <filter>" into the dvar mw2coop_dbg_req; this thread answers with numbered lines
// "<n>|<text>" in mw2coop_dbg_out, which the DLL prints into the console (one line every 0.26 s: it polls every 250 ms). It only
// reads the level, so it can run in any session; in a lockstep co-op session ask on one machine only.
// (The idea comes from H2-Mod's entity list and script console.)
#include common_scripts\utility;
#include maps\_utility;

start()
{
	level.mw2coop_dbg_n = 0;
	last = "";
	for ( ;; )
	{
		wait 0.25;
		request = GetDvar( "mw2coop_dbg_req" );
		if ( request == "" || request == last )
			continue;
		last = request;
		parts = strtok( request, " " );
		if ( parts.size < 2 )
			continue;
		filter = "";
		if ( parts.size > 2 )
			filter = ToLower( parts[ 2 ] );
		switch ( parts[ 1 ] )
		{
			case "ents":
				list_ents( filter );
				break;
			case "players":
				list_players();
				break;
			case "flags":
				list_flags( filter );
				break;
			case "ai":
				list_ai( filter );
				break;
		}
	}
}

out( text )
{
	level.mw2coop_dbg_n++;
	SetDvar( "mw2coop_dbg_out", level.mw2coop_dbg_n + "|" + text );
	wait 0.26;
}

text_of( value )
{
	if ( !isdefined( value ) )
		return "-";
	return value + "";
}

at( ent )
{
	if ( !isdefined( ent.origin ) )
		return "";
	return int( ent.origin[ 0 ] ) + " " + int( ent.origin[ 1 ] ) + " " + int( ent.origin[ 2 ] );
}

matches( filter, text )
{
	return filter == "" || IsSubStr( ToLower( text ), filter );
}

report_list( title, lines, filter )
{
	shown = 0;
	for ( i = 0; i < lines.size && shown < 40; i++ )
	{
		out( lines[ i ] );
		shown++;
	}
	if ( lines.size > shown )
		out( title + ": " + ( lines.size - shown ) + " more, narrow the filter" );
	suffix = "";
	if ( filter != "" )
		suffix = " '" + filter + "'";
	out( title + ": " + lines.size + " matching" + suffix );
}

list_ents( filter )
{
	lines = [];
	foreach ( ent in GetEntArray() )
	{
		line = text_of( ent.classname ) + " " + text_of( ent.targetname ) + " " + text_of( ent.model ) + " at " + at( ent );
		if ( matches( filter, line ) )
			lines[ lines.size ] = line;
	}
	report_list( "entities", lines, filter );
}

list_ai( filter )
{
	lines = [];
	foreach ( ai in GetAIArray() )
	{
		line = text_of( ai.team ) + " " + text_of( ai.classname ) + " " + text_of( ai.targetname ) + " hp " + text_of( ai.health ) + " at " + at( ai );
		if ( matches( filter, line ) )
			lines[ lines.size ] = line;
	}
	report_list( "ai", lines, filter );
}

list_players()
{
	players = level.players;
	if ( !isdefined( players ) )
	{
		players = [];
		players[ 0 ] = level.player;
	}
	for ( i = 0; i < players.size; i++ )
	{
		p = players[ i ];
		angles = p GetPlayerAngles();
		stance = p GetStance();
		weapon = p GetCurrentWeapon();
		linked = p IsLinked();
		out( "player " + ( i + 1 ) + ": alive " + IsAlive( p ) + " hp " + p.health + "/" + p.maxhealth + " at " + at( p ) + " yaw " + int( angles[ 1 ] ) + " stance " + stance + " weapon " + weapon + " linked " + linked );
	}
	out( "players: " + players.size );
}

list_flags( filter )
{
	lines = [];
	foreach ( name, _ in level.flag )
	{
		if ( flag( name ) && matches( filter, name ) )
			lines[ lines.size ] = name;
	}
	report_list( "flags set", lines, filter );
}
