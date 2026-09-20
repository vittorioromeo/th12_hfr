# Requires an i686 MinGW-w64 cross compiler (gcc and g++). One build carries every
# game adapter. The runtime is C; only the in-game menu and Dear ImGui are C++.
param([string]$Compiler='C:\msys64\mingw32\bin\gcc.exe')
$ErrorActionPreference='Stop'
$compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
$savedPath=$env:PATH
$env:PATH="$(Split-Path -Parent $compilerPath);$savedPath"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build/obj | Out-Null
    $cxxPath=Join-Path (Split-Path -Parent $compilerPath) 'g++.exe'
    $objects=@('build/obj/hfr.o')
    & $compilerPath -std=gnu11 -O2 -msse2 -mfpmath=sse -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -c src/hfr.c -o $objects[0]
    if($LASTEXITCODE){throw 'Runtime compilation failed'}
    $menuSources=@('third_party/imgui/imgui.cpp','third_party/imgui/imgui_draw.cpp',
        'third_party/imgui/imgui_tables.cpp','third_party/imgui/imgui_widgets.cpp',
        'third_party/imgui/backends/imgui_impl_dx9.cpp','third_party/imgui/backends/imgui_impl_win32.cpp',
        'src/ui/menu.cpp','src/ui/menu_dx9.cpp','src/backends/d3d8_bridge.cpp')
    $menuSources += Get-ChildItem third_party/d3d8to9/source/*.cpp | Where-Object Name -ne 'd3d8to9.cpp' | ForEach-Object FullName
    foreach($source in $menuSources) {
        $object='build/obj/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.o'
        & $cxxPath -std=gnu++17 -O2 -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-strict-aliasing -DD3D8TO9NOLOG -Wall -Wno-unused-parameter -Wno-delete-non-virtual-dtor -Wno-unknown-pragmas -Ithird_party/imgui -c $source -o $object
        if($LASTEXITCODE){throw "Menu compilation failed: $source"}
        $objects += $object
    }
    & $cxxPath -shared -static -static-libgcc -static-libstdc++ -o build/touhou_hfr.dll @objects -ld3d9 -lwinmm -lgdi32 -ldwmapi -lpsapi '-Wl,--kill-at'
    if($LASTEXITCODE){throw 'Unified DLL build failed'}
    & $compilerPath -std=gnu11 -O2 -msse2 -mfpmath=sse -Wall -Wextra -Wno-unused-function -s -mwindows -static-libgcc src/launcher.c -o build/touhou_hfr.exe -lbcrypt
    if($LASTEXITCODE){throw 'Unified launcher build failed'}
    Copy-Item -LiteralPath build/touhou_hfr.dll -Destination build/dinput8.dll
    Copy-Item -LiteralPath touhou_hfr.ini -Destination build/touhou_hfr.ini
    New-Item -ItemType Directory -Force build/shaders | Out-Null
    Copy-Item shaders/*.hlsl,shaders/README.md -Destination build/shaders
    Write-Output 'Built the shared runtime, in-game menu, launcher, and shaders.'
} finally {Pop-Location;$env:PATH=$savedPath}
