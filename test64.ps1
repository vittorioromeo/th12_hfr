param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [string]$Compiler='C:\msys64\mingw64\bin\gcc.exe',
    [string]$Python='C:\Python313\python.exe'
)
$ErrorActionPreference='Stop'
$compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
$pythonPath=(Get-Command $Python -ErrorAction Stop).Source
$fixture=(Resolve-Path -LiteralPath $GameExe).Path
$savedPath=$env:PATH
$env:PATH="$(Split-Path -Parent $compilerPath);$savedPath"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build/tests | Out-Null
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc tools/test_fixed.c `
        build/obj64/buffer.o build/obj64/hook.o build/obj64/trampoline.o build/obj64/hde64.o -lbcrypt -o build/tests/test_fixed.exe
    if($LASTEXITCODE){throw 'x64 harness build failed (run build64.ps1 first)'}
    & ./build/tests/test_fixed.exe build/tests/fixed-plan.json
    if($LASTEXITCODE){throw 'x64 native regression failed'}
    & $pythonPath tools/test_fixed_stubs.py build/tests/fixed-plan.json
    if($LASTEXITCODE){throw 'x64 machine-code regression failed'}
    # The dxgi proxy against this machine's real dxgi.dll.
    & $compilerPath -std=gnu11 -O1 -Wall -Wextra -o build/tests/test_dxgi_proxy.exe tools/test_dxgi_proxy.c -lole32
    if($LASTEXITCODE){throw 'dxgi proxy test build failed'}
    & ./build/tests/test_dxgi_proxy.exe build/dxgi.dll
    if($LASTEXITCODE){throw 'dxgi proxy regression failed'}
    # Use the actual dxgi.dll basename and a stand-in runtime in an isolated folder.
    # Forwarding alone can pass even if the runtime initialization hook is never called.
    New-Item -ItemType Directory -Force build/tests/proxy-start | Out-Null
    Copy-Item -LiteralPath build/dxgi.dll -Destination build/tests/proxy-start/dxgi.dll
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -shared -static-libgcc -DPROXY_TEST_RUNTIME tools/test_dxgi_proxy_start.c -o build/tests/proxy-start/touhou_hfr64.dll
    if($LASTEXITCODE){throw 'Proxy test runtime build failed'}
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -static-libgcc tools/test_dxgi_proxy_start.c -o build/tests/proxy-start/test_proxy_start.exe
    if($LASTEXITCODE){throw 'Proxy startup test build failed'}
    foreach($factory in @('CreateDXGIFactory','CreateDXGIFactory1','CreateDXGIFactory2')) {
        & ./build/tests/proxy-start/test_proxy_start.exe $factory
        if($LASTEXITCODE){throw "Proxy startup regression failed: $factory"}
        & ./build/tests/proxy-start/test_proxy_start.exe $factory --system-factory
        if($LASTEXITCODE){throw "System DXGI startup regression failed: $factory"}
    }
    & ./build/tests/proxy-start/test_proxy_start.exe CreateDXGIFactory2 --unload
    if($LASTEXITCODE){throw 'Proxy unload regression failed'}
    & $pythonPath tools/test_fixed_profile.py $fixture
    if($LASTEXITCODE){throw 'x64 profile and launcher regression failed'}
} finally {Pop-Location;$env:PATH=$savedPath}
