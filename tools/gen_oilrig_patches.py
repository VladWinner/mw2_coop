"""Write the oilrig (The Only Easy Day...) co-op patch, taking every replaced text from the
extracted game script so it matches byte for byte.
Usage: python -I gen_oilrig_patches.py <extracted gsc root> <mod content/patches/maps dir>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gsc_patch import Patch  # noqa: E402
from seek_patch import both_fields, both_ignoreme  # noqa: E402

gsc_root, out_dir = sys.argv[1], sys.argv[2]
M = 'maps\\_mw2coop_oilrig::'

p = Patch(gsc_root, 'oilrig/maps/oilrig.gsc', out_dir)

p.note('''1. Underwater ride: player 2 rides SDV B in the place of the SEAL sdv02_swimmer1 (left out in
   co-op), with the player's ride-and-swim animation; player 1's rig alone drives the swim flags.''')
p.subst('underwater_friendly_setup', 'level.sdvSquad02[ 3 ].animname = "sdv02_swimmer2";',
        'level.sdvSquad02[ 3 ].animname = "sdv02_swimmer2";\n\t' + M + 'leave_out_swimmer();')
p.subst('underwater_sequence', 'level.player thread player_sdv_think( eSDV_01, eNodeIntro );',
        M + 'players_ride( eSDV_01, eSDV_02, eNodeIntro );')
p.subst('player_sdv_think', 'level.player', 'self')
p.subst('player_sdv_think', 'ePlayer_rig thread underwater_player_flags();', 'self ' + M + 'rig_flags( ePlayer_rig );')

p.note('''2. Surface: every player bobs and swims by the grate until his own turn to be pulled out.''')
p.subst('player_breaks_surface', 'thread water_bob();', M + 'players_bob();')
p.subst('water_bob', 'level.player', 'self')
for line in ('org_view_bob.origin = self.origin + ( 0, 0, 10 );',
             'waterMoveModel.origin = self.origin + ( 0, 0, zOffset);',
             'org_view_bob.origin = self.origin + ( 0, 0, zOffset);'):
    p.subst('water_bob', line, line.replace('self.origin', 'self ' + M + 'bob_base()'))
p.subst('water_bob', 'waterMoveModel thread water_movement( org_view_bob );',
        'waterMoveModel.mw2coop_player = self;\n\twaterMoveModel thread water_movement( org_view_bob );')
p.subst('water_movement', 'level.player', 'self.mw2coop_player')
p.subst('water_movement', 'while( !flag( "player_ready_to_be_helped_from_water" ) )',
        'while( !self.mw2coop_player ' + M + 'ready_to_be_helped() )')
p.subst('open_up_player_fov', 'level.player', 'self')
p.subst('water_streaks', 'level.player SetWaterSheeting( 1, 3.0 );',
        'foreach ( p in level.players )\n\t\tp SetWaterSheeting( 1, 3.0 );')

p.note('''3. Stealth kill: every player waits by the grate; the first to strike (looking at the guard,
   melee) does the kill, the other is pulled out of the water after him. The hint shows when
   any player looks at the guard. The exit point is raised once.''')
p.subst('AA_surface_init', 'level.player thread player_grate_sequence_think();',
        'foreach ( p in level.players )\n\t\tp thread player_grate_sequence_think();')
p.subst('player_grate_sequence_think', 'org_water_exit.origin = org_water_exit.origin + ( 0, 0, 10 );',
        'if ( !isdefined( org_water_exit.mw2coop_raised ) )\n\t\torg_water_exit.origin = org_water_exit.origin + ( 0, 0, 10 );\n\torg_water_exit.mw2coop_raised = true;')
p.subst('player_grate_sequence_think', 'level.player', 'self')
p.subst('player_grate_sequence_think', 'thread grate_enemy_hint();',
        'if ( self == level.player )\n\t\tthread grate_enemy_hint();')
p.subst('player_grate_sequence_think', 'thread player_looking_at_grate_guard_logic();',
        'if ( self == level.player )\n\t\tthread player_looking_at_grate_guard_logic();')
p.subst('player_grate_sequence_think', 'waittill_player_triggers_stealth_kill();',
        'if ( self ' + M + 'stealth_kill_turn( ePlayer_rig, eNodeIntro, org_water_exit ) )\n\t\treturn;')
p.subst('player_looking_at_grate_guard_logic',
        'bInFOV = within_fov( level.player geteye(), level.player getPlayerAngles(), org.origin, level.cosine[ "25" ] );',
        'bInFOV = ' + M + 'any_player_looking( org );')

p.note('''4. In and out of the water after the kill: the player who did it (the other one is set up when
   he is pulled out); the ocean does not kill the player still waiting by the grate.''')
p.subst('player_in_water_monitor', 'level.player', 'self')
p.subst('player_out_of_water_monitor', 'level.player', 'self')
p.subst('killtrigger_ocean_on', 'if ( ( isdefined( other ) ) && ( isPlayer ( other ) ) )',
        'if ( ( isdefined( other ) ) && ( isPlayer ( other ) ) && ' + M + 'ocean_can_kill( other ) )')
p.note('''5. The rappel and the squad's quiet moments hide both players from enemies, not only player 1
   (maps\\_mw2coop::players_ignoreme): the partner is in the same scene.''')
both_ignoreme(p, ['rappel_ignoreme', 'ignoreme_on_squad_and_player', 'ignoreme_off_squad_and_player'])
p.note('''6. How much enemies want "the player" (threatbias) is set on both players
   (maps\_mw2coop::players_threatbias): the partner is in the same scene.''')
assert both_fields(p)

p.write('oilrig.gsc.patch', '''
mw2coop patch for maps/oilrig.gsc (The Only Easy Day... Modern Warfare 2, 64-bit Steam build)
Generated by tools/gen_oilrig_patches.py from the game's own script; do not edit by hand.
Co-op logic lives in mw2coop/scripts/maps/_mw2coop_oilrig.gsc. Outside a co-op campaign session
every change does what the original did (the player is level.player there).
''')
