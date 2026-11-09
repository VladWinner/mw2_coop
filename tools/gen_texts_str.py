#!/usr/bin/env python3
"""Writes content/release/localizedstrings/mw2coop.str, the readable file of the mod's menu and
HUD texts, from the tables in src/localize.c (the strings there are Windows-1251 escapes).

  python -I tools/gen_texts_str.py            write the .str file
  python -I tools/gen_texts_str.py --check    fail if the file is not what the source gives

The format is the one the Call of Duty engines (and IW4M / iw4x mods) use for localized strings:
VERSION "1", then per text REFERENCE <NAME> and LANG_ENGLISH / LANG_RUSSIAN lines, // comments.
The game reads every *.str in the mw2coop/localizedstrings folder at start (src/localize.c, load_str_files).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "localize.c")
OUT = os.path.join(HERE, "..", "content", "release", "localizedstrings", "mw2coop.str")
BS = chr(92)

HEADER = """// mw2coop texts: the words of the mod's menus, lobby and HUD, in the .str format of the Call of Duty
// engines (the same as the game's own localizedstrings). Edit a line and restart the game; a text
// you delete keeps the mod's built-in wording. UTF-8.
//
//   REFERENCE     the text's name as the game's scripts and menus know it
//   LANG_ENGLISH  the English text (also shown in every language but Russian)
//   LANG_RUSSIAN  the Russian text (shown when the game's language is Russian)
//
// Words like [{+activate}] are the game's own key hints: keep them as they are. A line break in a
// text is written as a backslash and the letter n.
VERSION             "1"
CONFIG              "mw2coop"
FILENOTES           ""
"""


def quote(text):
    return '"' + text.replace(BS, BS + BS).replace('"', BS + '"').replace(chr(10), BS + "n") + '"'


def c_literals(text):
    """The C string literals of one expression, joined (adjacent literals concatenate)."""
    out = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
        out.append(m.group(1))
    return "".join(out)


def unescape(s, encoding):
    """C escapes of a literal to bytes, then decoded."""
    data = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c != BS:
            data += c.encode("utf-8")
            i += 1
            continue
        i += 1
        c = s[i]
        if c == "x":
            j = i + 1
            while j < len(s) and j < i + 3 and s[j] in "0123456789abcdefABCDEF":
                j += 1
            data.append(int(s[i + 1:j], 16))
            i = j
            continue
        i += 1
        data += {"n": b"\n", "t": b"\t", BS: BS.encode(), '"': b'"', "'": b"'"}.get(c, c.encode())
    return data.decode(encoding)


def parse_strings(src):
    m = re.search(r"static const ModString strings\[\] = \{(.*?)\n\};", src, re.S)
    body = m.group(1)
    body = re.sub(r"//[^\n]*", "", body)  # the comments
    entries = []
    for e in re.finditer(r'\{\s*("MW2COOP_[A-Z0-9_]+")\s*,(.*?)\}\s*,', body, re.S):
        name = e.group(1).strip('"')
        parts = re.findall(r'(?:"(?:[^"\\]|\\.)*"\s*)+', e.group(2))
        if len(parts) != 2:
            raise SystemExit("cannot split the entry %s into ru and en (%d parts)" % (name, len(parts)))
        entries.append((name, unescape(c_literals(parts[0]), "cp1251"), unescape(c_literals(parts[1]), "utf-8")))
    return entries


def parse_fixed(src):
    m = re.search(r"static FixedString fixed\[\] = \{(.*?)\n\};", src, re.S)
    body = re.sub(r"//[^\n]*", "", m.group(1))
    out = []
    for e in re.finditer(r'\{\s*("[A-Z0-9_]+")\s*,(.*?)\}\s*,', body, re.S):
        name = e.group(1).strip('"')
        out.append((name, unescape(c_literals(e.group(2)), "cp1251")))
    return out


def build():
    src = open(SRC, encoding="utf-8", newline="").read()
    lines = [HEADER]
    for name, ru, en in parse_strings(src):
        lines.append("REFERENCE           " + name)
        lines.append("LANG_ENGLISH        " + quote(en))
        lines.append("LANG_RUSSIAN        " + quote(ru))
        lines.append("")
    fixed = parse_fixed(src)
    if fixed:
        lines.append("// The game's own texts the mod words differently in the Russian game (no English line: the English game keeps its own).")
        lines.append("")
        for name, ru in fixed:
            lines.append("REFERENCE           " + name)
            lines.append("LANG_RUSSIAN        " + quote(ru))
            lines.append("")
    return (chr(13) + chr(10)).join(lines)


def main():
    text = build()
    if "--check" in sys.argv:
        have = open(OUT, encoding="utf-8", newline="").read() if os.path.exists(OUT) else ""
        if have != text:
            print("content/release/localizedstrings/mw2coop.str is out of date: run python -I tools/gen_texts_str.py")
            return 1
        print("mw2coop.str matches src/localize.c")
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("wrote", os.path.normpath(OUT), text.count(chr(10) + "REFERENCE"), "texts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
