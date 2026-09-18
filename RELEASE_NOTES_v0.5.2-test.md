**Touhou HFR and thcrap now work together**, on Steam and standalone copies of TH10–13 alike.

Everything else is unchanged from **v0.5.1-test**: no gameplay, scheduler, scaler, renderer or
New Classic code was touched, and a replay recorded on v0.5.1-test behaves the same here.

## What was wrong

With thcrap installed, this patch did nothing at all — and said it had installed successfully.

thcrap injects by letting the Windows loader finish and stopping the game's thread at the
executable's entry point, which is *after* this patch's DLL has loaded and installed. thcrap
then walks the game's import table, matches functions by name, overwrites whatever it finds,
and chains to `GetProcAddress(dll, func)` — the library's own function, not the pointer it
replaced. Every import this patch had hooked that thcrap also detours was therefore dropped
rather than chained to, `d3d9.dll!Direct3DCreate9` among them. That is how this patch obtains
the Direct3D device, so nothing else it does could happen either.

## The fix

- **Hooking an import now calls whatever the slot held**, whoever put it there, so a patch
  arriving afterwards is nested inside this one rather than discarded.
- **The imports are taken back once the game is running**, from a function the game calls
  before it asks for Direct3D and no translation patch has any reason to touch. The patch that
  took them ends up inside this one, and both run. The log says when this happens.
- The same trigger is now how a **Steam release is identified**, replacing the Direct3D import
  the previous version used — which was exactly the one thcrap takes away.

Nothing outside the game's own import table is modified. An earlier attempt at this redirected
the exporting library's export table instead; it worked, and it crashed Steam copies on the
first frame, because an export table belongs to the whole process and Steam's overlay is in it.

## Using both

Install thcrap and this patch as each normally wants, and start the game through thcrap. Order
does not matter, and neither modifies the other's files: this patch never changes the
executable on disk, so thcrap still identifies the game by hash exactly as it expects.

That the two can share a game at all is checked rather than assumed —
`tools/check_patch_overlap.py` compares thcrap's own game definitions against every byte this
patch writes and every byte it verifies, and for TH10, TH11, TH12 and TH13 the two sets are
disjoint.

One caveat: **thcrap's own Direct3D features do not engage while `d3d9ex=1`** (the default).
This patch creates the Direct3D object through 9Ex, which steps over whoever else is in that
chain, so thcrap's translation notes and its device-lost handling are skipped. Everything
thcrap does to text, fonts, images and files — which is where the translation lives — is
unaffected. Set `d3d9ex=0` if you want those extras, at the cost of `max_frame_latency`.

Executables that have already been translated on disk — the old pre-thcrap English patches that
ship a modified `th10e.exe` and friends — are still not supported: they rewrite the game's code,
so this patch no longer recognises it and declines. thcrap translates at run time instead and
leaves the executable alone, which is why it works.

## Also in this build

- `touhou_hfr.log` now lists every module in the process that did not come from Windows itself,
  names any import that had to be taken back, and says when 9Ex steps over another patch's
  Direct3D hook. Each of those would have turned this bug from a day of guessing into a minute
  of reading.
- `test64.sh` no longer silently skips the New Classic signature checks when run without an
  executable — the same flaw `test.sh` had.

## Known limits

Unchanged from v0.5.1-test, and the `-test` in the name is still deliberate.

- **A replay recorded with the patch is not guaranteed to play back in the unmodified game,
  or in a different build of this patch.** Replays recorded before the patch play back fine.
- **Scores set with this patch are not comparable** to unmodified play; don't submit them.
- **Sub-stepping changes outcomes and generally makes the game harder**, because collision is
  tested several times per frame instead of once.
- **No full-run native-versus-patched replay comparison has been made for any game.**
- New Classic's replay format cannot describe either of its two optional settings, and nothing
  turns them off for you. Turn them off by hand before recording or watching a replay.

The README's Limitations section has the rest, including the per-game gaps.

## Verifying the download

```
sha256  01a6ec3b98962cf546dff856519845425670347c701fc5c369c59600852757b5
```

The archive contains both runtimes, the shaders, every document and the complete source. No
game file is modified by the patch, and no game file is included here.
