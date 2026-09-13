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
    & $pythonPath tools/test_fixed_profile.py $fixture
    if($LASTEXITCODE){throw 'x64 profile and launcher regression failed'}
} finally {Pop-Location;$env:PATH=$savedPath}
