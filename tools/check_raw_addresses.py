#!/usr/bin/env python3
"""Fails when code of the mod uses a raw offset into the game's exe instead of XR() (docs/address-resolution.md).

  python -I tools/check_raw_addresses.py

A build whose addresses moved is only handled if every address the code uses goes through XR() / XRA() / XV() (the resolver); a literal such as `base + 0x1bab60` would still point at
the reference build's place. The check reads, for the 64-bit build, the lines of src/*.c and src/*.h it compiles (src/dx is the renderer, guarded as a whole by its build hash:
src/dx/natsites_gen.h), and for the 32-bit build the lines of the files build.sh gives it (src/x86 included) and of the headers; it looks for `<base> + 0x<4 or more hex digits>`,
`<base>[0x...]`, a cast of a 32-bit VA (`(int *)0x738c6c`) and `<base> + NAME` where NAME is a `#define` of a raw number. A line that has a good reason carries `raw-ok: <why>` in a
comment (image bounds, a scan window, an address of the iw4x-sp 1.0.159 image in code that runs on that client only, QA diagnostics that check the bytes they hook). Run by
tools/pr_checks.py.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SRC = os.path.join(ROOT, "src")
PAT = re.compile(r"\b(?:base|exe|image_base|exe_base)\s*\)?\s*[+\[]\s*0x[0-9a-fA-F]{4,}")
PATVA = re.compile(r"\(\s*(?:volatile\s+)?(?:const\s+)?[a-z_0-9]+\s*\*+\s*\)\s*\(?\s*0x[4-9a-fA-F][0-9a-fA-F]{5}\b")
DEF = re.compile(r"\s*#\s*define\s+(\w+)\s+\(?(0x[0-9a-fA-F]{4,})u?\)?\s*(//.*)?$")
SKIP = ("addr159.c", "settings_template_gen.h")
NL = "\n"


def compiled_lines(text, want64):
    """For each line: whether the 64-bit build (want64) or the 32-bit build compiles it, by the #if / #ifdef _WIN64 nesting."""
    out, stack = [], []
    for line in text.split(NL):
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
        if m:
            k, rest = m.group(1), m.group(2)
            if k in ("ifdef", "ifndef", "if"):
                neg = k == "ifndef" or "!defined(_WIN64)" in rest
                stack.append(["_WIN64" in rest, neg, False])
            elif k in ("elif", "else"):
                stack[-1][2] = True
            elif k == "endif" and stack:
                stack.pop()
            out.append(False)
            continue
        if want64:
            other = any(is64 and ((not els) == neg) for is64, neg, els in stack)  # a branch only the 32-bit build compiles
        else:
            other = any(is64 and ((not els) != neg) for is64, neg, els in stack)  # a branch only the 64-bit build compiles
        out.append(not other)
    return out


def files32():
    build = open(os.path.join(ROOT, "build.sh"), encoding="utf-8").read()
    line = [l for l in build.split(NL) if "main32.c" in l][0]
    out = sorted(set(os.path.join(ROOT, p) for p in re.findall(r"src/[A-Za-z0-9_/]+\.c", line)))
    return out + glob.glob(os.path.join(SRC, "*.h")) + glob.glob(os.path.join(SRC, "x86", "*.h"))


def read_lines(paths, want64):
    out = {}
    for path in paths:
        if os.path.basename(path) in SKIP or not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8", errors="replace").read().replace("\r" + NL, NL)
        flags = compiled_lines(text, want64)
        out[path] = [(n, l) for n, l in enumerate(text.split(NL), 1) if flags[n - 1]]
    return out


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def scan(texts, pats):
    bad, defs = [], {}
    for path, lines in texts.items():
        for n, line in lines:
            if "raw-ok" in line:
                continue
            code = line.split("//")[0]
            if any(p.search(code) for p in pats):
                bad.append("%s:%d: %s" % (rel(path), n, line.strip()[:130]))
            m = DEF.match(line)
            if m:
                defs[m.group(1)] = (path, n)
    # the same bypass behind a name: <base> + NAME where NAME is a #define of a raw number
    for path, lines in texts.items():
        for n, line in lines:
            if "raw-ok" in line:
                continue
            code = line.split("//")[0]
            for name, (dp, dn) in defs.items():
                if name in code and re.search(r"\b(?:base|exe|image_base|exe_base)\s*\)?\s*\+\s*\(?\s*" + name + r"\b", code):
                    bad.append("%s:%d: %s (a raw number, defined at %s:%d)" % (rel(path), n, name, os.path.basename(dp), dn))
    return bad


def main():
    paths64 = sorted(glob.glob(os.path.join(SRC, "*.c")) + glob.glob(os.path.join(SRC, "*.h")))
    bad = scan(read_lines(paths64, True), (PAT,))
    bad32 = scan(read_lines(files32(), False), (PAT, PATVA))
    if bad or bad32:
        if bad:
            print("raw offsets into the exe in 64-bit code (use XR(), or say why with `raw-ok: ...` in a comment):")
            for b in bad:
                print("  " + b)
        if bad32:
            print("raw offsets or VAs of the exe in 32-bit code (use XR() / XV(), or say why with `raw-ok: ...`):")
            for b in bad32:
                print("  " + b)
        return 1
    print("no raw exe offsets in the 64-bit or the 32-bit code")
    return 0


sys.exit(main())
