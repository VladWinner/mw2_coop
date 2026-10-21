"""Write the Team Player (roadkill) co-op patches, taking every replaced text from the extracted
game scripts so it matches byte for byte.
Usage: python -I gen_roadkill_patches.py <extracted roadkill/maps dir> <mod content/patches/maps dir>"""
import os
import re
import sys

src_dir, out_dir = sys.argv[1], sys.argv[2]


def load(name):
    return open(os.path.join(src_dir, name), encoding='latin-1').read().replace('\r', '')


def unique(src, text):
    assert src.count(text) == 1, (src.count(text), text[:80])
    return text


def func(src, name):
    """The whole top-level function, header to closing brace."""
    i = src.index('\n' + name + '(') + 1
    j = src.index('\n}\n', i) + 3
    return unique(src, src[i:j])


def write(name, header, hunks):
    out = [header.strip('\n') + '\n']
    for note, old, new in hunks:
        out.append('\n' + note.strip('\n') + '\n@@@ replace\n' + old + '@@@ with\n' + new + '@@@ end\n')
    path = os.path.join(out_dir, name)
    open(path, 'w', encoding='utf-8', newline='\n').write(''.join(out))
    print('wrote', path, len(hunks), 'hunks')


COOP = 'maps\\_mw2coop::is_campaign_coop()'

# ---------------------------------------------------------------- roadkill_code.gsc
code = load('roadkill_code.gsc')
hunks = []

old = unique(code, '\t\tif ( Distance( self.origin, level.player.origin ) < 80 )\n\t\t\tself notify( "trigger" );\n')
hunks.append(('''1. The glowing seat answers either player (player 1 first on a tie), and says who.''', old,
              '\t\tforeach ( player in level.players )\n'
              '\t\t\tif ( Distance( self.origin, player.origin ) < 80 )\n'
              '\t\t\t\tself notify( "trigger", player );\n'))

old = unique(code, '\tchair waittill( "trigger" );\n\tlevel.player DisableWeapons();\n')
hunks.append(('''2. Whoever boards first is the gunner (maps\\_mw2coop_roadkill::boarding_gunner; player 1
   outside co-op).''', old,
              '\tchair waittill( "trigger", user );\n'
              '\tgunner = maps\\_mw2coop_roadkill::boarding_gunner( user );\n'
              '\tgunner DisableWeapons();\n'))

old = unique(code, '\tflag_set( "player_gets_in" );\n')
hunks.append(('''3. In co-op the convoy leaves ("player_gets_in") once the second player has boarded too.''', old,
              old.replace('\tflag_set( "player_gets_in" );\n', '\tself maps\\_mw2coop_roadkill::gunner_boarded();\n')))

old = unique(code, '\tlevel.player SetStance( "stand" );\n\tlevel.player AllowCrouch( false );\n\tlevel.player AllowProne( false );\n\n'
                   '\tlevel.player PlayerLinkToBlend( player_rig, "tag_player", 0.4, 0.2, 0.2 );\n')
hunks.append(('''4. The get-in animation is the gunner's.''', old, re.sub(r'level\.player\b', 'gunner', old)))

old = unique(code, '\tlevel.player EnableWeapons();\n\tplayer_gets_on_turret();\n')
hunks.append(('''5. ... and so are the weapons afterwards.''', old, old.replace('level.player', 'gunner')))

old = func(code, 'player_gets_on_turret')
new = re.sub(r'level\.player\b', 'gunner', old)
new = new.replace('{\n', '{\n\tgunner = maps\\_mw2coop::vehicle_gunner();\n', 1)
hunks.append(('''6. The turret goes to the gunner (maps\\_mw2coop::vehicle_gunner(): player 1 outside co-op).''', old, new))

old = unique(code, '\tself player_gets_on_turret();\n')
hunks.append(('''7. Start points that begin on the turret: the second player rides on a rear seat.''', old,
              old + '\tself thread maps\\_mw2coop_roadkill::partner_rides_now();\n'))

old = unique(code, '\tlevel.player PlayerLinkToDelta( player_rig, "tag_player", 1, arc, arc, arc, arc, 1 );\n')
hunks.append(('''8. Shepherd's "get on the line" scene: player 2 shares player 1's view rig (as COD-MOD did), so
   both see the scene and start the fight in the same place.''', old,
              old + '\tif ( ' + COOP + ' )\n\t\tlevel.player2 maps\\_mw2coop_roadkill::share_scene_rig( player_rig, arc );\n'))
old = unique(code, '\tstruct waittill ( scene );\n\tlevel.player unlink();\n')
hunks.append(('''9. ... and leaves it next to player 1.''', old,
              old + '\tif ( ' + COOP + ' )\n\t\tlevel.player2 maps\\_mw2coop_roadkill::leave_scene_rig();\n'))

old = unique(code, '\tself.favoriteenemy = level.player;\n')
hunks.append(('''10. The enemy behind the corner (corner_hider_spawner) goes for the closest player still up
   (maps\\_mw2coop::seek_player; player 1 outside co-op).''', old,
              '\tself.favoriteenemy = maps\\_mw2coop::seek_player( self.origin );\n'))

note = '''11. How well enemies hit "the player" and how much they want him (attackeraccuracy, threatbias)
   is set on both players (maps\\_mw2coop::players_attackeraccuracy, players_threatbias): the
   partner is in the same scene.'''
for line in ('\t\t\tlevel.player.attackeraccuracy = 0;', '\tlevel.player.threatbias = 5000;', '\tlevel.player.threatbias = 150;',
             '\t\tlevel.player.threatbias = 50000;', '\t\tlevel.player.threatbias = 150;'):
    old = unique(code, '\n' + line + '\n')
    m = re.match(r'(\t*)level\.player\.(\w+) = (.+);', line)
    hunks.append((note, old, '\n' + m.group(1) + 'maps\\_mw2coop::players_' + m.group(2) + '( ' + m.group(3) + ' );\n'))
    note = '   (same)'

write('roadkill_code.gsc.patch', '''mw2coop patch for maps/roadkill_code.gsc (Team Player, Modern Warfare 2, 64-bit Steam build)
Co-op logic lives in mw2coop/scripts/maps/_mw2coop_roadkill.gsc. Outside a co-op campaign
session every change does what the original did.''', hunks)

# ---------------------------------------------------------------- roadkill.gsc
main = load('roadkill.gsc')
hunks = []
old = unique(main, '\torg = level.player.origin;\n//\tlevel.player FreezeControls( true );\n')
hunks.append(('''1. Ride end: the second player gets off too (the gunner may be player 2).''', old,
              '\tmaps\\_mw2coop_roadkill::ride_ends();\n' + old))
old = unique(main, '\tlevel.player SetOrigin( player_slide_crash.origin );\n')
hunks.append(('''2. Crash: player 2 lands next to player 1.''', old,
              old + '\tif ( ' + COOP + ' )\n\t\tlevel.player2 thread maps\\_mw2coop::teleport_to_partner_when_safe( level.player, 5 );\n'))
old = unique(main, '\tlevel.player.ignoreme = false;\n')
hunks.append(('''3. After the crazy ride enemies see both players again, not only player 1
   (maps\\_mw2coop::players_ignoreme).''', old, '\tmaps\\_mw2coop::players_ignoreme( false );\n'))
write('roadkill.gsc.patch', '''mw2coop patch for maps/roadkill.gsc (Team Player, Modern Warfare 2, 64-bit Steam build)''', hunks)

# ---------------------------------------------------------------- _minigun_viewmodel.gsc
mini = load('_minigun_viewmodel.gsc')
hunks = []
loop_start = mini.index('\tfor(;;)\n\t{\n\t\tif( level.player [[checkFunc]]() )')
loop_end = mini.index('\n}\n', loop_start)
old = unique(mini, mini[loop_start:loop_end + 1])
new = old.replace('\tfor(;;)\n\t{\n', '\tfor(;;)\n\t{\n\t\tgunner = maps\\_mw2coop::vehicle_gunner();\n', 1)
new = new.replace('level.player [[checkFunc]]()', 'gunner [[checkFunc]]()')
hunks.append(('''1. The turret hands follow the gunner's buttons (player 1 outside co-op).''', old, new))
for name in ('spinButtonPressed', 'fireButtonPressed'):
    old = func(mini, name)
    hunks.append(('''2. Button checks run on the player they are called on.''' if name == 'spinButtonPressed' else '3. Same.',
                  old, old.replace('level.player ', 'self ')))
write('_minigun_viewmodel.gsc.patch', '''mw2coop patch for maps/_minigun_viewmodel.gsc (Team Player, Modern Warfare 2, 64-bit Steam build)''', hunks)
