"""Write the patches of level scripts whose only co-op changes are about enemies: those sent at
"the player" go for the closest player still up, and scenes that hide "the player" from enemies
hide both (tools/seek_patch.py). Scripts that have a generator of their
own (airport, arcadia, af_caves, cliffhanger, dcburning, dc_whitehouse, estate, favela_escape, roadkill) call
seek_patch from it instead.
Usage: python -I gen_seek_patches.py <extracted gsc root> <mod content/patches/maps dir>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gsc_patch import Patch  # noqa: E402
from seek_patch import enemies_for_both  # noqa: E402

gsc_root, out_dir = sys.argv[1], sys.argv[2]

# script, level, functions whose scene hides "the player" from enemies (level.player.ignoreme)
SCRIPTS = [
    ('af_caves/maps/af_caves_backhalf.gsc', 'Just Like Old Times', ['AAA_sequence_airstrip']),
    ('boneyard/maps/boneyard.gsc', 'The Enemy of My Enemy', ['intro_player']),
    ('cliffhanger/maps/cliffhanger_code.gsc', 'Cliffhanger', []),
    ('cliffhanger/maps/cliffhanger_stealth.gsc', 'Cliffhanger', []),
    ('contingency/maps/contingency.gsc', 'Contingency', []),
    ('dcemp/maps/dcemp_endpart.gsc', 'Second Sun', ['whitehouse_main']),
    ('ending/maps/char_museum_code.gsc', 'Museum', ['panic_button']),
    ('favela/maps/favela_code.gsc', 'Takedown', []),
]

# script: functions whose level end waits for the default start (see ENDING_NOTE)
ENDINGS = {
    'boneyard/maps/boneyard.gsc': ['objectives'],
    'dcemp/maps/dcemp_endpart.gsc': ['endpart_objective', 'tunnels_end'],
}
ENDING_NOTE = '''{0}. The level ends and the next mission starts after a restart at a co-op checkpoint too: the
   level only went on from its default start ("DEVELOPER: END OF SCRIPTED LEVEL" otherwise),
   and a checkpoint is a start point (maps\\_mw2coop::ending_goes_on).'''

for rel, title, scenes in SCRIPTS:
    name = os.path.basename(rel)
    p = Patch(gsc_root, rel, out_dir)
    n = enemies_for_both(p, scenes)
    assert n, rel
    if rel in ENDINGS:
        p.note(ENDING_NOTE.format(n + 1))
        for fn in ENDINGS[rel]:
            p.subst(fn, 'is_default_start()', 'maps\\_mw2coop::ending_goes_on()')
        n += 1
    if rel == 'cliffhanger/maps/cliffhanger_code.gsc':
        # owner's playtest, 2026-10-23: the enemies always saw player 2
        p.note('''{0}. Stealth: both players get the level's stealth visibility (stance, movement, snow);
   only player 1 had it, so the enemies saw player 2 at any distance.'''.format(n + 1))
        p.subst('player_init', 'level.player stealth_cliffhanger();',
                'foreach ( mw2coop_player in level.players )\n\t\tmw2coop_player stealth_cliffhanger();')
        n += 1
        p.note('''{0}. The heartbeat sensor's hint (shown to both players) goes away by the sensor of the player who sees it: its break
   test read player 1's weapons (night audit 2026-11-17).'''.format(n + 1))
        p.subst('should_break_activate_heartbeat', 'level.player', 'self')
        n += 1
        p.note('''{0}. A guard's view opens wide when a player comes within his sprint-hearing distance (increase_fov_when_player_is_near: the
   blue building's smoker and loader in Spec Ops' Sabotage, the campaign's camp): it watched player 1 alone, and player 2 could walk
   right up to them (Spec Ops audit 2026-11-17, item 1.7). Any player near does it.'''.format(n + 1))
        p.subst('increase_fov_when_player_is_near',
                'if ( DistanceSquared( self.origin, level.player.origin ) < squared( self.footstepDetectDistSprint ) )',
                'if ( DistanceSquared( self.origin, maps\\_mw2coop::seek_player( self.origin ).origin ) < squared( self.footstepDetectDistSprint ) )')
        n += 1
    if rel == 'af_caves/maps/af_caves_backhalf.gsc':
        # night audit 2026-11-17: the cave exit's collapse was player 1's alone
        C = 'maps\\_mw2coop_cave::'
        EACH = 'foreach ( mw2coop_player in level.players ) mw2coop_player'
        p.note('''{0}. The cave exit's collapse (AAA_sequence_airstrip) for both players (maps\\_mw2coop_cave): the self-destruct
   alarm, the knock-down on a view of his own (crouched, slow, invulnerable, shell shocked), the move to the cave mouth
   beside player 1 while the screen is black, the frozen moment of the second blackout and the release. Before, player 2
   kept running, was not protected or shocked, and stayed behind the rubble that closes the cave.'''.format(n + 1))
        fn = 'AAA_sequence_airstrip'
        p.subst(fn, 'level.player PlayLocalSound( "af_caves_selfdestruct" );',
                'level.player PlayLocalSound( "af_caves_selfdestruct" );\n\t' + C + 'partner_local_sound( "af_caves_selfdestruct" );')
        p.subst(fn, 'level.player enableinvulnerability();', 'level.player enableinvulnerability();\n\t' + C + 'collapse_partner_link( playerOrg );')
        p.subst(fn, 'level.player ShellShock( "af_cave_collapse", shellshockTime );',
                'level.player ShellShock( "af_cave_collapse", shellshockTime );\n\t' + C + 'collapse_partner_shock( shellshockTime );')
        p.subst(fn, 'level.player SetOrigin( playerOrg.origin );\n\tlevel.player PlayerLinkToBlend( playerOrg, "tag_player", 1 );',
                'level.player SetOrigin( playerOrg.origin );\n\tlevel.player PlayerLinkToBlend( playerOrg, "tag_player", 1 );\n\t'
                + C + 'collapse_partner_to_exit( cave_exit_playerview_01 );')
        p.subst(fn, 'playerOrg RotateTo( playerOrg.angles + ( 40, 0, -70 ), 12, 5, 5 );',
                'playerOrg RotateTo( playerOrg.angles + ( 40, 0, -70 ), 12, 5, 5 );\n\t' + C + 'collapse_partner_rotate();')
        p.subst(fn, 'level.player FreezeControls( true );', 'level.player FreezeControls( true );\n\t' + C + 'collapse_partner_blackout( cave_exit_playerview_01 );')
        p.subst(fn, 'level.player FreezeControls( false );', EACH + ' FreezeControls( false );')
        p.subst(fn, 'level.player setMoveSpeedScale( 1 );', EACH + ' setMoveSpeedScale( 1 );')
        p.subst(fn, 'level.player disableinvulnerability();', EACH + ' disableinvulnerability();')
        n += 1
    if rel == 'ending/maps/char_museum_code.gsc':
        M = 'maps\_mw2coop_ending::'
        p.note('''{0}. The credits' camera carries both players (camera_move blends both onto it).'''.format(n + 1))
        p.subst('camera_move', 'level.player playerlinktoblend( level.camera, undefined, time );', M + 'museum_link_blend( time, undefined, undefined );')
        p.subst('camera_move', 'level.player playerlinktoblend( level.camera, undefined, time, time * acc, time * dec );',
                M + 'museum_link_blend( time, time * acc, time * dec );')
        n += 1
        # the civilians' idle fix is no part of the co-op: it is its own file, <name>.fix.patch, loaded in single player too (src/scripts.c)
        f = Patch(gsc_root, rel, out_dir)
        f.note('''1. The museum's talking civilians: their idle entries in level.scr_anim are single animations, and
   _anim::anim_weight wants an array of looping animations (its AssertEx is stripped from the release, what is left
   is "size cannot be applied", thousands of script errors a second and civilians in a T-pose, in single player too).
   maps\\_mw2fix_museum::civ_loop makes the entry an array first. A fix of the game's own bug: it is loaded in every session.''')
        f.subst('civ_talkers', 'node anim_generic_loop( self, node.animation );', 'maps\\_mw2fix_museum::civ_loop( node, self );')
        f.write(name + '.fix.patch', '''
mw2coop fix for maps/{0} ({1}, Modern Warfare 2, 64-bit Steam build). Generated by
tools/gen_seek_patches.py; do not edit by hand.
Loaded in single player too (nothing of the co-op in it: scripts/maps/_mw2fix_museum.gsc).
'''.format(name, title))
    p.write(name + '.patch', '''
mw2coop patch for maps/{0} ({1}, Modern Warfare 2, 64-bit Steam build). Generated by
tools/gen_seek_patches.py; do not edit by hand.
'''.format(name, title))
