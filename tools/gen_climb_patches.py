"""Write the Cliffhanger ice climb co-op patches (maps/_climb.gsc, maps/_climb_anim.gsc), taking
every replaced text from the extracted game scripts so it matches byte for byte. Most of the work
is `@@@ subst`: in each function of the climb, level.player becomes the climbing player and the
climb's flags become that player's own (maps\\_mw2coop_climb::pflag*).
Usage: python -I gen_climb_patches.py <extracted gsc root> <mod content/patches/maps dir>"""
import os
import re
import sys

gsc_root, out_dir = sys.argv[1], sys.argv[2]
M = 'maps\\_mw2coop_climb::'

# flags of the climb that become per player (see climb_setup() in _mw2coop_climb.gsc)
PLAYER_FLAGS = set('''player_in_position_to_climb climb_start climb_pullup climb_drop_down
player_climbs_past_safe_point climb_jump_prep climb_big_jump climb_icepick_slide
we_care_about_right_icepick finished_climbing final_climb player_preps_for_jump
player_makes_the_jump player_hangs_on player_was_caught price_caught_player climbing_dof
fade_to_death reached_top player_starts_climbing'''.split())


def load(rel):
    return open(os.path.join(gsc_root, rel), encoding='latin-1').read().replace('\r', '')


def func(src, name):
    i = src.index('\n' + name + '(') + 1
    j = src.index('\n}\n', i) + 3
    return src[i:j]


class Patch:
    def __init__(self, rel):
        self.src = load(rel)
        self.out = []

    def note(self, text):
        self.out.append('\n' + text.strip('\n') + '\n')

    def subst(self, fn, old, new):
        """Whole-word replace inside one function, as the DLL does; applied here too, so later
        hunks match the text the earlier ones left."""
        body = func(self.src, fn)
        lead = r'(?<![A-Za-z0-9_])' if re.match(r'\w', old[0]) else ''
        trail = r'(?![A-Za-z0-9_])' if re.match(r'\w', old[-1]) else ''
        body2, n = re.subn(lead + re.escape(old) + trail, lambda m: new, body)
        assert n, (fn, old)
        self.src = self.src.replace(body, body2, 1)
        self.out.append('@@@ subst ' + fn + '\n' + old + '\n@@@ to\n' + new + '\n@@@ end\n')

    def replace(self, old, new):
        assert self.src.count(old) == 1, (self.src.count(old), old[:80])
        self.src = self.src.replace(old, new)
        self.out.append('@@@ replace\n' + old + '@@@ with\n' + new + '@@@ end\n')

    def per_player(self, fn, who):
        """level.player -> who, per-player flags -> who's, in function fn."""
        body = func(self.src, fn)
        if re.search(r'level\.player(?![A-Za-z0-9_])', body):
            self.subst(fn, 'level.player', who)
        for call, helper in (('flag', 'pflag'), ('flag_set', 'pflag_set'), ('flag_clear', 'pflag_clear'),
                             ('flag_wait', 'pflag_wait'), ('flag_waitopen', 'pflag_waitopen')):
            for name in sorted(set(re.findall(r'(?<![A-Za-z0-9_])' + call + r'\( "([a-z0-9_]+)" \)', body))):
                if name in PLAYER_FLAGS:
                    self.subst(fn, f'{call}( "{name}" )', f'{who} {M}{helper}( "{name}" )')

    def write(self, name, header):
        path = os.path.join(out_dir, name)
        open(path, 'w', encoding='utf-8', newline='\n').write(header.strip('\n') + '\n' + ''.join(self.out))
        print('wrote', path, sum(1 for x in self.out if x.startswith('@@@')), 'hunks')


# ---------------------------------------------------------------- common/maps/_climb.gsc
p = Patch('common/maps/_climb.gsc')

p.note('''1. Setup: per-player flags fed by the climb's trigger volumes, and the slide off the cliff
   per player (was: one thread on level.player).''')
p.subst('climb_init', 'thread player_slides_off_cliff();', M + 'climb_setup();')

p.note('''2. The swing buttons: the player's own usercmd in co-op (ButtonPressed reads this PC's keys).''')
p.subst('rightSwingPressed', 'return level.player buttonpressed( "mouse2" );', 'return self ' + M + 'swing_pressed( "right", "mouse2" );')
p.subst('leftSwingPressed', 'return level.player buttonpressed( "mouse1" );', 'return self ' + M + 'swing_pressed( "left", "mouse1" );')
p.subst('icepick_button_pressed', 'ent.player ButtonPressed( ent.key )', M + 'key_pressed( ent )')
p.subst('icepick_button_pressed_instant', 'ent.player ButtonPressed( ent.key )', M + 'key_pressed( ent )')

p.note('''3. Every function of the climb works on the climbing player (self, ent.player or the rig's
   player) and on his own flags.''')
for fn in ('death_trigger', 'teleport_to_cave', 'climb_wall', 'wait_until_player_climbs',
           'player_finishes_climbing', 'fall_to_your_death', 'arm_completes_stab', 'free_ground_ref',
           'track_trigger_pulls', 'player_leaps', 'player_slides_off_cliff', 'player_big_jump',
           'track_player_button_presses_for_holding_on', 'stop_hanging_arm_hint',
           'should_stop_how_to_climb_hint'):
    p.per_player(fn, 'self')
for fn in ('player_falls_to_death', 'player_relinks_to_tag', 'arm_stabs', 'player_got_off_cliff',
           'reached_drop_down_spot', 'climbing_cracks_fx'):
    p.per_player(fn, 'ent.player')
p.per_player('icepick_impact_fx', 'self.player')
for fn in ('arms_animated_relative_to_input', 'slide_rumble'):
    p.per_player(fn, 'self.mw2coop_player')

p.note('''4. Climb start: the second climb thread finds the climb blocker already deleted; each player
   remembers his lane.''')
p.subst('climb_wall', 'player_climb_blocker Delete();',
        'if ( isdefined( player_climb_blocker ) )\n\t\tplayer_climb_blocker Delete();')
p.subst('climb_wall', 'MusicPlayWrapper( "cliffhanger_climbing_music" );',
        'self ' + M + 'note_lane( start_org );\n\tMusicPlayWrapper( "cliffhanger_climbing_music" );')

p.note('''5. The cave: both players at the foot of the wall, player 2 beside player 1 on his lane's side;
   the white fade-in once.''')
p.subst('teleport_to_cave', 'self SetOrigin( climb_cam.origin + ( 0, 0, -12 ) );',
        'self SetOrigin( climb_cam.origin + ( 0, 0, -12 ) + self ' + M + 'cave_offset() );')
p.subst('teleport_to_cave', 'thread maps\\_introscreen::introscreen_generic_white_fade_in( 1.25, 2 );',
        'if ( self == level.player )\n\t\tthread maps\\_introscreen::introscreen_generic_white_fade_in( 1.25, 2 );')
p.subst('cliff_scene_with_price', 'thread teleport_to_cave();', M + 'teleport_to_cave();')
p.subst('cliff_scene_with_price', 'thread death_trigger();', M + 'death_triggers();')
p.subst('cliff_scene_with_price', 'level.player thread play_loop_sound_on_entity( "gear_jacket_flapping_plr_loop" );',
        'foreach ( p in level.players )\n\t\tp thread play_loop_sound_on_entity( "gear_jacket_flapping_plr_loop" );')
for call in ('::SetStance, "stand"', '::allowcrouch, false', '::allowprone, false'):
    p.subst('cliff_scene_with_price', f'level.player delayCall( delay_time, {call} );',
            f'foreach ( p in level.players )\n\t\tp delayCall( delay_time, {call} );')
body = func(p.src, 'give_player_icepicker_ammo')
old = body[body.index('\t\tif ( player_has_weapon'):body.index('\t\twait( 1 );')].rstrip('\n')
p.subst('give_player_icepicker_ammo', old.lstrip('\t'), M + 'give_icepick_ammo();')

p.note('''6. Arm state per player (also on the rig, for its notetracks and the hints); the climbing
   direction is "up" when the climb_startdir dvar reads something else (it does in this build,
   which left the arm animations empty until the first stab).''')
p.subst('player_finishes_climbing', 'level.arm_ent_globals = arm_globals;', 'self ' + M + 'note_arm_globals( arm_globals, model );')
p.subst('player_finishes_climbing', 'level.stabs_completed = 0;', 'self.stabs_completed = 0;')
p.subst('player_finishes_climbing', 'start_dir = GetDvar( "climb_startdir" );', 'start_dir = ' + M + 'climb_start_dir();')
p.subst('arm_stabs', 'level.stabs_completed', 'ent.player.stabs_completed')
p.subst('stop_hanging_arm_hint', 'level.arm_ent_globals', 'self.mw2coop_arm_globals')

p.note('''7. The single spots at the top (pull-up, getting up, dropping down, recovering from a fall)
   on the player's lane.''')
for fn, name, kind, side in (('reached_drop_down_spot', 'player_icepicker_bigjump_end_getup', 'getstruct', 'far'),
                             ('reached_drop_down_spot', 'climb_jump_org', 'GetEnt', 'near'),
                             ('reached_drop_down_spot', 'jump_down_org', 'GetEnt', 'near'),
                             ('reached_drop_down_spot', 'climb_get_on', 'GetEnt', 'near'),
                             ('player_falls_to_death', 'player_recover', 'GetEnt', 'near')):
    helper = 'lane_struct' if kind == 'getstruct' else 'lane_ent'
    p.subst(fn, f'{kind}( "{name}", "targetname" )', f'{M}{helper}( "{name}", ent.player, "{side}" )')

p.note('''8. Big jump per player: Soap jumps once, the jump trigger is lowered once, the jumpdown block
   may be gone, the rig and its anim node are the player's, the final climb starts on his lane.''')
p.subst('player_big_jump', 'player_jumpdown_block Solid();',
        'if ( isdefined( player_jumpdown_block ) )\n\t\tplayer_jumpdown_block Solid();')
p.subst('player_big_jump', 'player_jumpdown_block Delete();',
        'if ( isdefined( player_jumpdown_block ) )\n\t\tplayer_jumpdown_block Delete();')
p.subst('player_big_jump', 'thread price_makes_the_jump( climb_jump_org );', M + 'price_jump_once();')
p.subst('player_big_jump', 'GetEnt( "climb_jump_org", "targetname" )', M + 'lane_ent( "climb_jump_org", self, "far" )')
p.subst('player_big_jump', 'trigger.origin += ( 0, 0, -40 );', M + 'lower_jump_trigger_once( trigger );')
p.subst('player_big_jump', 'level notify( "stop_force_sliding_the_player" );', 'self notify( "stop_force_sliding_the_player" );')
p.subst('player_slides_off_cliff', 'level endon( "stop_force_sliding_the_player" );', 'self endon( "stop_force_sliding_the_player" );')
p.subst('player_big_jump', 'level.playeR_arms = player_arms;', 'level.playeR_arms = player_arms;\n\tplayer_arms.mw2coop_player = self;')
p.subst('player_big_jump', 'GetEnt( "climb_catch", "targetname" )', M + 'lane_ent( "climb_catch", self, "far" )')
p.subst('player_big_jump', 'climb_catch Delete();', M + 'keep_or_delete( climb_catch );')
p.subst('player_big_jump', 'level.rumble_ent', 'self.rumble_ent')
p.subst('player_big_jump', 'if ( !flag( "can_save" ) )', 'if ( !self ' + M + 'climb_survived() )')

p.note('''9. Soap catches one player at a time: the catch is over when his animation is.''')
p.subst('gaz_catches_player', 'self anim_single_solo( level.climbing_gaz, "climb_catch" );',
        'self anim_single_solo( level.climbing_gaz, "climb_catch" );\n\t' + M + 'catch_done();')
p.subst('slowmo_gaz', 'wait( 0.5 );', 'caught = ' + M + 'caught_player();\n\twait( 0.5 );')
p.subst('slowmo_gaz', 'flag( "player_hangs_on" )', 'caught ' + M + 'pflag( "player_hangs_on" )')
p.subst('slowmo_gaz', 'level.player', 'caught')
p.subst('slowmo_gaz', 'flag_set( "price_caught_player" )', 'caught ' + M + 'pflag_set( "price_caught_player" )')

p.write('_climb.gsc.patch', '''
mw2coop patch for maps/_climb.gsc (Cliffhanger ice climb, Modern Warfare 2, 64-bit Steam build)
Generated by tools/gen_climb_patches.py from the game's own scripts; do not edit by hand.
Co-op logic lives in mw2coop/scripts/maps/_mw2coop_climb.gsc. Outside a co-op campaign session
every change does what the original did (the climbing player is level.player there).
''')

# ---------------------------------------------------------------- cliffhanger/maps/cliffhanger.gsc
c = Patch('cliffhanger/maps/cliffhanger.gsc')
c.note('''1. The map has one climb start ("player_climb"): the second player gets a lane beside it.''')
c.subst('cave_main', 'climb_starts[ index ].origin', M + 'climb_start( climb_starts, index ).origin')
c.subst('cave_main', 'climb_starts[ index ].angles', M + 'climb_start( climb_starts, index ).angles')
c.write('cliffhanger.gsc.patch', '''
mw2coop patch for maps/cliffhanger.gsc (Cliffhanger, Modern Warfare 2, 64-bit Steam build)
Generated by tools/gen_climb_patches.py; do not edit by hand.
Co-op logic lives in mw2coop/scripts/maps/_mw2coop_climb.gsc.
''')

# ---------------------------------------------------------------- cliffhanger/maps/_climb_anim.gsc
a = Patch('cliffhanger/maps/_climb_anim.gsc')
a.note('''1. Soap's catch goes through the co-op queue (one player at a time).''')
a.subst('player_animations', 'maps\\_climb::gaz_catches_player', M + 'catch_queued')
a.note('''2. The catch, rumble and stab effects are for the player whose rig it is.''')
a.subst('price_catches_player', 'if ( isdefined( level.rumble_ent ) )',
        'caught = ' + M + 'caught_player();\n\tif ( isdefined( caught.rumble_ent ) )')
a.subst('price_catches_player', 'level.rumble_ent delete();', 'caught.rumble_ent delete();')
a.subst('price_catches_player', 'level.player', 'caught')
a.subst('price_catches_player', 'flag_set( "player_was_caught" )', 'caught ' + M + 'pflag_set( "player_was_caught" )')
a.subst('right_stab_fx', 'level.player PlayRumbleOnEntity( "icepick_climb" );',
        'player = player_model ' + M + 'rig_player();\n\tplayer PlayRumbleOnEntity( "icepick_climb" );')
a.subst('stab_fx', 'level.player PlayRumbleOnEntity( "icepick_climb" );',
        'player = arm_model ' + M + 'rig_player();\n\tplayer PlayRumbleOnEntity( "icepick_climb" );')
a.subst('stab_fx_current', 'arm = level.arm_ent_globals.current_arm;',
        'globals = arm_model ' + M + 'rig_arm_globals();\n\tarm = globals.current_arm;')
a.write('_climb_anim.gsc.patch', '''
mw2coop patch for maps/_climb_anim.gsc (Cliffhanger ice climb animations, Modern Warfare 2,
64-bit Steam build). Generated by tools/gen_climb_patches.py; do not edit by hand.
Co-op logic lives in mw2coop/scripts/maps/_mw2coop_climb.gsc.
''')
