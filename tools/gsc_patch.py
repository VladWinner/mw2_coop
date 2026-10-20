"""Shared by the gen_*_patches.py generators: build an mw2coop patch file from the extracted game
script, taking every replaced text from that script so it matches byte for byte, and applying
each hunk as the DLL does (src/scripts.c) so later hunks see the text earlier ones left."""
import os
import re


def func(src, name):
    """The whole top-level function, header to closing brace."""
    i = src.index('\n' + name + '(') + 1
    j = src.index('\n}\n', i) + 3
    return src[i:j]


class Patch:
    def __init__(self, gsc_root, rel, out_dir):
        self.src = open(os.path.join(gsc_root, rel), encoding='latin-1').read().replace('\r', '')
        self.out_dir = out_dir
        self.out = []

    def note(self, text):
        self.out.append('\n' + text.strip('\n') + '\n')

    def func(self, name):
        return func(self.src, name)

    def subst(self, fn, old, new):
        """`@@@ subst`: whole-word replace inside one function."""
        body = func(self.src, fn)
        lead = r'(?<![A-Za-z0-9_])' if re.match(r'\w', old[0]) else ''
        trail = r'(?![A-Za-z0-9_])' if re.match(r'\w', old[-1]) else ''
        body2, n = re.subn(lead + re.escape(old) + trail, lambda m: new, body)
        assert n, (fn, old)
        self.src = self.src.replace(body, body2, 1)
        self.out.append('@@@ subst ' + fn + '\n' + old + '\n@@@ to\n' + new + '\n@@@ end\n')

    def replace(self, old, new):
        """`@@@ replace`: text that occurs exactly once in the script."""
        assert self.src.count(old) == 1, (self.src.count(old), old[:80])
        self.src = self.src.replace(old, new)
        self.out.append('@@@ replace\n' + old + '@@@ with\n' + new + '@@@ end\n')

    def write(self, name, header):
        path = os.path.join(self.out_dir, name)
        open(path, 'w', encoding='utf-8', newline='\n').write(header.strip('\n') + '\n' + ''.join(self.out))
        print('wrote', path, sum(1 for x in self.out if x.startswith('@@@')), 'hunks')
