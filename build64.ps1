param([string]$Compiler='C:\msys64\mingw64\bin\gcc.exe')
$ErrorActionPreference='Stop'
$compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
$savedPath=$env:PATH
$env:PATH="$(Split-Path -Parent $compilerPath);$savedPath"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build/obj64 | Out-Null
    $objects=@()
    $sources=@('src/hfr64.c','third_party/minhook/src/buffer.c','third_party/minhook/src/hook.c',
        'third_party/minhook/src/trampoline.c','third_party/minhook/src/hde/hde64.c')
    foreach($source in $sources) {
        $object='build/obj64/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.o'
        & $compilerPath -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -c $source -o $object
        if($LASTEXITCODE){throw "x64 compilation failed: $source"}
        $objects += $object
    }
    $cxxPath=Join-Path (Split-Path -Parent $compilerPath) 'g++.exe'
    $menuSources=@('third_party/imgui/imgui.cpp','third_party/imgui/imgui_draw.cpp',
        'third_party/imgui/imgui_tables.cpp','third_party/imgui/imgui_widgets.cpp',
        'third_party/imgui/backends/imgui_impl_dx11.cpp','third_party/imgui/backends/imgui_impl_win32.cpp',
        'src/ui/menu.cpp','src/ui/menu_dx11.cpp','src/ui/overlay_dx11.cpp')
    foreach($source in $menuSources) {
        $object='build/obj64/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.o'
        & $cxxPath -std=gnu++17 -O2 -g -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter -Ithird_party/imgui -c $source -o $object
        if($LASTEXITCODE){throw "x64 menu compilation failed: $source"}
        $objects += $object
    }
    & $cxxPath -shared -static -static-libgcc -static-libstdc++ -o build/touhou_hfr64.dll @objects -ldxgi -ld3d11 -ld3dcompiler -ldxguid -lbcrypt -lgdi32 -ldwmapi
    if($LASTEXITCODE){throw 'x64 runtime link failed'}
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -static-libgcc src/launcher64.c -o build/touhou_hfr64.exe -lbcrypt
    if($LASTEXITCODE){throw 'x64 launcher build failed'}
    # The dxgi.dll proxy: the game loads it by name, so this is how the patch installs
    # itself when Steam is the launcher. Built alone; it forwards and nothing more.
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -shared -s -static-libgcc src/proxy_dxgi.c -o build/dxgi.dll
    if($LASTEXITCODE){throw 'dxgi proxy build failed'}
    Write-Output 'Built the experimental x64 fixed-clock runtime, its dxgi proxy and the launcher helper.'
} finally {Pop-Location;$env:PATH=$savedPath}
