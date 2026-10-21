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
