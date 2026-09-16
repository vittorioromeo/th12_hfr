#!/usr/bin/env python3
"""Compare literal thprac hooks with HFR's emitted patch plans (read-only).

Example, from the HFR root after running test.ps1/test.sh:
    python tools/check_thprac_overlap.py /path/to/thprac build/tests/*.patches.json

This is a source audit, not a C++ parser or a compatibility test. It includes
optional/disabled patches, ignores preprocessor conditions, and cannot discover
arbitrary runtime writes, replaced control flow, IAT/vtable chains or ECL edits.
Recognised declarations that cannot be parsed are reported, not silently counted
as safe. See docs/MOD_COMPATIBILITY.md for the required semantic audit.
"""

import argparse
from dataclasses import dataclass
import glob
import json
from pathlib import Path
import re


@dataclass(frozen=True)
class Site:
    addr: int
    write: int
    span: int
    label: str
    line: int


# Preserve strings and line numbers while removing comments.
COMMENTS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
HEX = r"0[xX][0-9a-fA-F]+"
START = re.compile(r"\b(?:EHOOK_(?:DY|ST|HK)|PATCH_(?:DY|ST|HK)|SetDpadHook)\s*\(|\.addr\s*=")
PATTERNS = [
    ("hook", re.compile(rf"EHOOK_(?:DY|ST)\s*\(\s*(?P<label>\w+)\s*,\s*(?P<addr>{HEX})\s*,\s*(?P<span>\d+)\s*,")),
    ("hook", re.compile(rf"(?P<label>EHOOK_HK)\s*\(\s*(?P<addr>{HEX})\s*,\s*(?P<span>\d+)\s*,")),
    ("hook", re.compile(rf"(?P<label>SetDpadHook)\s*\(\s*(?P<addr>{HEX})\s*,\s*(?P<span>\d+)\s*\)")),
    ("hook", re.compile(rf'\.addr\s*=\s*(?P<addr>{HEX})\s*,\s*\.name\s*=\s*"(?P<label>[^"]+)"\s*,\s*\.callback\s*=\s*\w+\s*,\s*\.data\s*=\s*PatchHookImpl\(\s*(?P<span>\d+)\s*\)')),
    ("patch", re.compile(rf'PATCH_(?:DY|ST)\s*\(\s*(?P<label>\w+)\s*,\s*(?P<addr>{HEX})\s*,\s*"(?P<bytes>[0-9a-fA-F\s]+)"\s*\)')),
    ("patch", re.compile(rf'(?P<label>PATCH_HK)\s*\(\s*(?P<addr>{HEX})\s*,\s*"(?P<bytes>[0-9a-fA-F\s]+)"\s*\)')),
]
# These direct x86 pointer writes are outside the hook macros. Match their
# actual source form rather than hard-coding addresses from a particular commit.
FPS_WRITE = re.compile(rf"\*\(double\s*\*\s*\*\)\s*(?P<addr>{HEX})\s*=\s*&mOptCtx\.fps_dbl\s*;")


def collect(path):
    raw = path.read_text(encoding="utf-8-sig")
    source = COMMENTS.sub(lambda m: re.sub(r"[^\n]", " ", m[0]) if m[0].startswith("/") else m[0], raw)
    sites, matched = [], set()
    for kind, pattern in PATTERNS:
        for m in pattern.finditer(source):
            size = int(m["span"]) if kind == "hook" else len(bytes.fromhex(m["bytes"]))
            sites.append(Site(int(m["addr"], 16), 1 if kind == "hook" else size,
                              size, m["label"], source.count("\n", 0, m.start()) + 1))
            matched.add(m.start())
    for m in FPS_WRITE.finditer(source):
        sites.append(Site(int(m["addr"], 16), 4, 4, "FPS operand write",
                          source.count("\n", 0, m.start()) + 1))
    missed = [source.count("\n", 0, m.start()) + 1 for m in START.finditer(source) if m.start() not in matched]
    return sorted(sites, key=lambda s: (s.addr, s.line)), missed


def overlaps(a, size, b, other_size):
    return a < b + other_size and b < a + size


def audit(plan_path, source_root):
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    game = re.match(r"TH(10|11|12|13)\b", plan["game"])
    if not game:
        raise ValueError(f"Unsupported audit profile: {plan['game']}")
    source = source_root / f"thprac_th{game[1]}.cpp"
    sites, missed = collect(source)
    # The emitted plan includes import-table entries as well as code patches.
    # Keep them all: only identical addresses in the game image can intersect.
    writes = plan["patches"]
    checks = [("write/write", writes, "write"),
              ("frozen signature/write", plan.get("signatures", []), "write"),
              ("frame-loop guard/write", plan.get("conflicts", []), "write"),
              ("write/instruction dependency", writes, "span")]
    print(f"\n{plan['game']}: {len(sites)} literal thprac sites, {len(writes)} HFR writes")
    count = 0
    for label, ranges, field in checks:
        hits = [(a, n, s) for a, n in ranges for s in sites
                if overlaps(a, n, s.addr, getattr(s, field))
                and (field != "span" or not overlaps(a, n, s.addr, s.write))]
        count += len(hits)
        print(f"  {label}: {len(hits)}")
        for a, n, s in hits:
            print(f"    HFR 0x{a:08x}+{n} / thprac 0x{s.addr:08x}+{getattr(s, field)}"
                  f" {s.label} ({source.name}:{s.line})")
    for line in missed:
        print(f"  UNPARSED declaration: {source.name}:{line}")
    if not count:
        print("  No intersections among scanned ranges; this does NOT establish compatibility.")
    return len(missed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("thprac", type=Path, help="root of the upstream thprac checkout")
    parser.add_argument("plans", nargs="+", help="HFR patch-plan JSON paths or glob patterns")
    args = parser.parse_args()
    source_root = args.thprac / "thprac" / "src" / "thprac"
    paths = []
    for pattern in args.plans:
        matches = glob.glob(pattern)
        if not matches:
            parser.error(f"No patch plan matches {pattern!r}")
        paths.extend(Path(p) for p in matches)
    missed = 0
    for path in sorted(set(paths)):
        missed += audit(path, source_root)
    print("\nScope: literal hook declarations, SetDpadHook, and FPS operand writes in four game files.")
    print("Optional patches are included. Arbitrary writes and control-flow conflicts need manual review.")
    print("Instruction dependency counts exclude intersections already listed as write/write.")
    return 2 if missed else 0  # zero means the scoped audit ran, NOT that co-loading is safe


if __name__ == "__main__":
    raise SystemExit(main())
