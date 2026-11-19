"""Write the Loose Ends (estate) co-op patches, taking every replaced text from the extracted game
scripts so it matches byte for byte: the enemy changes (tools/seek_patch.py) and the ending for
two players (maps\\_mw2coop_estate).
Usage: python -I gen_estate_patches.py <extracted gsc root> <mod content/patches/maps dir>"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gsc_patch import Patch  # noqa: E402
from seek_patch import enemies_for_both  # noqa: E402

gsc_root, out_dir = sys.argv[1], sys.argv[2]
M = 'maps\\_mw2coop_estate::'
EACH = 'foreach ( mw2coop_player in level.players ) mw2coop_player'
STATEMENT = re.compile(r'^[ \t]*(level\.player[ .][^;\n]*;)', re.M)

# What the ending does to player 1 alone: Shepherd's hit (the other player gets it from
# _mw2coop_estate::partner_shot), its rumbles, the damage watch (it sets both players' accuracy
# already), the breathing sound (played where both are), and the blur cycle (paced by player 1).
ONLY_PLAYER_1 = [
    'level.player PlayRumbleOnEntity( "shepherd_pistol" );',
    'level.player delaycall( 1.3, ::PlayRumbleOnEntity, "shot_collapse" );',
    'level.player SetNormalHealth( 1 );',
    'level.player DoDamage( 99 / level.player.damagemultiplier, gunpos );',
    'level.player delayThread( 0.3, ::ending_player_fullhealth );',
    'level.player thread ending_drag_damagecheck();',
    'level.player play_sound_on_entity( "breathing_hurt" );',
    'level.player endingBlurView( 3.6, 2 );',
    'level.player endingBlurView( 1, 0.8 );',
    'level.player endingBlurView( 2, 1 );',
    'level.player endingBlurView( 1, 0.7 );',
    'level.player endingBlurView( 3, 1.1 );',
]


def both(p, fn):
    """Every other live `level.player ...;` statement of fn is done to each player."""
    body = p.func(fn)
    olds = []
    for m in STATEMENT.finditer(body):
        stmt = m.group(1)
        if stmt not in ONLY_PLAYER_1 and stmt not in olds:
            olds.append(stmt)
    for stmt in olds:
        p.subst(fn, stmt, EACH + stmt[len('level.player'):])
    return len(olds)


# ---------------------------------------------------------------- estate.gsc
p = Patch(gsc_root, 'estate/maps/estate.gsc', out_dir)
n = enemies_for_both(p, ['ending_drag_failure'])
p.note('''{0}. The ending for two players (maps\\_mw2coop_estate): both go through it as Roach, held on
   the same view with no bodies of their own; what it does to player 1 it does to each player,
   except Shepherd's hit (the other player takes it from maps\\_mw2coop_estate and sees the scene on);
   no damage between the players while they are held there; both go in the one throw.'''.format(n + 1))
p.subst('ending_moments', 'flag_wait( "play_ending_sequence" );',
        'flag_wait( "play_ending_sequence" );\n\t' + M + 'players_into_ending();')
assert both(p, 'ending_moments') >= 20
p.subst('ending_moments', 'level.player DoDamage( 99 / level.player.damagemultiplier, gunpos );',
        'level.player DoDamage( 99 / level.player.damagemultiplier, gunpos );\n\t\t' + M + 'partner_shot( gunpos );')
p.subst('ending_moments', 'node anim_first_frame_solo( playerBody, "estate_ending_part2" );',
        'node anim_first_frame_solo( playerBody, "estate_ending_part2" );\n\t' + M + 'partner_wakes( 3 );')
for fn in ('ending_gasoline_sequence', 'ending_passing_out', 'ending_slowmo', 'ending_mortarhit_ghost_sequence',
           'startEnding'):
    assert both(p, fn), fn
p.subst('ending_blur_cycler', 'level.player endingBlurView(', M + 'players_blur_view(')
p.note('''{0}. The forest fight's start point (and a co-op restart there) puts every player in the state the mine ambush
   leaves him in (thrown, shocked, prone for a moment), not player 1 alone.'''.format(n + 2))
p.subst('estate_ambush', 'thread bouncing_betty_throwplayer( level.player.origin );',
        'foreach ( mw2coop_player in level.players )\n\t\tthread bouncing_betty_throwplayer( mw2coop_player.origin, mw2coop_player );')
p.note('''{0}. The objectives also come right when the level starts at a later start point (a co-op restart at a checkpoint or a Continue):
   objectives() waited for deploy_house_defense_jeeps and ghost_gives_regroup_order, which no start function sets, and stood on its
   first objective ("Assassinate Makarov", at the house) for the rest of the level (owner, 2026-11-17: after the DSM the marker stayed at
   the house). Each of those waits also ends at the flag of the next step, which every later start sets.'''.format(n + 3))
p.subst('objectives', 'flag_wait( "deploy_house_defense_jeeps" );',
        'flag_wait_any( "deploy_house_defense_jeeps", "house_exterior_has_been_breached" );')
p.subst('objectives', 'flag_wait( "ghost_gives_regroup_order" );',
        'flag_wait_any( "ghost_gives_regroup_order", "house_briefing_is_over" );')
p.note('''{0}. The ambush's and the forest fight's start points put the level's "starterguy" allies on their start nodes: in a co-op
   session there are more of them than nodes, and the one past the last node was a script error that ended the start function before
   the troops and the scouts were spawned and the start's triggers were used (both are checkpoints: a restart there was broken; night
   tour 2026-11-18). The ones past the last node stay where they are.'''.format(n + 4))
for fn in ('startAmbush', 'startForestFight'):
    p.subst(fn, 'redshirt.origin = nodes[ index ].origin;\n\t\t\tredshirt.angles = nodes[ index ].angles;',
            'if ( index < nodes.size )\n\t\t\t{\n\t\t\t\tredshirt.origin = nodes[ index ].origin;\n\t\t\t\tredshirt.angles = nodes[ index ].angles;\n\t\t\t}')
p.write('estate.gsc.patch', '''
mw2coop patch for maps/estate.gsc (Loose Ends, Modern Warfare 2, 64-bit Steam build). Generated by
tools/gen_estate_patches.py; do not edit by hand.''')

# ---------------------------------------------------------------- estate_code.gsc
p = Patch(gsc_root, 'estate/maps/estate_code.gsc', out_dir)
n = enemies_for_both(p, ['bouncing_betty_gameplay_shock'])
p.note('''{0}. The run's end at the LZ (the mortar that knocks Roach down before the ending): both players
   are shielded from death and held for a moment.'''.format(n + 1))
assert both(p, 'birchfield_knockout') == 4
p.note('''{0}. The bouncing-betty ambush for both players (maps\\_mw2coop_estate): each player gets a mine of his own
   in front of him (playerFovGroundSpot for that player), its hint, its rule (prone: it throws him away from
   it, with its shock and the prone-only moment; standing: it kills him) and the slow motion's walk speed.
   Before, only player 1's mine existed: player 2 walked through the ambush unthrown, unshocked, at full
   speed, and could stand.'''.format(n + 2))
P = 'mw2coop_player'
p.subst('bouncing_betty_slow_mo', 'level.player SetMoveSpeedScale( slomobreachplayerspeed );', EACH + ' SetMoveSpeedScale( slomobreachplayerspeed );')
p.subst('bouncing_betty_slow_mo', 'level.player SetMoveSpeedScale( 1.0 );', EACH + ' SetMoveSpeedScale( 1.0 );')
# the mine's place in front of a given player (player 1 when none is given: the mortars use it too)
p.subst('playerFovGroundSpot', 'level.player', P)
p.subst('playerFovGroundSpot', 'player_can_see_origin( alt.origin + ( 0, 0, 6 ) )', M + 'sees_origin( ' + P + ', alt.origin + ( 0, 0, 6 ) )')
p.subst('playerFovGroundSpot', 'playerFovGroundSpot( specialPlayerCase )\n{\n',
        'playerFovGroundSpot( specialPlayerCase, ' + P + ' )\n{\n\tif ( !isdefined( ' + P + ' ) )\n\t\t' + P + ' = level.player;\n')
p.subst('bouncing_betty_activate', 'thread bouncing_betty_hintprint();', M + 'partner_mine();\n\tthread bouncing_betty_hintprint();')
p.subst('bouncing_betty_activate', 'level.player allowSprint( false );', EACH + ' allowSprint( false );')
p.subst('bouncing_betty_hintprint', 'level.player thread display_hint(', EACH + ' thread display_hint(')
# the mine: the player it is for (the personal mines); a forest mine checks every player (gameplay_mine_blast)
p.subst('bouncing_betty_fx', 'level.player', P)
p.subst('bouncing_betty_fx', 'bouncing_betty_fx( specialPlayerCase, gameplayOn, skipFX )\n{\n',
        'bouncing_betty_fx( specialPlayerCase, gameplayOn, skipFX, ' + P + ' )\n{\n\tif ( !isdefined( ' + P + ' ) )\n\t\t' + P + ' = level.player;\n')
p.subst('bouncing_betty_fx', 'friendlies = getaiarray( "allies" );',
        'friendlies = getaiarray( "allies" );\n\t\tif ( ' + P + ' != level.player )\n\t\t\tfriendlies = [];')
p.subst('bouncing_betty_fx', 'flag_set( "mine_throw_player" );', P + '.mw2coop_mine_throw = true;\n\t\t\tflag_set( "mine_throw_player" );')
p.subst('bouncing_betty_fx', 'if( gameplay )\n\t\t{\n\t\t\tdist = length( ' + P + '.origin - self.origin );',
        'if( gameplay && ' + M + 'coop() )\n\t\t\t' + M + 'gameplay_mine_blast( self, explosion_org );\n\t\telse if( gameplay )\n\t\t{\n\t\t\tdist = length( ' + P + '.origin - self.origin );')
# a forest mine goes off for the player who walks into it and looks at it
p.subst('bouncing_betty_gameplay', 'level.player', P)
p.subst('bouncing_betty_gameplay', 'self waittill ( "trigger" );', 'self waittill ( "trigger", ' + P + ' );\n\t\tif ( !isdefined( ' + P + ' ) || !isplayer( ' + P + ' ) )\n\t\t\t' + P + ' = level.player;')
# the throw, the shock and the prone-only moment of the player whose mine it was
p.subst('bouncing_betty_throwplayer', 'level.player', P)
p.subst('bouncing_betty_throwplayer', 'flag_wait( "mine_throw_player" );', M + 'throw_wait( ' + P + ' );')
p.subst('bouncing_betty_throwplayer', 'thread bouncing_betty_shellshock();', 'thread bouncing_betty_shellshock( ' + P + ' );')
p.subst('bouncing_betty_throwplayer', 'thread bouncing_betty_stances();', 'thread bouncing_betty_stances( ' + P + ' );')
p.subst('bouncing_betty_throwplayer', 'thread ambush_fake_rpg_barrage();', 'if ( ' + P + ' == level.player )\n\t\tthread ambush_fake_rpg_barrage();')
p.subst('bouncing_betty_throwplayer', 'bouncing_betty_throwplayer( mineOrg )\n{\n',
        'bouncing_betty_throwplayer( mineOrg, ' + P + ' )\n{\n\tif ( !isdefined( ' + P + ' ) )\n\t\t' + P + ' = level.player;\n')
p.subst('bouncing_betty_shellshock', 'bouncing_betty_shellshock()\n{\n',
        'bouncing_betty_shellshock( ' + P + ' )\n{\n\tif ( isdefined( ' + P + ' ) && ' + P + ' != level.player )\n\t{\n\t\t'
        + P + ' ' + M + 'partner_mine_shock();\n\t\treturn;\n\t}\n')
p.subst('bouncing_betty_stances', 'level.player', P)
p.subst('bouncing_betty_stances', 'bouncing_betty_stances()\n{\n',
        'bouncing_betty_stances( ' + P + ' )\n{\n\tif ( !isdefined( ' + P + ' ) )\n\t\t' + P + ' = level.player;\n')
# each player's prone-only moment starts when his own slide is over (EndSliding allows every stance): the level's flag is set by the first one
p.subst('bouncing_betty_throwplayer', 'flag_set( "bouncing_betty_player_released" );',
        'flag_set( "bouncing_betty_player_released" );\n\t' + P + ' notify( "mw2coop_mine_released" );')
p.subst('bouncing_betty_stances', 'flag_wait( "bouncing_betty_player_released" );', P + ' waittill( "mw2coop_mine_released" );')
p.note('''{0}. The hint to go prone goes away by the stance of the player it is shown to (it read player 1's).'''.format(n + 3))
p.subst('should_break_mineavoid_hint', 'level.player', 'self')
p.note('''{0}. The claymore hint (shown to both players) goes away when the player who sees it takes his claymores; the d-pad press it
   also reads is a key of the local machine, kept for player 1 only (night audit 2026-11-17), and only outside a co-op session: there
   buttonPressed() is a script error ("not valid to use in network special ops", night tour 2026-11-18), every time the hint looked.'''.format(n + 4))
p.subst('should_break_claymore_hint', 'weapon = level.player GetCurrentWeapon();', 'weapon = self GetCurrentWeapon();')
p.subst('should_break_claymore_hint', 'if( !( level.player buttonpressed( "DPAD_RIGHT" ) ) )',
        'if( self != level.player || maps\\_mw2coop::is_campaign_coop() || !( level.player buttonpressed( "DPAD_RIGHT" ) ) )')
p.write('estate_code.gsc.patch', '''
mw2coop patch for maps/estate_code.gsc (Loose Ends, Modern Warfare 2, 64-bit Steam build). Generated by
tools/gen_estate_patches.py; do not edit by hand.''')
