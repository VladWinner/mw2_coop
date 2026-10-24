// mw2coop: checkpoints for the levels that run their sections themselves (Wolverines!: start_bmp
// threads handler_house_destroyer_to_pizza, which the section before calls when it ends). A start
// point is reached when a flag of the level is set: the last flag that start point's start
// function sets and the one before it does not, i.e. the end of the section before it, set in a
// normal run by a script or a map trigger. Found with tools/find_section_handlers.py and checked
// by hand; developers' test starts, generic helpers and flags that may come early are left out.
// Levels whose sections are add_start() section functions need no table (maps\_load patch), and
// the start points marked only by a section handler are hooked by tools/gen_checkpoint_patches.py.
// Each flag reached calls maps\_mw2coop::section_reached, which only moves forward.
#include common_scripts\utility;
#include maps\_utility;

// [ flag, start point ] pairs of a level.
table( level_name )
{
	t = [];
	switch ( level_name )
	{
		case "favela":
			t[ t.size ] = pair( "start_chase", "chase" );
			t[ t.size ] = pair( "favela_music", "favela" );
			break;
		case "arcadia":
			t[ t.size ] = pair( "used_laser", "checkpoint" );
			t[ t.size ] = pair( "golf_course_battle", "golf" );
			t[ t.size ] = pair( "disable_stryker_dialog", "crash" );
			break;
		case "oilrig":
			t[ t.size ] = pair( "player_breaks_surface", "surface" );
			t[ t.size ] = pair( "player_ready_to_be_helped_from_water", "rig" );
			t[ t.size ] = pair( "lower_room_breached", "deck1" );
			t[ t.size ] = pair( "player_approaching_deck_2", "deck2" );
			t[ t.size ] = pair( "obj_explosives_locate_given", "deck3" );
			break;
		case "favela_escape":
			t[ t.size ] = pair( "radiotower_escape_technical_2_arrival", "street" );
			t[ t.size ] = pair( "market_advance_4", "market_evac" );
			t[ t.size ] = pair( "solorun_objective_display", "solorun" );
			t[ t.size ] = pair( "solorun_player_at_balcony", "solorun_chopper" );
			break;
		case "dcburning":
			t[ t.size ] = pair( "obj_commerce_given", "elevator_bottom" );
			t[ t.size ] = pair( "player_at_commerce_crows_floor", "elevator_top" );
			t[ t.size ] = pair( "player_approaching_crowsnest2", "crows_nest" );
			t[ t.size ] = pair( "start_crow_armor_sequence", "crows_nest_armor" );
			t[ t.size ] = pair( "crowsnest_sequence_finished", "to_roof" );
			t[ t.size ] = pair( "player_approaching_outer_balcony", "roof" );
			t[ t.size ] = pair( "player_crash_done", "crash" );
			break;
		case "contingency":
			t[ t.size ] = pair( "start_btr_slide", "slide" );
			t[ t.size ] = pair( "safe_from_btrs", "midwoods" );
			t[ t.size ] = pair( "leaving_village", "base" );
			t[ t.size ] = pair( "price_splits_off", "defend_sub" );
			break;
		case "boneyard":
			t[ t.size ] = pair( "uaz_mounted", "ride_end" );
			break;
		case "af_caves":
			t[ t.size ] = pair( "rappel_threads", "rappel" );
			// both players are down the rope (_mw2coop_cave), not when the first starts the knife kill
			t[ t.size ] = pair( "mw2coop_cave_down", "barracks" );
			t[ t.size ] = pair( "steamroom_ambush_finish_dialogue_ended", "ledge" );
			t[ t.size ] = pair( "player_crossed_bridge", "overlook" );
			t[ t.size ] = pair( "player_right_near_breach", "control" );
			t[ t.size ] = pair( "obj_escape_complete", "airstrip" );
			break;
		case "estate":
			t[ t.size ] = pair( "print_first_objective", "ambush" );
			t[ t.size ] = pair( "player_is_out_of_ambush_zone", "house_approach" );
			t[ t.size ] = pair( "deploy_house_defense_jeeps", "house_breach" );
			t[ t.size ] = pair( "all_enemies_killed_up_to_house_capture", "house_briefing" );
			t[ t.size ] = pair( "ozone_to_earlydefense_start", "house_defense" );
			t[ t.size ] = pair( "dsm_recovered", "escape" );
			t[ t.size ] = pair( "finish_line", "ending" );
			break;
		case "dc_whitehouse":
			t[ t.size ] = pair( "whitehouse_entrance_clear", "oval_office" );
			t[ t.size ] = pair( "countdown_start", "flare" );
			break;
		case "invasion":
			t[ t.size ] = pair( "spawn_nates_attackers_in_alley", "pizza" );
			t[ t.size ] = pair( "leaving_gas_station", "crash" );
			t[ t.size ] = pair( "crash_objective", "nates_roof" );
			t[ t.size ] = pair( "sentry_in_position", "attack_diner" );
			t[ t.size ] = pair( "nates_bomb_incoming", "diner" );
			t[ t.size ] = pair( "taco_goes_to_roof", "vip_escort" );
			t[ t.size ] = pair( "president_in_BT_meat_locker", "defend_BT" );
			t[ t.size ] = pair( "first_attack_heli_spawned", "helis" );
			break;
	}
	return t;
}

pair( flag_name, start_name )
{
	p = [];
	p[ 0 ] = flag_name;
	p[ 1 ] = start_name;
	return p;
}

// From maps\_mw2coop::main() in a co-op campaign session.
watch_flags()
{
	foreach ( p in table( level.script ) )
		level thread watch_flag( p[ 0 ], p[ 1 ] );
}

watch_flag( flag_name, start_name )
{
	level endon( "mw2coop_level_end" );
	for ( t = 0; !isdefined( level.flag[ flag_name ] ); t += 0.5 )
	{
		if ( t == 30 )
			maps\_mw2coop::state( "checkpoints: flag " + flag_name + " (" + start_name + ") not in this level yet" );
		wait 0.5;
	}
	flag_wait( flag_name );
	// the player in the flag's trigger got there first (the other may be brought to him)
	leader = undefined;
	// GetEntArray( flag, "script_flag" ) finds nothing in this engine: pick by the field
	foreach ( trigger in array_combine( GetEntArray( "trigger_multiple_flag_set_touching", "classname" ), GetEntArray( "trigger_multiple_flag_set", "classname" ) ) )
		if ( isdefined( trigger.script_flag ) && trigger.script_flag == flag_name )
			foreach ( player in level.players )
				if ( player IsTouching( trigger ) )
					leader = player;
	maps\_mw2coop::section_reached( start_name, leader );
}
