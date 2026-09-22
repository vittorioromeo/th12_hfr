# TH18 handoff — 2026-09-22

## Workspace and constraints

Source worktree: `C:\Users\vittorio\Documents\ChatGPT\touhou hfr\th18_hfr`
Branch: `codex/th18-support`, based on main `e17443f` (TH15 release).
Main checkout: sibling `th12_hfr`. Another agent ports TH20; do not overwrite its work.
User installation/test target: `G:\TouhouClean\(TH18) Touhou Kouryuudou ~ Unconnected Marketeers\`.
User asked not to control the PC. Do not launch/drive the game; prepare builds and ask the
user to playtest. `touhou_hfr.exe --check` and inert tests do not launch a game.

## Current implementation

The branch changes implement an experimental native-60 presentation port, NOT full
TH15-style gameplay sub-stepping. Ability-card mechanics and replay logic stay native.
`src/games/th18.c` adds frame wrappers, routes all latency settings through the shared frame
scheduler, interpolates ordinary sprite quads at their batch-copy call, and enables shared
D3D9 scaling/F11/dimming. Native runner is preserved, including thprac's terminal RET site.
No disk game code has been patched. Sub-tick gameplay disabled; non-quad lasers
are not smoothed yet. The second build adds stage-camera smoothing and a draw-timer fix. Default interpolation on, prediction off.

`src/backends/fixed_quad.h` extracts TH08's geometry/history algorithm; TH08 now calls it.
Registry/signature tools include TH18. `tools/test_fixed_quad.h` covers shared geometry and
TH18 clock behavior using generated inert RET stubs. `tools/test_th18_stubs.py` checks the
actual emitted quad thunk and latency routing with Unicorn.

Detailed static analysis, addresses, rationale and future work are in
`docs/games/TH18_DEVNOTES.md`; `docs/GAME_DIFFERENCES.md` includes TH18. Scratch binary,
disassembly, 2,037-function Ghidra dump and helper scripts are under `build/th18-research/`.
Ghidra and compiler already installed in sibling `toolchain/` and `C:\msys64\mingw32\bin`.

## Verified so far

- `build.ps1` succeeds (existing compiler warnings in shared/third-party code).
- `test.ps1 -GameExe '<TH18 folder>\th18.exe'` passes: 27 signatures, identity mutation
  rejection, patch transaction/import checks, geometry, clock and Unicorn bridge tests.
- Launcher `--check` identifies the clean TH18 executable.
- Original SHA256: `6243e3624ae5100eaa5ded846e2d9b2d9e438ee7c20735e197c9c8170fb9627f`.
- User reported the first build "seems good". Log confirms ~360 presents/s and ~60 logic ticks/s.
- Second camera build: complete inert suite passes (33 signatures), build succeeds; awaiting visual test.

An early test failed because the harness's inert fixture section is non-executable;
the new clock test now temporarily makes only its generated RET-stub pages executable,
then restores bytes/protection. This was a test-fixture issue, not a game crash.

## Deployment and final validation

The matching runtime, proxy, launcher, INI and shaders are installed in the user's TH18
folder. `th18.exe` is unchanged (SHA256 above). Deployed `dinput8.dll` and `touhou_hfr.dll`
match the build, SHA256 `f89bef2435d26c168f08e89163e8549f1b0fbfd4fd2aac3193c48f65f6f1d3ff`.
Installed launcher `--check` returned 0. First-test settings: fps=0 (auto), debug=1,
fixed60 interpolate=1, predict=0, substep/subtick off. No game was launched.

TH08 and TH15 **native** regression harnesses passed. The existing TH08 Unicorn script
`tools/test_th08_stubs.py build/tests/fixture0` stopped making progress after its native
harness finished; it was terminated. It is an unresolved test, not a pass. No TH15 Unicorn
run was claimed. TH18's own complete `test.ps1` suite, including Unicorn, passed.
`check_docs.py`, signature generation consistency and `git diff --check` passed.

## Continue here

1. First build accepted by user. Next test: smooth background movement, transitions, then F11 menu, F10/window/fullscreen, stage movement/shots,
   pause, card activation and market. Read `touhou_hfr.log` in the game folder after testing.
2. Reproduce/resolve any rendering or pacing problem before enabling more features.
3. Smooth non-quad lasers; camera implementation is now present. Gameplay sub-stepping requires a separate
   card/timer/collision/RNG audit; do not enable MODE_SUB blindly.
4. Investigate the stalled existing TH08 Unicorn test if further changing shared quad code.
5. Integrate `codex/th18-support` with current main/TH20 work once reviewed. Registry edits
   are small; preserve both games when resolving concurrent additions. No push/PR was made.


Potential review points: native post-present bookkeeping is gated to native ticks;
catch-up may need special consideration for its slowdown accounting. Extra draw paths
may have side effects not yet covered by hardware testing; ordinary bullet/player draws
were statically checked. Never describe this initial build as a validated full port.

## Latest checkpoint: second build

Initial port is committed as `02c5047`; this follow-up adds source-camera interpolation
and limits the stage transition countdown to native frames. See the new sections at the
end of TH18_DEVNOTES.md for exact addresses, camera layouts, culling caveats and laser
virtual tables. No shared runtime algorithm changed in this follow-up.

Next useful work after visual testing: inspect laser draw functions 450340, 452b80,
44d010, 44ab60; manager 4cf3f4 and dispatch 448901. Preserve logical laser state and
collisions. Full sub-stepping remains a separate, unaudited project.

Validation logs: build/th18-research/test-camera.log and build-camera.log. Tests include
both stage C wrappers against inert native callees, camera input restoration, discontinuity
handling and transition countdown. The existing TH08 Unicorn stall remains unresolved;
no further shared-geometry changes were made here. User INI must be preserved on deployment.

Second build deployed 2026-09-22. DLL SHA256: 981b1115032272c78e8954e37877a3d135d0eddb44cb2e2da3318d6acfd50d45.
Rollback files: G:\TouhouClean\(TH18) Touhou Kouryuudou ~ Unconnected Marketeers\hfr-backup-20260922-021649. User INI and game EXE hashes verified unchanged.
Installed launcher --check returned 0; deployed runtime/proxy/launcher match build hashes.
