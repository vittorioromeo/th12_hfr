param(
    [ValidateSet('th10','th11','th12','all')][string]$Game='all', # compatibility: every build includes every adapter
    [string]$Compiler='C:\msys64\mingw32\bin\gcc.exe'
)
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
        'src/ui/menu.cpp')
    foreach($source in $menuSources) {
        $object='build/obj/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.o'
        & $cxxPath -std=gnu++17 -O2 -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter -Ithird_party/imgui -c $source -o $object
        if($LASTEXITCODE){throw "Menu compilation failed: $source"}
        $objects += $object
    }
    & $cxxPath -shared -static -static-libgcc -static-libstdc++ -o build/touhou_hfr.dll @objects -ld3d9 -lwinmm -lgdi32 -ldwmapi '-Wl,--kill-at'
    if($LASTEXITCODE){throw 'Unified DLL build failed'}
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -s -mwindows -static-libgcc src/launcher.c -o build/touhou_hfr.exe
    if($LASTEXITCODE){throw 'Unified launcher build failed'}
    Copy-Item -LiteralPath build/touhou_hfr.dll -Destination build/dinput8.dll
    Copy-Item -LiteralPath touhou_hfr.ini -Destination build/touhou_hfr.ini
    New-Item -ItemType Directory -Force build/shaders | Out-Null
    Copy-Item shaders/*.hlsl,shaders/README.md -Destination build/shaders
    Write-Output 'Built the shared runtime, in-game menu, launcher, and shaders.'
} finally {Pop-Location;$env:PATH=$savedPath}
