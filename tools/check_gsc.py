#!/usr/bin/env python3
"""Fails on GSC the game's compiler does not take, in the mod's scripts and in the new text of its patches.

  python -I tools/check_gsc.py

The game does not say which line failed: a script that does not compile ends the level load and drops the game back to the menu (the log then shows "scrdebug: 0 script
functions mapped" and no level). What is checked: a `?` outside strings and comments (GSC has no `a ? b : c`; 2026-11-17 a QA scenario's ternary stopped every co-op
level from loading), and the bracket balance of each file. Run by tools/pr_checks.py.
"""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def code_only(text):
    """The text with strings and comments blanked (newlines kept, so line numbers stay)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == chr(92) else 1
            j = min(j + 1, n)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def check_text(name, text, bad, balance=True):
    code = code_only(text)
    for n, line in enumerate(code.split("\n"), 1):
        if "?" in line:
            bad.append("%s:%d: '?' outside a string (GSC has no a ? b : c)" % (name, n))
    if balance:
        for o, c in ("()", "[]", "{}"):
            if code.count(o) != code.count(c):
                bad.append("%s: %d '%s' against %d '%s'" % (name, code.count(o), o, code.count(c), c))


def patch_new_texts(text):
    """The new texts of a patch file: what follows each "@@@ to" up to "@@@ end"."""
    return re.findall(r"^@@@ to\r?\n(.*?)^@@@ end", text, re.S | re.M)


REF = re.compile(r"\bmaps\\(\w+)::", re.I)


def level_reach(bad):
    """A script compiled on every level must not reach, through the mod's own scripts, a level's script (maps\\<level>::...): on any other level the compiler
    stops with "Could not find script 'maps/<level>'" and the level does not load (2026-11-17: a QA scenario called maps\\_mw2coop_oilrig::, which calls
    maps\\oilrig::, and every co-op level but Oil Rig failed). The roots: the mod's scripts every co-op level loads, and the mod's scripts the patches of the
    game's common scripts (maps/_*.gsc) call."""
    mod = {}
    for path in glob.glob(os.path.join(ROOT, "content", "scripts", "maps", "*.gsc")):
        name = os.path.splitext(os.path.basename(path))[0].lower()
        mod[name] = set(n.lower() for n in REF.findall(code_only(open(path, encoding="utf-8", errors="replace").read())))
    roots = {"_mw2coop", "_mw2coop_qa", "_mw2coop_dbg"} & set(mod)
    for path in glob.glob(os.path.join(ROOT, "content", "patches", "maps", "_*.patch")):
        for piece in patch_new_texts(open(path, encoding="utf-8", errors="replace").read()):
            roots |= set(n.lower() for n in REF.findall(code_only(piece))) & set(mod)
    for root in sorted(roots):
        seen, todo, via = {root}, [root], {root: root}
        while todo:
            f = todo.pop()
            for r in mod.get(f, ()):
                if not r.startswith("_"):
                    bad.append("content/scripts/maps/%s.gsc is compiled on every level (%s) and calls maps\\%s:: (the level's own script)" % (f, via[f], r))
                elif r in mod and r not in seen:
                    seen.add(r)
                    via[r] = via[f] + " -> " + r
                    todo.append(r)


def qa_stub(bad):
    """A session without a QA scenario gets a stub of maps/_mw2coop_qa.gsc that has only its entry points (src/scripts.c): a call from another script to a
    function the stub lacks is a compile error ("unknown function") on every level of every normal session (2026-11-18: maps\\_mw2coop_qa::bbcheck from
    _mw2coop.gsc, caught by a QA run before it was pushed)."""
    src = open(os.path.join(ROOT, "src", "scripts.c"), encoding="utf-8", errors="replace").read()
    m = re.search(r"static const char stub\[\] = (.*?);\n", src, re.S)
    if not m:
        bad.append("src/scripts.c: the QA stub text was not found")
        return
    text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))).replace("\\n", "\n").replace("\\t", "\t")
    have = set(re.findall(r"^(\w+)\s*\(", text, re.M))
    paths = glob.glob(os.path.join(ROOT, "content", "scripts", "**", "*.gsc"), recursive=True) + glob.glob(os.path.join(ROOT, "content", "patches", "**", "*.patch"), recursive=True)
    for path in paths:
        if os.path.basename(path).lower() == "_mw2coop_qa.gsc":
            continue
        code = code_only(open(path, encoding="utf-8", errors="replace").read())
        for fn in sorted(set(re.findall(r"\bmaps\\_mw2coop_qa::(\w+)", code, re.I))):
            if fn not in have:
                bad.append("%s calls maps\\_mw2coop_qa::%s, which the QA stub of src/scripts.c lacks (every level without a QA scenario would fail to compile)"
                           % (os.path.relpath(path, ROOT).replace(os.sep, "/"), fn))


def main():
    bad = []
    level_reach(bad)
    qa_stub(bad)
    for path in sorted(glob.glob(os.path.join(ROOT, "content", "scripts", "**", "*.gsc"), recursive=True)):
        check_text(os.path.relpath(path, ROOT).replace(os.sep, "/"), open(path, encoding="utf-8", errors="replace").read(), bad)
    for path in sorted(glob.glob(os.path.join(ROOT, "content", "patches", "**", "*.patch"), recursive=True)):
        name = os.path.relpath(path, ROOT).replace(os.sep, "/")
        for k, piece in enumerate(patch_new_texts(open(path, encoding="utf-8", errors="replace").read())):
            check_text("%s (new text %d)" % (name, k + 1), piece, bad, balance=False)
    if bad:
        print("GSC the game will not compile:")
        for b in bad:
            print("  " + b)
        return 1
    print("GSC checks passed (no '?' outside strings, brackets balanced)")
    return 0


sys.exit(main())
