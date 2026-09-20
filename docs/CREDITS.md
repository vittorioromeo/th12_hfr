# Tools, references and sources

The third-party code shipped in the build is listed in the [README](../README.md#credits).

**Reverse engineering.** [Ghidra](https://github.com/NationalSecurityAgency/ghidra) 11.3.2,
headless, with the two scripts in `tools/` (`FixFuncs.java` recovers the functions ZUN's MSVC
builds hide behind vtables and `int3` padding; `ExportAll.java` decompiles everything into one
greppable file). `objdump -d -M intel` for exact instruction bytes; Python with
[pefile](https://github.com/erocarrera/pefile) and [Capstone](https://www.capstone-engine.org/)
for the pattern scans in `tools/`, and [Unicorn](https://www.unicorn-engine.org/) to emulate
the emitted stubs and patched sites in the Python tests. Game data was unpacked and the ECL and
ANM scripts read with [thtk](https://github.com/thpatch/thtk) (`thdat`, `thecl`, `thanm`) and
[truth](https://github.com/ExpHP/truth), whose instruction tables name what each script does.

**Existing projects consulted.** [thprac](https://github.com/touhouworldcup/thprac) for its
large, well-tested sets of TH11 and TH12 addresses and struct offsets, used as an independent
reference; [OpenInputLagPatch](https://github.com/khang06/OpenInputLagPatch) by khang06 for the
Direct3D 9Ex approach (managed-pool conversion, `CreateDeviceEx`, `SetMaximumFrameLatency`)
and its main-loop hook site; vpatch and thcrap for how a `dinput8.dll` proxy is expected to
coexist with the rest of the ecosystem; PivotDX9 as the wrapper the presentation-path detection
was written against. The update-runner protocol, the game-speed model, the draw order and
everything else in the devnotes was reverse-engineered from the binaries.

The filters were ported to Direct3D 9 HLSL from the
[libretro/slang-shaders](https://github.com/libretro/slang-shaders) and
[libretro/common-shaders](https://github.com/libretro/common-shaders) versions; the licence
texts travel at the top of each file, and `shaders/README.md` records what the ports changed
and which well-known filters (xBRZ, hqx, NNEDI3, FSRCNNX) were left out and why.

**Build and test environment.** MinGW-w64 GCC on Windows and Linux; PowerShell and POSIX shell
scripts. Development and every automated run happened under [Wine](https://www.winehq.org/) 9 on
Linux with Xvfb, `xdotool` driving the games and ImageMagick reading the screen; Direct3D
behaviour was confirmed on Windows in play.
