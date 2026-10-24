"""Write the patches of level scripts whose only co-op changes are about enemies: those sent at
"the player" go for the closest player still up, and scenes that hide "the player" from enemies
hide both (tools/seek_patch.py). Scripts that have a generator of their
own (airport, arcadia, cliffhanger, dcburning, dc_whitehouse, estate, roadkill) call seek_patch from it instead.
Usage: python -I gen_seek_patches.py <extracted gsc root> <mod content/patches/maps dir>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gsc_patch import Patch  # noqa: E402
from seek_patch import enemies_for_both  # noqa: E402

gsc_root, out_dir = sys.argv[1], sys.argv[2]

# script, level, functions whose scene hides "the player" from enemies (level.player.ignoreme)
SCRIPTS = [
    ('af_caves/maps/af_caves.gsc', 'Just Like Old Times', []),
    ('af_caves/maps/af_caves_backhalf.gsc', 'Just Like Old Times', ['AAA_sequence_airstrip']),
    ('af_caves/maps/af_caves_code.gsc', 'Just Like Old Times', []),
    ('boneyard/maps/boneyard.gsc', 'The Enemy of My Enemy', ['intro_player']),
    ('cliffhanger/maps/cliffhanger_code.gsc', 'Cliffhanger', []),
    ('cliffhanger/maps/cliffhanger_stealth.gsc', 'Cliffhanger', []),
    ('contingency/maps/contingency.gsc', 'Contingency', []),
    ('dcemp/maps/dcemp_endpart.gsc', 'Second Sun', ['whitehouse_main']),
    ('ending/maps/char_museum_code.gsc', 'Museum', ['panic_button']),
    ('favela/maps/favela_code.gsc', 'Takedown', []),
    ('favela_escape/maps/favela_escape_code.gsc', "The Hornet's Nest", []),
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
    if rel == 'af_caves/maps/af_caves_code.gsc':
        # the opening rappel for both players (maps\_mw2coop_cave.gsc)
        p.note("""{0}. The opening rappel for both players (maps\_mw2coop_cave): af_caves_rappel_behavior( who )
   is the descent of whoever used the hook-up; the other player rappels the same way afterwards
   (second_done: no guard is left to kill). The guard below shoots the first player. The level's
   flag "rappel_end" is the first player's, so each player has an ending of his own.""".format(n + 1))
        p.replace('af_caves_rappel_behavior()\n{\n', 'af_caves_rappel_behavior( who )\n{\n\tsecond = maps\_mw2coop_cave::is_second( who );\n')
        # the spawners are gone for the second descent: the guards' spawn functions stay registered
        fns = (('2', 'rappel_guard2_patrol'), ('2', 'rappel_guard2_kill_player'), ('2', 'rappel_guard2_death'),
               ('1', 'rappel_guard1_kill_player'), ('2', 'rappel_guards_think'), ('1', 'rappel_guards_think'))
        lines = ''.join('\tarray_spawn_function_noteworthy( "rappel_guard_%s", ::%s );\n' % f for f in fns)
        p.replace(lines, '\tif ( !second )\n\t{\n' + ''.join('\t' + l + '\n' for l in lines.split('\n')[:-1]) + '\t}\n')
        p.replace('\tplayer_rope = spawn_anim_model( "rope" );\n',
                  '\tplayer_rope = spawn_anim_model( "rope" );\n\tmaps\_mw2coop_cave::rope_made( player_rope );\n')
        p.replace('\tarray_spawn( rappel_baddie_spawners );\n',
                  '\tif ( !second )\n\t\tarray_spawn( rappel_baddie_spawners );\n')
        p.replace('\telse\n\t{\n\n\t\tfor ( ;; )\n\t\t{\n\t\t\tif ( !isalive( level.player ) )\n',
                  '\telse\n\t{\n\t\tif ( second )\n\t\t{\n\t\t\tmaps\_mw2coop_cave::second_done( who, ending_player_rig, player_rig, old_weapon );\n\t\t\treturn;\n\t\t}\n\n\t\tfor ( ;; )\n\t\t{\n\t\t\tif ( !isalive( level.player ) )\n')
        p.replace('\tthread player_decent_death();\n', '\tthread player_decent_death( who );\n')
        p.replace('::hurt_player_on_bounce );\n', '::hurt_player_on_bounce, who );\n')
        # the hook-up's view link and the knife kill's relink were player 1's (the second player's
        # rig pulled player 1 up to the top; owner, 2026-10-24)
        p.replace('::player_gets_groundref_and_opens_fov, tag_origin );\n', '::player_gets_groundref_and_opens_fov, tag_origin, who );\n')
        p.replace('player_gets_groundref_and_opens_fov( tag_origin )\n{\n', 'player_gets_groundref_and_opens_fov( tag_origin, who )\n{\n')
        p.subst('player_gets_groundref_and_opens_fov', 'open_up_fov( ', 'maps\\_mw2coop_cave::link_fov( who, ')
        p.subst('player_gets_groundref_and_opens_fov', 'level.player', 'who')
        p.replace('relink_player_for_knife_kill( percentage );\n', 'relink_player_for_knife_kill( percentage, who );\n')
        p.replace('relink_player_for_knife_kill( percentage )\n{\n', 'relink_player_for_knife_kill( percentage, who )\n{\n')
        p.subst('relink_player_for_knife_kill', 'level.player', 'who')
        p.replace('player_decent_death()\n{\n', 'player_decent_death( who )\n{\n')
        p.replace('hurt_player_on_bounce()\n{\n', 'hurt_player_on_bounce( who )\n{\n')
        p.subst('af_caves_rappel_behavior', 'flag_set( "rappel_end" )', 'maps\_mw2coop_cave::rappel_end_set( who )')
        p.subst('af_caves_rappel_behavior', 'flag( "rappel_end" )', 'maps\_mw2coop_cave::rappel_ending( who )')
        for fn in ('af_caves_rappel_behavior', 'player_decent_death', 'hurt_player_on_bounce'):
            p.subst(fn, 'level.player', 'who')
        n += 1
    if rel == 'favela_escape/maps/favela_escape_code.gsc':
        # the chopper's rope ladder for both players (maps\_mw2coop_favelaescape.gsc)
        p.note("""{0}. The chopper's rope ladder for both players (maps\\_mw2coop_favelaescape): whichever player
   leaps for the ladder first starts the scene (who), and the other catches the same rope with a
   rig of his own (partner_jumps).""".format(n + 1))
        src = p.func('solorun_chopperjump')
        orig = src[src.index('\tthread player_jump_watcher();\n'):src.index('\tflag_set( "chopperjump_player_jump" );')]
        p.subst('solorun_chopperjump', 'level.player', 'who')
        single = orig.replace('altLookSpots, true )', 'altLookSpots, true, level.player )')
        spots = '\taltLookSpots = GetStructArray( "struct_chopperjump_alt_lookspot", "targetname" );\n'
        assert spots in orig
        p.replace(orig.replace('level.player', 'who'),
                  '\twho = level.player;\n\tif ( maps\\_mw2coop_favelaescape::coop() )\n\t{\n' + spots +
                  '\t\twho = maps\\_mw2coop_favelaescape::catch_leap( jumpstart_vol, ladder, altLookSpots );\n\t}\n\telse\n\t{\n' +
                  single + '\t}\n\n')
        p.replace('\t// catch the jump\n', '\tthread maps\\_mw2coop_favelaescape::slide_sync();\n\n\t// catch the jump\n')
        p.replace('\tchopper thread anim_single( player_and_ladder, jump_scene, propsTag );\n',
                  '\tchopper thread anim_single( player_and_ladder, jump_scene, propsTag );\n'
                  '\tthread maps\\_mw2coop_favelaescape::partner_jumps( who, chopper, jump_scene, propsTag );\n')
        p.replace('\tthread chopperjump_player_blend_to_anim( player_rig );\n', '\tthread chopperjump_player_blend_to_anim( player_rig, who );\n')
        p.replace('chopperjump_player_blend_to_anim( player_rig )\n{\n', 'chopperjump_player_blend_to_anim( player_rig, who )\n{\n')
        p.subst('chopperjump_player_blend_to_anim', 'level.player', 'who')
        p.replace('player_leaps_to_chopper( volume, ladder, altLookSpots, checkIsOnGround )\n{\n',
                  'player_leaps_to_chopper( volume, ladder, altLookSpots, checkIsOnGround, who )\n{\n')
        p.subst('player_leaps_to_chopper', 'level.player IsOnGround()', '( who IsOnGround() || maps\\_mw2coop::is_standing( who ) )')
        p.subst('player_leaps_to_chopper', 'level.player', 'who')
        n += 1
        # the big jump over the gap and the wake-up for both players (maps\_mw2coop_favelaescape.gsc)
        p.note("""{0}. The big jump over the gap on the roofs for both players (maps\\_mw2coop_favelaescape):
   whichever player leaps (or falls) first starts it; player 1 keeps the level's flow, player 2 plays the
   same jump and wake-up with a rig of his own, 55 units beside player 1's (partner_bigjump,
   partner_fall).""".format(n + 1))
        body = p.func('roofrun_player_bigjump')
        orig = body[body.index('\twhile( 1 )\n\t{\n\t\tbreakout = false;'):body.index('\tflag_set( "player_jump_watcher_stop" );')]
        single = orig.replace('0.915, true )', '0.915, true, level.player )')
        p.replace(orig, '\tif ( maps\\_mw2coop_favelaescape::coop() )\n\t\tmaps\\_mw2coop_favelaescape::catch_bigjump( jumpstart_trig, jumpForward );\n\telse\n\t{\n' + single + '\t}\n\n')
        p.replace('\tanimref thread anim_single_solo( player_rig, jump_scene );\n',
                  '\tanimref thread anim_single_solo( player_rig, jump_scene );\n'
                  '\tthread maps\\_mw2coop_favelaescape::partner_bigjump( startpoint, startangles, groundref );\n')
        p.replace('player_leaps( trig, jump_forward, goodDot, checkIsOnGround )\n{\n',
                  'player_leaps( trig, jump_forward, goodDot, checkIsOnGround, who )\n{\n')
        p.subst('player_leaps', 'level.player IsOnGround()', '( who IsOnGround() || maps\\_mw2coop::is_standing( who ) )')
        p.subst('player_leaps', 'level.player', 'who')
        p.replace('\tthread roofrun_player_recovery();\n',
                  '\tthread roofrun_player_recovery();\n'
                  '\tthread maps\\_mw2coop_favelaescape::partner_fall( GetStruct( "struct_player_recovery_animref", "targetname" ) );\n')
        p.subst('roofrun_player_bigjump', 'player.origin[ 1 ]', 'maps\\_mw2coop_favelaescape::start_y( player )')
        p.subst('roofrun_player_recovery', 'level.player EnableInvulnerability();', 'maps\\_mw2coop_favelaescape::invulnerable( true );')
        p.subst('roofrun_player_recovery', 'level.player DisableInvulnerability();', 'maps\\_mw2coop_favelaescape::invulnerable( false );')
        p.subst('roofrun_player_recovery', 'while( !level.player IsOnGround() )', 'while( !maps\\_mw2coop_favelaescape::all_landed() )')
        n += 1
    if rel == 'af_caves/maps/af_caves.gsc':
        p.note("""{0}. The opening rappel for both players (maps\_mw2coop_cave): whoever uses the hook-up first
   rappels; it is free again for the other player afterwards.""".format(n + 1))
        p.subst('rappel_player_rappel_setup', 'rappel_trigger waittill( "trigger" );', 'rappel_trigger waittill( "trigger", who );')
        p.subst('rappel_player_rappel_setup', 'level.player', 'who')
        p.subst('rappel_player_rappel_setup', 'rappel_trigger delete();', 'maps\_mw2coop_cave::trigger_used( rappel_trigger, who );')
        p.subst('rappel_player_rappel_setup', 'af_caves_rappel_behavior();',
                'af_caves_rappel_behavior( who );\n\tmaps\_mw2coop_cave::partner_turn( rappel_trigger, who );')
        p.subst('rappel_guard1_kill_player', 'flag_wait( "rappel_end" );',
                'flag_wait( "rappel_end" );\n\tfirst = maps\_mw2coop_cave::first_rappeller();')
        p.subst('rappel_guard1_kill_player', 'level.player', 'first')
        n += 1
    p.write(name + '.patch', '''
mw2coop patch for maps/{0} ({1}, Modern Warfare 2, 64-bit Steam build). Generated by
tools/gen_seek_patches.py; do not edit by hand.
'''.format(name, title))
