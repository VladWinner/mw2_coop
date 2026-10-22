// Third-person bodies for the players in each campaign level: the Spec Ops co-op models
// (coop_body_* / coop_head_*), the only ones the player animations fit. They come from the Spec
// Ops zone the DLL loads with the level in a co-op session (src/zones.c); the uniform matches
// the level. Returns [ body, head ], or undefined for a level the DLL loads no zone for.
bodies( level_name )
{
	switch ( level_name )
	{
		case "trainer":
		case "roadkill":
		case "invasion":
		case "arcadia":
		case "airport":
		case "dcburning":
		case "dcemp":
		case "dc_whitehouse":
			return models( "coop_body_us_army", "coop_head_us_army" );
		case "cliffhanger":
		case "contingency":
			return models( "coop_body_tf141_arctic", "coop_head_tf141_arctic" );
		case "oilrig":
		case "gulag":
			return models( "coop_body_seal_udt", "coop_head_seal_udt" );
		case "estate":
			return models( "coop_body_tf141_forest", "coop_head_tf141_forest" );
		case "favela":
		case "favela_escape":
		case "boneyard":
		case "af_caves":
		case "af_chase":
		case "ending":
			return models( "coop_body_tf141_desert", "coop_head_tf141_desert" );
		default:
			return undefined;
	}
}

// Player n's (0, 1) body and head. No Russian dresses both as Makarov's men, as player 1 is in the
// story (owner, 2026-10-22: the US Army uniform of Spec Ops looked wrong among them). The crew's
// own body (body_airport_com_*) is an AI model and breaks the player pose, so the players keep the
// Spec Ops body and the DLL puts the crew's mesh on it (transplant_request(), src/transplant.c);
// the heads are the crew's own, in pairs none of the crew wears (character_airport_*: com_c/a,
// com_b/d, com_d/b, com_b/b; com_a is Makarov's).
player_bodies( level_name, n )
{
	if ( level_name == "airport" )
	{
		if ( n == 0 )
			return models( "coop_body_us_army", "head_airport_d" );
		return models( "coop_body_us_army", "head_airport_a" );
	}
	return bodies( level_name );
}

// The dvar mw2coop_transplant for the level: "<map> <target body> <donor body>", or "" (the DLL
// does nothing). Set at every level start, so a level without one clears the last.
transplant_request( level_name )
{
	if ( level_name == "airport" )
		return "airport coop_body_us_army body_airport_com_c";
	return "";
}

models( body, head )
{
	m = [];
	m[ 0 ] = body;
	m[ 1 ] = head;
	return m;
}

#using_animtree( "multiplayer" );

// Give a player his body as Spec Ops does (maps\_loadout::so_player_give_loadout): the model
// function, then the "code" node of the multiplayer animation tree, which lets the engine drive
// the third-person animations. Without the second step the body stays in its bind pose.
give_body( model_func )
{
	self maps\_utility::SetModelFunc( model_func );
	self SetAnim( %code, 1, 0 );
}
