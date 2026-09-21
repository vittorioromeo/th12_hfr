"""Generate tools/th*_signatures.json from src/games/th*_signatures.h.

The frozen signatures are written down once, in the header the runtime compiles. The JSON
beside this script is what the release's standalone identifier (verify_game.py) reads, and
until this generator existed it was a second hand-maintained copy of the same 300-odd
addresses -- the kind of duplicate that stays right until the day it does not.

  python tools/gen_signature_json.py            rewrite the JSON files
  python tools/gen_signature_json.py --check    fail if the committed JSON has drifted

The trailing comment on a header entry becomes its "description".
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAMES = ("08", 10, 11, 12, 13, 14, 15, 20)
ENTRY = re.compile(
    r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*\d+\s*,\s*\{([^}]*)\}\s*\}\s*,?"   # {addr, size, {bytes}}
    r"[ \t]*(?:/\*\s*(?P<comment>.*?)\s*\*/)?",                       # optional trailing comment
    re.S)


def read_header(game):
    text = (ROOT / f"src/games/th{game}_signatures.h").read_text(encoding="utf-8")
    out = []
    for m in ENTRY.finditer(text):
        data = [b.strip() for b in m.group(2).split(",") if b.strip()]
        entry = {"address": m.group(1).lower(),
                 "bytes": " ".join(f"{int(b, 16):02x}" for b in data)}
        if m.group("comment"):
            entry["description"] = " ".join(m.group("comment").split())
        out.append(entry)
    if not out:
        raise SystemExit(f"th{game}_signatures.h: no entries parsed")
    return out


def main():
    check = "--check" in sys.argv[1:]
    stale = []
    for game in GAMES:
        path = ROOT / f"tools/th{game}_signatures.json"
        text = json.dumps(read_header(game), indent=1) + "\n"
        if check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.write_text(text, encoding="utf-8")
    if check and stale:
        print("stale, regenerate with tools/gen_signature_json.py: " + ", ".join(stale))
        return 1
    print("signature JSON matches the headers" if check else "wrote the signature JSON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
