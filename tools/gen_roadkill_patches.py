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

EACH = 'foreach ( mw2coop_player in level.players )\n\t\t'
note = '''12. The protection of "the player" while he cannot fight back covers both players (night audit 2026-11-17): near the
   commanders at the start (protect_player_while_scene_is_active, player_is_protected_near_commanders: random bullets miss),
   in red-flashing health on the ride (player_doesnt_die_in_red_flashing: enemies stop hitting him), the full health right
   after the ride (player_gets_max_health_for_dismount) and the return to normal afterwards (player_becomes_normal_gameskill).
   Their accuracy against him goes with maps\\_utility::set_player_attacker_accuracy (_utility.gsc.patch hunk 8).'''
for old in ('\t\tset_player_attacker_accuracy( 0 );\n\t\tlevel.player.IgnoreRandomBulletDamage = true;\n',
            '\t\tmaps\\_gameskill::updateAllDifficulty();\n\t\tlevel.player.IgnoreRandomBulletDamage = false;\n\t}\n',
            '\tprotect_player_while_scene_is_active();\n\tmaps\\_gameskill::updateAllDifficulty();\n\tlevel.player.IgnoreRandomBulletDamage = false;\n'):
    old = unique(code, old)
    hunks.append((note, old, old.replace('level.player.IgnoreRandomBulletDamage', 'foreach ( mw2coop_player in level.players )\n\t\t\tmw2coop_player.IgnoreRandomBulletDamage'
                                        if old.startswith('\t\t') else 'foreach ( mw2coop_player in level.players )\n\t\tmw2coop_player.IgnoreRandomBulletDamage')))
    note = '   (same)'
# (hunk 11 made player 1's red flashing set both players' accuracy: this hunk works on the text it left)
unique(code, '\t\tif ( level.player ent_flag( "player_has_red_flashing_overlay" ) )\n\t\t\tlevel.player.attackeraccuracy = 0;\n')
old = '\t\tif ( level.player ent_flag( "player_has_red_flashing_overlay" ) )\n\t\t\tmaps\\_mw2coop::players_attackeraccuracy( 0 );\n'
hunks.append(('   (same)', old, '\t\tforeach ( mw2coop_player in level.players )\n'
              '\t\t\tif ( mw2coop_player ent_flag( "player_has_red_flashing_overlay" ) )\n\t\t\t\tmw2coop_player.attackeraccuracy = 0;\n'))
old = unique(code, '\t\tlevel.player.health = level.player.maxhealth;\n')
hunks.append(('   (same)', old, '\t\tforeach ( mw2coop_player in level.players )\n'
              '\t\t\tif ( IsAlive( mw2coop_player ) && !maps\\_mw2coop::is_downed( mw2coop_player ) )\n'
              '\t\t\t\tmw2coop_player.health = mw2coop_player.maxhealth;\n'))
old = unique(code, '\tlevel.player recover_random_bullet_damage();\n')
hunks.append(('   (same)', old, '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player recover_random_bullet_damage();\n'))
old = unique(code, 'recover_random_bullet_damage()\n{\n\tlevel.player.IgnoreRandomBulletDamage = false;\n')
hunks.append(('   (same)', old, old.replace('level.player.', 'self.')))
old = unique(code, '\t\tset_player_attacker_accuracy( 1000 );\n')
hunks.append(('''13. Exposing himself on the street in front of the school (handle_player_exposing_himself_in_front_of_school) is still
   player 1's rule, as before: set_player_attacker_accuracy() on the level now sets both players (hunk 12), and the rule
   must not punish player 2 for player 1's move. Unknown which player the flag's trigger saw.''', old,
              '\t\tlevel.player set_player_attacker_accuracy( 1000 );\n'))

note = '''14. The flash-bang and grenade-launcher lessons (shown to both players) go away by the throw or the weapon of the player who
   sees them: their break tests read player 1's (night audit 2026-11-17). The level also calls player_learned_m203() on itself
   (riverbank_player_learns_m203): that call stays player 1's (get_player_from_self).'''
old = unique(code, '\tlevel.player.flash_ammo = level.player GetWeaponAmmoStock( "flash_grenade" );\n')
hunks.append((note, old, '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player.flash_ammo = mw2coop_player GetWeaponAmmoStock( "flash_grenade" );\n'))
for name in ('player_learned_flash', 'player_learned_m203'):
    old = func(code, name)
    new = old.replace('level.player', 'mw2coop_player').replace(name + '()\n{\n', name + '()\n{\n\tmw2coop_player = get_player_from_self();\n', 1)
    assert new.count('get_player_from_self') == 1, name
    hunks.append(('   (same)', old, new))
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
note = '''4. Both players ride, so the ride's protection covers both (night audit 2026-11-17): the death shield from the start of the
   ride to the crazy ride, again for the end of the ride and four seconds after the crash; random bullets missing the
   players after the enemies retreat, at the end of the ride and at the dismount start points; the near-death vision off.
   Before, player 1 alone had them, and he may not be the gunner.'''
for old, new in (
        ('\tlevel.player EnableDeathShield( true );\n\n\n\tthread player_doesnt_die_in_red_flashing();\n\tlevel.player DisableWeapons();\n'
         '\tlevel.player ent_flag_clear( "near_death_vision_enabled" );\n',
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player EnableDeathShield( true );\n\n\n'
         '\tthread player_doesnt_die_in_red_flashing();\n\tlevel.player DisableWeapons();\n'
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player ent_flag_clear( "near_death_vision_enabled" );\n'),
        ('\n\tlevel.player EnableDeathShield( false );\n\n\n\twait( 0.05 );// wait for level.lead_vehicle to get set.\n',
         '\n\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player EnableDeathShield( false );\n\n\n\twait( 0.05 );// wait for level.lead_vehicle to get set.\n'),
        ('\tlevel.player.IgnoreRandomBulletDamage = false;\n\n\tarray_thread( level.school_ambush_spawners, ::spawn_ai );\n',
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player.IgnoreRandomBulletDamage = false;\n\n\tarray_thread( level.school_ambush_spawners, ::spawn_ai );\n'),
        ('\tlevel.player.IgnoreRandomBulletDamage = true;\n\tthread player_gets_max_health_for_dismount();\n',
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player.IgnoreRandomBulletDamage = true;\n\tthread player_gets_max_health_for_dismount();\n'),
        ('\tlevel.player EnableDeathShield( true );\n\trocket = undefined;\n',
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player EnableDeathShield( true );\n\trocket = undefined;\n'),
        ('\tlevel.player delayCall( 4, ::EnableDeathShield, false );\n',
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player delayCall( 4, ::EnableDeathShield, false );\n'),
        ('\tlevel.player.IgnoreRandomBulletDamage = true;\n\tthread player_becomes_normal_gameskill();\n',
         '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player.IgnoreRandomBulletDamage = true;\n\tthread player_becomes_normal_gameskill();\n')):
    hunks.append((note, unique(main, old), new))
    note = '   (same)'
old = unique(main, '\tlevel.player ShellShock( "default", 5 );\n')
hunks.append(('''5. The crash at the end of the ride knocks both players down: the shell shock and the prone landing (player 2 is put next to
   player 1 by hunk 2).''', old, '\tforeach ( mw2coop_player in level.players )\n\t\tmw2coop_player ShellShock( "default", 5 );\n'))
old = unique(main, '\tlevel.player SetStance( "prone" );\n\tlevel.player AllowCrouch( true );\n\tlevel.player AllowProne( true );\n')
hunks.append(('   (same)', old, '\tforeach ( mw2coop_player in level.players )\n\t{\n\t\tmw2coop_player SetStance( "prone" );\n'
              '\t\tmw2coop_player AllowCrouch( true );\n\t\tmw2coop_player AllowProne( true );\n\t}\n'))
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
