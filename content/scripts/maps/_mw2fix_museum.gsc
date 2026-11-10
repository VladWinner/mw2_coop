// Fixes of the game's own bugs in the Museum (maps/char_museum_code.gsc.fix.patch). Loaded in every session, single player too:
// nothing here may use maps\_mw2coop*.

// char_museum_code::civ_talkers(): the civilian loops its idle. The entries of the museum's own animation script are
// single animations; anim_weight() needs an array of looping animations, so the entry is made one first.
civ_loop( node, civ )
{
	name = node.animation;
	// credits_1 (from the knife fight): the museum's animation tables are incomplete when the civilians
	// come (26 entries of "generic", none of civilian_stand_loops()); define them once more
	if ( isdefined( name ) && !isdefined( level.scr_anim[ "generic" ][ name ] ) && !isdefined( level.mw2fix_museum_anim_again ) )
	{
		level.mw2fix_museum_anim_again = true;
		println( "museum: animation table has " + level.scr_anim[ "generic" ].size + " entries, running char_museum_anim::main again" );
		maps\char_museum_anim::main();
		println( "museum: now " + level.scr_anim[ "generic" ].size + " entries" );
	}
	if ( !isdefined( name ) || !isdefined( level.scr_anim[ "generic" ][ name ] ) )
	{
		if ( !isdefined( level.mw2fix_museum_civ_logged ) )
			level.mw2fix_museum_civ_logged = 0;
		if ( level.mw2fix_museum_civ_logged < 8 )
		{
			level.mw2fix_museum_civ_logged++;
			generic = level.scr_anim[ "generic" ];
			println( "museum: a civilian has no animation entry for '" + name + "' (generic has " + generic.size + " entries; stand_idle " + isdefined( generic[ "civilian_stand_idle" ] ) + " smoking_A " + isdefined( generic[ "civilian_smoking_A" ] ) + " directions_1_A " + isdefined( generic[ "civilian_directions_1_A" ] ) + " laptop_stand_idle " + isdefined( generic[ "laptop_stand_idle" ] ) + " sitting_talking_A_1 " + isdefined( generic[ "civilian_sitting_talking_A_1" ] ) + ")" );
		}
		return;
	}
	if ( !IsArray( level.scr_anim[ "generic" ][ name ] ) )
	{
		single = level.scr_anim[ "generic" ][ name ];
		level.scr_anim[ "generic" ][ name ] = [];
		level.scr_anim[ "generic" ][ name ][ 0 ] = single;
		println( "museum: the idle '" + name + "' made an array of looping animations" );
	}
	node maps\_anim::anim_generic_loop( civ, name );
}
