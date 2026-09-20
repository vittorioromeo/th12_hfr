# Upgrading from th11_hfr or th12_hfr

For folders that still have one of the two original per-game patches. TH10, TH13, TH14, TH08
and New Classic never had one: use the normal install.

```powershell
.\install.ps1 -GameDirectory 'G:\Touhou\TH11 ~ Subterranean Animism'
```

The script:

1. verifies the target game;
2. backs the folder up to `hfr-backups/<timestamp>/`;
3. installs the new files;
4. repoints the old `th11_hfr.dll` / `th12_hfr.dll` names at the new runtime, so existing
   shortcuts work and the old patch cannot load beside the new one;
5. keeps an existing `touhou_hfr.ini`, or carries the legacy INI over.

To undo: restore the timestamped backup and delete the files that its `manifest.json` lists
with `existed: false`.

By hand: replace `th11_hfr.dll` / `th12_hfr.dll` with a copy of `touhou_hfr.dll`, replace the
old launcher, and move your settings into `touhou_hfr.ini`. If an unrelated `dinput8.dll` proxy
is already in the folder, do not overwrite it; two proxies have to be chained deliberately.
