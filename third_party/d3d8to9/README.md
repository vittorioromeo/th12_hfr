# D3D8-to-D3D9 interface translation

Source: https://github.com/crosire/d3d8to9

Pinned commit: `6cdb8a82184898f1b9371e4c8412c2d33ebb7b51`.
The files in `source/` are unmodified upstream sources. Redistribution terms are
in [LICENSE.md](LICENSE.md).

HFR compiles the interface implementations into its x86 DLL. It excludes upstream
`d3d8to9.cpp` and supplies `src/backends/d3d8_bridge.cpp` instead: the D3D9 object
comes from HFR's existing graphics backend, and no separate d3d8.dll is installed.
The bridge requires the existing DirectX helper library `d3dx9_43.dll`.

Compile with `D3D8TO9NOLOG` and `-fno-strict-aliasing`. Include this notice and
LICENSE.md when distributing a build containing this code.
