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
can miss references or interpret embedded data as code. The AMD64 `unwind_begin_rva` comes
from `.pdata`, whose entries may be function fragments rather than complete functions.
Start disassembly on a known instruction boundary; an arbitrary byte offset is not reliable.
See [TH06NC_DEVNOTES.md](../../TH06NC_DEVNOTES.md) for the investigation using this tool.

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
