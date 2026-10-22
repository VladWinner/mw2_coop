"""For levels whose add_start() has no section function, find what marks each start point being
reached in a normal run of the level, for co-op checkpoints:
  - flags: flags the start point's start function sets that the previous start point's does not
    (the section it begins), which the normal flow also sets: in a script outside the start
    functions, or by a map trigger (script_flag on a trigger_multiple_flag_set, from the zone);
  - handler: a function of the level the start function calls or threads that the normal flow
    also calls (Wolverines!: start_bmp threads handler_house_destroyer_to_pizza, which
    handler_yards_to_house_destroyer calls when the section before ends).
Prints a table to review. Usage:
  python -I find_section_handlers.py <extracted gsc root> <zone dir or -> <level> [<level> ...]"""
import os
import re
import sys
import zlib

root, zone_dir, levels = sys.argv[1], sys.argv[2], sys.argv[3:]
CALL = re.compile(r'(?:::|\b)([A-Za-z_][A-Za-z0-9_]*)\s*\(|::([A-Za-z_][A-Za-z0-9_]*)')
FLAG_SET = re.compile(r'(?:\bflag_set(?:_delayed)?\s*\(\s*|::flag_set\s*,\s*)"([^"]+)"')
SKIP = set('''if while for foreach switch return isdefined assert assertex wait waittill notify endon
flag_set flag_wait flag_clear flag_init flag getstruct getent getentarray getstructarray spawn
spawn_ai array_thread setorigin setplayerangles teleport_player thread delaythread add_func
activate_trigger activate_trigger_with_targetname autosave_by_name objective_add objective_state
set_speed'''.split())


def functions(text):
    """name -> (start, end) of each top-level function."""
    out = {}
    for m in re.finditer(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\([^)\n]*\)\s*\n\{', text, re.M):
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out[m.group(1)] = (m.start(), i + 1)
    return out


def trigger_flags(level):
    """script_flag values of the map's flag-setting triggers, from the level's zone."""
    if zone_dir == '-':
        return set()
    path = os.path.join(zone_dir, level + '.ff')
    if not os.path.exists(path):
        return set()
    data = open(path, 'rb').read()
    raw = b''
    for s in range(8, 128):
        if data[s] == 0x78 and data[s + 1] in (1, 0x5e, 0x9c, 0xda):
            try:
                raw = zlib.decompressobj().decompress(data[s:])
                break
            except zlib.error:
                pass
    out = set()
    for block in re.findall(rb'\{\n("[^{}]*?)\n\}', raw):
        cls = re.search(rb'"classname" "([^"]+)"', block)
        flag = re.search(rb'"script_flag" "([^"]+)"', block)
        if cls and flag and cls.group(1).startswith(b'trigger'):
            out.add(flag.group(1).decode())
    return out


for level in levels:
    d = os.path.join(root, level, 'maps')
    texts = {f: open(os.path.join(d, f), encoding='latin-1').read().replace('\r', '')
             for f in os.listdir(d) if f.startswith(level) and f.endswith('.gsc')}
    funcs = {}
    for f, t in texts.items():
        for name, span in functions(t).items():
            funcs[name] = (f, span)
    main = texts.get(level + '.gsc', '')
    starts = re.findall(r'^\s*add_start\(\s*"([^"]+)"\s*,\s*::([A-Za-z0-9_]+)', main, re.M)
    start_funcs = {sf for _, sf in starts}
    callers, set_elsewhere = {}, set(trigger_flags(level))
    for name, (f, (a, b)) in funcs.items():
        if name in start_funcs:
            continue
        body = texts[f][a:b]
        for m in CALL.finditer(body):
            callee = m.group(1) or m.group(2)
            if callee != name:
                callers.setdefault(callee, set()).add(name)
        set_elsewhere.update(FLAG_SET.findall(body))
    print('=== ' + level)
    prev_flags = set()
    for start, sf in starts:
        if sf not in funcs:
            print('  %-22s %-28s (start function not found)' % (start, sf))
            continue
        f, (a, b) = funcs[sf]
        body = texts[f][a:b]
        picked = None
        for m in CALL.finditer(body):
            callee = m.group(1) or m.group(2)
            if callee in funcs and callee not in start_funcs and callee.lower() not in SKIP and callers.get(callee):
                picked = callee
        flags = FLAG_SET.findall(body)
        new = [fl for fl in flags if fl not in prev_flags and fl in set_elsewhere]
        choice = ('flag ' + new[-1]) if new else (('handler ' + picked) if picked else '-')
        print('  %-22s %-26s -> %-46s (new flags: %s)' % (start, sf, choice, ', '.join(new) or '-'))
        prev_flags = set(flags)
