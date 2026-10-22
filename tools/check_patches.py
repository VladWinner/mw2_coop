"""Apply mw2coop patches to extracted zone scripts the same way the DLL does (src/scripts.c)
and report every hunk. Usage: python -I tools/check_patches.py <content dir> <extracted gsc root>
The extracted root holds one folder per zone (common/, trainer/, ...) as written by
ff_rawfiles.py; a script is looked up in every zone folder."""
import os
import sys


def blocks(text):
    """Yield (kind, body) like next_block() in scripts.c."""
    pos = 0
    while True:
        at = text.find('@@@ ', pos)
        if at < 0:
            return
        eol = text.find('\n', at)
        if eol < 0:
            return
        kind = text[at + 4:eol]
        end = text.find('\n@@@ ', eol)  # from eol: an empty body ends right there
        if end < 0:
            return
        yield kind, text[eol + 1:end]
        pos = end + 1


def is_ident(c):
    return c.isalnum() or c == '_'


def find_function(src, name):
    """(start, end) of the top-level function, like find_function() in scripts.c, or None."""
    p = src.find(name)
    while p >= 0:
        q = p + len(name)
        if (p == 0 or src[p - 1] == '\n') and not (q < len(src) and is_ident(src[q])):
            while q < len(src) and src[q] in ' \t':
                q += 1
            if q < len(src) and src[q] == '(':
                c = src.find('{', q)
                depth = 0
                while 0 <= c < len(src):
                    if src.startswith('//', c):
                        c = src.find('\n', c)
                        if c < 0:
                            return None
                    elif src.startswith('/*', c):
                        c = src.find('*/', c + 2)
                        if c < 0:
                            return None
                        c += 1
                    elif src[c] == '"':
                        c += 1
                        while c < len(src) and src[c] != '"':
                            c += 2 if src[c] == '\\' else 1
                    elif src[c] == '{':
                        depth += 1
                    elif src[c] == '}':
                        depth -= 1
                        if depth == 0:
                            return p, c + 1
                    c += 1
                return None
        p = src.find(name, p + 1)
    return None


def subst(src, name, old, new):
    """Whole-word replace inside one function, like subst_in_function() in scripts.c."""
    span = find_function(src, name)
    if not span or not old:
        return src, 0
    start, end = span
    lead, trail = is_ident(old[0]), is_ident(old[-1])
    out, i, count = [], start, 0
    while i < end:
        if (src.startswith(old, i) and i + len(old) <= end
                and not (lead and i > 0 and is_ident(src[i - 1]))
                and not (trail and i + len(old) < len(src) and is_ident(src[i + len(old)]))):
            out.append(new)
            i += len(old)
            count += 1
        else:
            out.append(src[i])
            i += 1
    return src[:start] + ''.join(out) + src[end:], count


def apply(src, patch):
    results = []
    it = blocks(patch)
    for kind, body in it:
        if kind.startswith('subst '):
            kind2, to = next(it, (None, None))
            if kind2 != 'to':
                results.append(('malformed', body[:60]))
                break
            src, n = subst(src, kind[6:], body, to)
            results.append(('ok' if n else 'FAILED x0', f'subst in {kind[6:]}: {body[:40]} ({n})'))
            continue
        if kind == 'replace':
            kind2, with_ = next(it, (None, None))
            if kind2 != 'with':
                results.append(('malformed', body[:60]))
                break
            n = src.count(body)
            if n != 1:
                results.append((f'FAILED x{n}', body.splitlines()[0][:70] if body else ''))
                continue
            src = src.replace(body, with_, 1)
            results.append(('ok', body.splitlines()[0][:70]))
        elif kind == 'append':
            src = src + '\n' + body + '\n'
            results.append(('ok append', body.splitlines()[0][:70] if body else ''))
    return src, results


def main(content, gsc_root):
    pdir = os.path.join(content, 'patches')
    bad = 0
    for dirpath, _, files in os.walk(pdir):
        for f in files:
            if not f.endswith('.patch'):
                continue
            if f.endswith('.cp.patch'):
                if os.path.exists(os.path.join(dirpath, f[:-9] + '.patch')):
                    continue	# applied after the script's own patch, below
                f = f[:-9] + '.patch'	# a script with only checkpoint hooks
            rel = os.path.relpath(os.path.join(dirpath, f[:-6]), pdir).replace('\\', '/')
            main_patch = os.path.join(dirpath, f)
            patch = open(main_patch, encoding='latin-1').read().replace('\r', '') if os.path.exists(main_patch) else ''
            found = [z for z in sorted(os.listdir(gsc_root)) if os.path.isfile(os.path.join(gsc_root, z, rel))]
            if not found:
                print(f'{rel}: no extracted copy found')
                bad += 1
                continue
            src = open(os.path.join(gsc_root, found[0], rel), encoding='latin-1').read().replace('\r', '')
            src, results = apply(src, patch)
            cp = os.path.join(dirpath, f[:-6] + '.cp.patch')
            if os.path.exists(cp):	# the checkpoint hooks go on after the script's own patch
                _, more = apply(src, open(cp, encoding='latin-1').read().replace('\r', ''))
                results += [(status, 'cp: ' + line) for status, line in more]
            print(f'{rel} (from {found[0]}):')
            for status, line in results:
                print(f'  {status:10} {line}')
                bad += status != 'ok' and not status.startswith('ok ')
    print('ALL HUNKS OK' if bad == 0 else f'{bad} PROBLEM(S)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
