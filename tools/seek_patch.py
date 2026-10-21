"""Shared by the gen_*_patches.py generators: enemies a level script sends at "the player" go for
the closest player still up (maps\\_mw2coop::seek_player, as Spec Ops'
get_closest_player_healthy; player 1 outside co-op). Two forms are rewritten in every function:
    <ent>.favoriteenemy = level.player;         -> ... = maps\\_mw2coop::seek_player( <ent>.origin );
    <ent> setgoalentity( level.player )         -> <ent> setgoalentity( maps\\_mw2coop::seek_player( <ent>.origin ) )
Lines that are comments are left alone."""
import re

SEEK = 'maps\\_mw2coop::seek_player'
ENT = r'[A-Za-z_][A-Za-z0-9_]*(?:\[\s*[^\]\n]*\])?'
FORMS = [
    re.compile(r'(?P<ent>' + ENT + r')(?P<mid>\s*\.\s*favoriteenemy\s*=\s*)level\.player(?![A-Za-z0-9_.\[])', re.I),
    re.compile(r'(?P<ent>' + ENT + r')(?P<mid>\s+setgoalentity\(\s*)level\.player(?P<end>\s*\))', re.I),
]
HEADER = re.compile(r'\n([A-Za-z_][A-Za-z0-9_]*)\([^\n]*\)[ \t]*\n\{')


def _live(body, at):
    """The match at `at` is not on a // comment line."""
    line_start = body.rfind('\n', 0, at) + 1
    return not body[line_start:at].lstrip().startswith('//')


def seek_everywhere(p):
    """Add the hunks to Patch p; returns how many functions changed."""
    import check_patches
    changed = 0
    for m in HEADER.finditer('\n' + p.src):
        fn = m.group(1)
        if not check_patches.find_function(p.src, fn):
            continue
        body = p.func(fn)
        olds = []
        for form in FORMS:
            for hit in form.finditer(body):
                if _live(body, hit.start()) and hit.group(0) not in olds:
                    olds.append(hit.group(0))
        for old in olds:
            hit = None
            for form in FORMS:
                hit = form.fullmatch(old)
                if hit:
                    break
            ent = hit.group('ent')
            new = ent + hit.group('mid') + SEEK + '( ' + ent + '.origin )' + (hit.groupdict().get('end') or '')
            p.subst(fn, old, new)
        if olds:
            changed += 1
    return changed


IGNORE = 'maps\_mw2coop::players_ignoreme'


def both_ignoreme(p, fns):
    """In each function of fns, a scene's level.player.ignoreme = true/false covers both players
    (maps\_mw2coop::players_ignoreme): the partner is in the same scene."""
    for fn in fns:
        body = p.func(fn)
        hits = 0
        for value in ('true', 'false'):
            old = 'level.player.ignoreme = ' + value + ';'
            if old in body:
                p.subst(fn, old, IGNORE + '( ' + value + ' );')
                hits += 1
        assert hits, (fn, 'no level.player.ignoreme')


FIELDS = re.compile(r'level\.player\.(?P<field>attackeraccuracy|threatbias)\s*=\s*(?P<value>[^;\n]+?)\s*;', re.I)


def both_fields(p):
    """In every function, a level script's level.player.attackeraccuracy = v; or
    level.player.threatbias = v; sets it on both players (maps\\_mw2coop::players_attackeraccuracy,
    players_threatbias): the partner is in the same scene. Reads are left alone. Returns how many
    assignments changed."""
    import check_patches
    changed = 0
    for m in HEADER.finditer('\n' + p.src):
        fn = m.group(1)
        if not check_patches.find_function(p.src, fn):
            continue
        body = p.func(fn)
        olds = []
        for hit in FIELDS.finditer(body):
            if _live(body, hit.start()) and hit.group(0) not in olds:
                olds.append(hit.group(0))
        for old in olds:
            hit = FIELDS.fullmatch(old)
            p.subst(fn, old, 'maps\\_mw2coop::players_' + hit.group('field').lower() + '( ' + hit.group('value') + ' );')
            changed += 1
    return changed
