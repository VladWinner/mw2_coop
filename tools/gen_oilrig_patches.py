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
p.note('''7. A restart at the "surface" checkpoint puts player 2 in the water beside player 1, as the
   swim would have (he stayed at the map's player start, 15000 units away).''')
p.subst('start_surface', 'level.player disableweapons();',
        'level.player disableweapons();\n\t' + M + 'partner_beside_at_surface( eNode );')

p.note('''8. The escape on the little bird: both players board it (maps\\_mw2coop_oilrig::board_all). Player 2 sits
   53 units behind player 1 on the bench, in the place of the hidden SEAL with starting position 3; the bird
   takes off when both are on board (a slow player is seated a minute after the other).''')
body = p.func('player_gets_on_littlebird')
old = body[body.index('\ttrigger waittill( "trigger" );\n'):body.rindex('}')]
p.replace(old, '\t' + M + 'board_all( self, trigger, player_seat, sPlayerRideTag );\n')
hide_loop = '\tforeach( guy in aRiders )\n\t{\n\t\tguy disable_ai_color();\n'
p.replace(hide_loop, '\t' + M + 'hide_partner_rider( aRiders );\n\n' + hide_loop)

p.note('''9. The allies' catch-up sprint (friendly_adjust_movement_speed / friendly_should_speed_up) runs only when the host cannot see the ally; with the host ahead and the client trailing the
   allies sprinted past the client's eyes (an owner's report). Now no player may see them (maps\\_mw2coop::any_player_sees).''')
p.subst('friendly_should_speed_up', 'within_fov( level.player.origin, level.player getPlayerAngles(), self.origin, level.cosine[ "60" ] )',
        'maps\\_mw2coop::any_player_sees( self, level.cosine[ "60" ] )')

p.note('''10. The prisoner walk-off before the C4 ambush: the two hostages in the upper room are hidden and posed only while no player
   looks at them (it waited for player 1's view alone: with him looking their way, and the client elsewhere, it never went on),
   and the friendly plants his C4 once no player stands in the blast zone (it looked at player 1 alone).''')
p.subst('hostage_manhandle_sequence',
        'while ( within_fov( level.player getEye(), level.player getPlayerAngles(), aHostagesForSequence[ 0 ].origin, level.cosine[ "45" ] ) )',
        'while ( maps\\_mw2coop::any_player_sees( aHostagesForSequence[ 0 ], level.cosine[ "45" ] ) )')
p.subst('friendly_plant_c4', 'while( level.player isTouching( safeVolume ) )', 'while( maps\\_mw2coop::any_player_touching( safeVolume ) )')

p.note('''11. The deck-2 helicopter's damage ramp (heli_ramp_up_damage, Recruit and Regular: a short invulnerability after each
   hit of its guns, for 20 or 40 hits) protects every player, not player 1 alone: called on the level (the campaign
   and Spec Ops' so_assault_oilrig do), it runs once for each player on him (Spec Ops audit 2026-11-17, item 1.5).''')
p.subst('heli_ramp_up_damage', '\tif ( level.gameskill > 1 )\n\t\treturn;\n',
        '\tif ( level.gameskill > 1 )\n\t\treturn;\n\tif ( !IsPlayer( self ) )\n\t{\n'
        '\t\tforeach ( mw2coop_player in level.players )\n\t\t\tmw2coop_player thread heli_ramp_up_damage( eHeli );\n'
        '\t\treturn;\n\t}\n')
p.subst('heli_ramp_up_damage', 'level.player', 'self')

p.note('''12. The railing smoker's alarm (railing_patroller_player_detect: a player in the trigger by MacTavish's firing spot, then 2 s while the
   smoker lives, then "stealth_broken" and the mission fails, "enemies alerted, hostages killed"): in single player MacTavish is at his spot
   first and shoots the smoker the moment the player comes; in co-op one player can run ahead while MacTavish still helps the other out of the
   water, and the 2 s ran out before he got there (tester's log: player 1 at the lower door, MacTavish short of his spot; QA oilrigwait: the
   flag at 6750, the alarm at 8750, the fail at 10000). In co-op the 2 s start once MacTavish stands at his spot (40 s at most); a real noise
   (player_broke_stealth) is not delayed.''')
p.subst('railing_patroller_player_detect', '\tflag_wait_either( "player_alerted_railing", "player_broke_stealth" );\n\twait( 2 );\n',
        '\tflag_wait_either( "player_alerted_railing", "player_broke_stealth" );\n\t' + M + 'railing_grace();\n\twait( 2 );\n')

p.write('oilrig.gsc.patch', '''
mw2coop patch for maps/oilrig.gsc (The Only Easy Day... Modern Warfare 2, 64-bit Steam build)
Generated by tools/gen_oilrig_patches.py from the game's own script; do not edit by hand.
Co-op logic lives in mw2coop/scripts/maps/_mw2coop_oilrig.gsc. Outside a co-op campaign session
every change does what the original did (the player is level.player there).
''')
