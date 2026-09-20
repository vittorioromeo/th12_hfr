# Building

## Windows

x86 runtime: a **32-bit MinGW-w64 GCC** (default `C:\msys64\mingw32\bin\gcc.exe`; override with
`-Compiler`).

```powershell
.\build.ps1
.\test.ps1 -GameExe 'G:\Touhou\TH11\th11.exe','G:\Touhou\TH12\th12.exe' -Python 'C:\Python313\python.exe'
.\package.ps1
```

New Classic: also a **64-bit MinGW-w64 GCC/G++** (default `C:\msys64\mingw64\bin\gcc.exe`). Run
`build.ps1` first.

```powershell
.\build64.ps1
.\test64.ps1 -GameExe 'C:\Program Files (x86)\Steam\steamapps\common\th06nc\'
.\package.ps1 -IncludeExperimental64
```

## Linux

`build.sh`, `build64.sh`, `test.sh`, `test64.sh` and `package.sh` do the same with
`i686-w64-mingw32-gcc`, `x86_64-w64-mingw32-gcc` and `zip`, running the Windows binaries under
Wine.

## Tests

[TESTING.md](TESTING.md) covers what each suite checks, the in-game tests
(`test-games.ps1`), and what has to be played by hand before a release.
