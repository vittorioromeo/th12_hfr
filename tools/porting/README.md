# Porting tools

Scripts used to take the runtime from TH12 to TH13 (TH13_DEVNOTES.md §7). They are research
aids, not part of the build; they expect files you produce yourself and never ship: an
`objdump -D -M intel` listing of each executable's `.text`, and a Ghidra headless
decompilation dump (`// ==== FUNCTION FUN_xxx @ addr size=n` headers, one function each).
No game executable, listing or decompilation belongs in the repository.

`inspect_pe.py` also supports **AMD64 PE32+** games. Install `pefile` and `capstone` in your
Python environment. It opens executables read-only and accepts either a preferred VA
(`0x14003c330`) or an RVA (`rva:0x3c330`):

```powershell
python inspect_pe.py 'path\to\th06nc.exe' info --imports
python inspect_pe.py 'path\to\th06nc.exe' strings 'DxLib|fps|D3D11CreateDevice'
python inspect_pe.py 'path\to\th06nc.exe' xrefs rva:0x30c9e8 rva:0x3c330
python inspect_pe.py 'path\to\th06nc.exe' disasm rva:0x11562 rva:0x1156d
```

`info` emits JSON with hashes, sections and optional import/IAT RVAs. String searches cover
ASCII and the ASCII subset of UTF-16LE; strings over 240 characters are skipped by default
(`--max-length` changes this) to avoid dumping embedded shader blobs. `xrefs` scans executable
sections for instruction immediates and absolute/RIP-relative memory operands. It does not
resolve register-indirect calls, pointer tables, or arbitrary control flow; linear decoding
can miss references or interpret embedded data as code — **for AMD64 use `xrefs64.py` below
instead; this warning was in the file all along and was still not enough to stop two wrong
conclusions being built on `xrefs` output.** The AMD64 `unwind_begin_rva` comes
from `.pdata`, whose entries may be function fragments rather than complete functions.
Start disassembly on a known instruction boundary; an arbitrary byte offset is not reliable.
See [TH06NC_DEVNOTES.md](../../TH06NC_DEVNOTES.md) for the investigation using this tool.

- `xrefs64.py <exe> <rva> [...]` — **AMD64 xrefs, decoded from real function boundaries.**
  Prefer this to `inspect_pe.py xrefs`, which decodes executable sections linearly and so
  misaligns wherever data or padding sits between functions. That misalignment is not
  theoretical: it invented four apparent readers of one byte, and a gate built on them
  silently disabled two features for several builds. Worse, when the same linear scan was
  then used to look for *callers* of those functions and found none, three live node
  callbacks were written up as dead code -- an absence of references found by a linear
  decode is exactly as worthless as a reference found by one (TH06NC_DEVNOTES §17). This one walks `.pdata`, starts each function at its own
  `BeginAddress`, and says whether each hit is a read or a write. It cannot see accesses made
  through a register (`[rbx+0x774c]` on an object pointer), which is the other way an address
  scan lies — §16 and §18 were both bitten by that. **A reference found by a scan is a
  candidate; something reachable has to read it before you build on it.**
- `disasm.py <exe> <start> <end>` — disassemble a code range straight from the executable
  (objdump on the raw bytes, VA-adjusted). The everyday tool.
- `scan_registrations.py <exe> <helper VA>` — every UpdateFunc registration with priority and
  callback: the node table for a profile's `classes[]`.
- `match.py` — map an old-game address to the new game by matching sequences of normalised
  instructions (`OLD.asm` / `NEW.asm`, cached in `asm.pkl`). `best(addr)` for one site,
  `via_ref(F)` to find what a global/function became by matching the sites that reference it.
  Finds thunks and helpers reliably; sites inside large update functions mostly do not match.
- `dmatch.py` — match functions by shingle similarity of their normalised decompiled bodies
  (`OLD_decomp.c` / `NEW_decomp.c`, cached in `dmatch.pkl`). Narrows a hook to a function;
  read the function from there.
