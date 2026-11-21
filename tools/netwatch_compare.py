#!/usr/bin/env python3
"""Compare the state hash additions the two PCs wrote to their logs when the network co-op games went apart (src/netwatch.c, "netwatch: add" lines).

  python -I tools/netwatch_compare.py <host mw2coop.log> <client mw2coop.log>

Each log holds, after "the hash's additions of the steps after the frames A..B", one line per entity (type, number, health, position, angles) and one per other
addition, for the steps both PCs simulated. The additions are matched by level time and entity (or by the call site for the others); the output lists the
entities and values that differ, the first one first: that is where the two simulations went apart (players: ent 0 and 1; type 1 player, 9 vehicle, 13 actor)."""
import re
import sys

ENT = re.compile(r'netwatch: add t (-?\d+) ent (-?\d+) type (-?\d+) health (-?\d+) pos (\w+) (\w+) (\w+) \(([^)]*)\) angles (\w+) (\w+) (\w+) \(([^)]*)\)')
OTHER = re.compile(r'netwatch: add t (-?\d+) at (\w+) (.*)')
HEAD = re.compile(r'netwatch: (.*): the hash\'s additions of the steps after the frames (-?\d+)\.\.(-?\d+)')


def blocks(path):
    """The dumps of a log: [(who, from, to, entries)], entries = list of (key, text, line) in the order the hash took them."""
    out, cur = [], None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = HEAD.search(line)
        if m:
            cur = (m.group(1), int(m.group(2)), int(m.group(3)), [])
            out.append(cur)
            continue
        if cur is None:
            continue
        m = ENT.search(line)
        if m:
            t, ent = int(m.group(1)), int(m.group(2))
            val = 'type %s health %s pos %s %s %s angles %s %s %s' % (m.group(3), m.group(4), m.group(5), m.group(6), m.group(7), m.group(9), m.group(10), m.group(11))
            cur[3].append((('ent', t, ent), val, line.strip()))
            continue
        m = OTHER.search(line)
        if m:
            t, at = int(m.group(1)), m.group(2)
            n = sum(1 for k, _, _ in cur[3] if k[0] == 'add' and k[1] == t and k[2] == at)
            cur[3].append((('add', t, at, n), m.group(3), line.strip()))
    return out


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, b = blocks(sys.argv[1]), blocks(sys.argv[2])
    if not a or not b:
        print('no dump in %s' % (sys.argv[1] if not a else sys.argv[2]))
        return 1
    # the pair of dumps that cover the same steps
    for da in a:
        for db in b:
            ta = {k[1] for k, _, _ in da[3]}
            tb = {k[1] for k, _, _ in db[3]}
            common = ta & tb
            if not common:
                continue
            print('dump "%s" (%d..%d, %d additions) against "%s" (%d..%d, %d additions); level times in both: %s' % (da[0], da[1], da[2], len(da[3]), db[0], db[1], db[2], len(db[3]),
                                                                                              ' '.join(str(t) for t in sorted(common))))
            ma = {k: (v, l) for k, v, l in da[3] if k[1] in common}
            mb = {k: (v, l) for k, v, l in db[3] if k[1] in common}
            order = [k for k, _, _ in da[3] if k[1] in common]
            diffs = 0
            for k in order:
                if k not in mb:
                    print('  only in the first log:  %s' % ma[k][1])
                    diffs += 1
                elif ma[k][0] != mb[k][0]:
                    print('  differs:\n    first:  %s\n    second: %s' % (ma[k][1], mb[k][1]))
                    diffs += 1
                if diffs >= 20:
                    break
            for k in mb:
                if k not in ma and diffs < 20:
                    print('  only in the second log: %s' % mb[k][1])
                    diffs += 1
            print('  %d differences shown' % diffs)
    return 0


if __name__ == '__main__':
    sys.exit(main())
