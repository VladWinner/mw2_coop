"""qa_split.sh: puts (or removes) a made-up pad in the [splitscreen] section of a copy's settings.ini."""
import os
import re
import sys

path, fake = sys.argv[1], sys.argv[2]
text = open(path).read() if os.path.exists(path) else ""
text = re.sub(r"\n?\[splitscreen\].*", "", text, flags=re.S).rstrip()
if fake:
    text += "\n\n[splitscreen]\nfakepad=1"
open(path, "w").write(text + "\n")
