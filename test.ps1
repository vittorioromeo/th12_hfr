param(
    [Parameter(Mandatory=$true)][string[]]$GameExe,
    [string]$Compiler='C:\msys64\mingw32\bin\gcc.exe',
    [string]$Python='C:\Python313\python.exe'
)
$ErrorActionPreference='Stop'
$compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
$pythonPath=(Get-Command $Python -ErrorAction Stop).Source
$fixtures=@($GameExe | ForEach-Object {(Resolve-Path -LiteralPath $_).Path})
$savedPath=$env:PATH
$env:PATH="$(Split-Path -Parent $compilerPath);$savedPath"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build/tests | Out-Null
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -static-libgcc tools/test_hfr.c -o build/tests/test_hfr.exe -ld3d9 -lwinmm -lgdi32 -ldwmapi -lpsapi '-Wl,--image-base,0x300000,--disable-dynamicbase,--section-start,.fixture=0x400000'
    if($LASTEXITCODE){throw 'Native harness build failed'}
    & $pythonPath tools/fill_pe_gaps.py build/tests/test_hfr.exe
    if($LASTEXITCODE){throw 'Harness fixture reservation failed'}
    & $pythonPath tools/gen_signature_json.py --check
    if($LASTEXITCODE){throw 'tools/th*_signatures.json no longer matches src/games/th*_signatures.h'}
    & $pythonPath tools/check_docs.py
    if($LASTEXITCODE){throw 'Documentation links failed'}
    & $pythonPath tools/check_game_differences.py
    if($LASTEXITCODE){throw 'docs/GAME_DIFFERENCES.md is behind the source'}
    for($i=0;$i -lt $fixtures.Count;++$i) {
        $prefix="build/tests/fixture$i"
        & ./build/tests/test_hfr.exe $fixtures[$i] $prefix
        if($LASTEXITCODE){throw "Native regression failed: $($fixtures[$i])"}
        $game=(& $pythonPath tools/verify_game.py $fixtures[$i])
        if($LASTEXITCODE){throw 'Executable identification failed'}
        & $pythonPath "tools/test_th${game}_stubs.py" $prefix
        if($LASTEXITCODE){throw 'Machine-code regression failed'}
        $check=Start-Process -FilePath './build/touhou_hfr.exe' -ArgumentList @('--check',('"'+$fixtures[$i]+'"')) -WindowStyle Hidden -Wait -PassThru
        if($check.ExitCode -ne 0){throw 'Launcher detection failed'}
    }
} finally {Pop-Location;$env:PATH=$savedPath}
