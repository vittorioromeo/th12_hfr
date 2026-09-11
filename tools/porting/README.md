# Porting tools

Scripts used to take the runtime from TH12 to TH13 (TH13_DEVNOTES.md §7). They are research
aids, not part of the build; they expect files you produce yourself and never ship: an
`objdump -D -M intel` listing of each executable's `.text`, and a Ghidra headless
decompilation dump (`// ==== FUNCTION FUN_xxx @ addr size=n` headers, one function each).
No game executable, listing or decompilation belongs in the repository.

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
