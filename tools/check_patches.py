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
        end = text.find('\n@@@ ', eol + 1)
        if end < 0:
            return
        yield kind, text[eol + 1:end]
        pos = end + 1


def apply(src, patch):
    results = []
    it = blocks(patch)
    for kind, body in it:
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
            rel = os.path.relpath(os.path.join(dirpath, f[:-6]), pdir).replace('\\', '/')
            patch = open(os.path.join(dirpath, f), encoding='latin-1').read().replace('\r', '')
            found = [z for z in sorted(os.listdir(gsc_root)) if os.path.isfile(os.path.join(gsc_root, z, rel))]
            if not found:
                print(f'{rel}: no extracted copy found')
                bad += 1
                continue
            src = open(os.path.join(gsc_root, found[0], rel), encoding='latin-1').read().replace('\r', '')
            _, results = apply(src, patch)
            print(f'{rel} (from {found[0]}):')
            for status, line in results:
                print(f'  {status:10} {line}')
                bad += status != 'ok' and not status.startswith('ok ')
    print('ALL HUNKS OK' if bad == 0 else f'{bad} PROBLEM(S)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
