"""Writes docs/settings-reference.md: every settings.ini key the mod reads, with its default and a description.

  python -I tools/gen_settings_reference.py            writes the file
  python -I tools/gen_settings_reference.py --check    exits 1 if the file is out of date

The keys are found in the sources: cfg("key", default) / CFG_ONCE("key", default) in native.c ([native]) and GetPrivateProfileIntA/StringA("section", "key", default, ...)
everywhere else. The description comes from, in this order:
  1. tools/settings_notes.txt          a line "section.key text", written by hand
  2. content/release/settings.ini      the comment above the key in the shipped file (also when the key is commented out there)
  3. a comment in the sources that names the key ("[native] key=1 ..."), with the sentences before it
  4. a comment on the line that reads the key, or just above it
  5. a generic line by name and default (marked as such in the file; read the source line for those)
"""
import collections
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
OUT = os.path.join(ROOT, "docs", "settings-reference.md")

CFG = re.compile(r'\b(cfg|CFG_ONCE)\(\s*"([A-Za-z0-9_]+)"\s*,\s*([^,)]*)')
INI = re.compile(r'GetPrivateProfile(Int|String)A\(\s*"([a-z0-9_]+)"\s*,\s*"([A-Za-z0-9_]+)"\s*,\s*("[^"]*"|[^,]*)')
TRAIL = re.compile(r'//\s*(.*)$')
NL = chr(10)
CR = chr(13)


def clean(text):
    text = re.sub(r"\s+", " ", text).strip()
    return text.replace("|", chr(92) + "|")


def read_lines(path):
    return open(path, encoding="utf-8", errors="replace").read().replace(CR + NL, NL).split(NL)


def comment_for(lines, i):
    """The comment next to line i (0-based): its own trailing comment, else the // lines directly above."""
    m = TRAIL.search(lines[i])
    if m and m.group(1).strip():
        return clean(m.group(1))
    above = []
    j = i - 1
    while j >= 0 and lines[j].strip().startswith("//"):
        above.insert(0, lines[j].strip()[2:].strip())
        j -= 1
    return clean(" ".join(above)) if above else ""


def collect():
    keys = collections.OrderedDict()
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True)):
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        lines = read_lines(path)
        for i, line in enumerate(lines):
            if line.strip().startswith("//"):
                continue
            hits = [("native", m.group(2), m.group(3).strip()) for m in CFG.finditer(line)] if rel.endswith("native.c") else []
            hits += [(m.group(2), m.group(3), m.group(4).strip()) for m in INI.finditer(line)]
            for section, key, default in hits:
                e = keys.setdefault((section, key), {"defaults": [], "comment": "", "where": []})
                if default not in e["defaults"]:
                    e["defaults"].append(default)
                c = comment_for(lines, i)
                if c and len(c) > len(e["comment"]):
                    e["comment"] = c
                if len(e["where"]) < 3 and "%s:%d" % (rel, i + 1) not in e["where"]:
                    e["where"].append("%s:%d" % (rel, i + 1))
    return keys


def ini_keys():
    """{(section, key): the comment above it in the shipped settings.ini}; a commented-out key right after another shares its comment."""
    out = {}
    section = ""
    block = []
    for line in open(os.path.join(ROOT, "content", "release", "settings.ini"), encoding="utf-8", errors="replace"):
        raw = line.strip()
        t = raw.lstrip(";").strip()
        m = re.match(r"\[([a-z0-9_]+)\]$", raw)
        if m:
            section = m.group(1)
            block = []
            continue
        m = re.match(r"([A-Za-z0-9_]+)=", t)
        if m and section and re.match(r"[A-Za-z0-9_]+=", t):
            out[(section, m.group(1))] = clean(" ".join(block))
            if not raw.startswith(";"):
                block = []
            continue
        if raw.startswith(";"):
            block.append(t)
        else:
            block = []
    return out


def mentions(keys):
    """For each key, a comment in the sources that names it as `[section] key` or `key=`, with the sentences before the mention."""
    found = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.[ch]"), recursive=True)):
        lines = read_lines(path)
        i = 0
        while i < len(lines):
            if "//" not in lines[i]:
                i += 1
                continue
            j = i
            block = []
            while j < len(lines) and "//" in lines[j]:
                block.append(lines[j].split("//", 1)[1].strip())
                j += 1
            text = " ".join(block)
            for section, key in keys:
                if (section, key) in found:
                    continue
                m = re.search(r"(\[%s\]\s*%s\b|\b%s=)" % (re.escape(section), re.escape(key), re.escape(key)), text)
                if not m:
                    continue
                start = text.rfind(". ", 0, m.start())
                start = start + 2 if start >= 0 and m.start() - start < 260 else (0 if m.start() < 260 else m.start() - 120)
                piece = text[start:m.end() + 260]
                if m.end() + 260 < len(text):
                    cut = max(piece.rfind(". "), piece.rfind("; "))
                    piece = piece[:cut + 1] if cut > 120 else piece + " ..."
                found[(section, key)] = clean(piece)
            i = j
    return found


def notes():
    out = {}
    path = os.path.join(HERE, "settings_notes.txt")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.rstrip(NL + CR)
            if not line.strip() or line.startswith("#"):
                continue
            k, _, text = line.partition(" ")
            section, _, key = k.partition(".")
            out[(section, key)] = clean(text)
    return out


def generic(section, key, defaults):
    d = defaults[0] if defaults else ""
    if key.endswith(("log", "stats", "count")):
        return "Diagnostic: writes extra lines about this to the log (testing; no effect on play)."
    if d == "0":
        return "Testing switch, off by default (see the source line)."
    if d == "1":
        return "Switch of one of the split screen's fixes, on by default; 0 turns it off (testing; see the source line)."
    return "Internal setting (see the source line)."


def render():
    keys = collect()
    shipped = ini_keys()
    ment = mentions(list(keys))
    hand = notes()
    for (section, key), e in keys.items():
        e["source"] = "code" if e["comment"] else ""
        if (section, key) in ment:
            e["comment"], e["source"] = ment[(section, key)], "code"
        if shipped.get((section, key)):
            e["comment"], e["source"] = shipped[(section, key)], "ini"
        if (section, key) in hand:
            e["comment"], e["source"] = hand[(section, key)], "note"
        if not e["comment"]:
            e["comment"], e["source"] = generic(section, key, e["defaults"]), "generic"
    by_section = collections.OrderedDict()
    for (section, key), e in sorted(keys.items()):
        by_section.setdefault(section, []).append((key, e))
    total = len(keys)
    described = sum(1 for e in keys.values() if e["source"] != "generic")
    out = ["# Settings reference", "",
           "Generated by `tools/gen_settings_reference.py` from the sources: every key of `settings.ini` that the mod reads, with its default and a description (the shipped `settings.ini`'s own",
           "comment, a hand-written note in `tools/settings_notes.txt`, or the comment in the code that names the key). The keys the shipped `settings.ini` lists are marked **ini**; most of the",
           "rest are switches for testing and measuring (log more, force a path, skip a step) and are not meant for players. `[native]` is the split screen's section. A key is read at the start",
           "of the game or of a session (a few more often); change it in the file with the game closed.", "",
           "%d keys; %d have a description from `settings.ini`, a note or a comment in the code, the other %d a generic line (read the source line for those)." % (total, described, total - described), ""]
    for section, items in by_section.items():
        out += ["## [%s]" % section, "", "| Key | Default | What it does | Source |", "|---|---|---|---|"]
        for key, e in items:
            mark = " **ini**" if (section, key) in shipped else ""
            out.append("| `%s`%s | %s | %s | %s |" % (key, mark, ", ".join("`%s`" % d for d in e["defaults"]), e["comment"], ", ".join("`%s`" % w for w in e["where"])))
        out.append("")
    return NL.join(out)


def main():
    text = render()
    if "--check" in sys.argv:
        old = open(OUT, encoding="utf-8").read().replace(CR + NL, NL) if os.path.exists(OUT) else ""
        if old != text:
            print("docs/settings-reference.md is out of date: run tools/gen_settings_reference.py")
            return 1
        return 0
    open(OUT, "w", encoding="utf-8", newline="").write(text)
    print("wrote", OUT, "-", text.count(NL + "| `"), "keys")
    return 0


if __name__ == "__main__":
    sys.exit(main())
