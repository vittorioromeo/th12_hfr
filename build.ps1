param(
    [ValidateSet('th11','th12','all')][string]$Game='all', # compatibility: every build supports both
    [string]$Compiler='C:\msys64\mingw32\bin\gcc.exe'
)
$ErrorActionPreference='Stop'
$compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
$savedPath=$env:PATH
$env:PATH="$(Split-Path -Parent $compilerPath);$savedPath"
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -shared -static-libgcc src/hfr.c -o build/touhou_hfr.dll -ld3d9 -lwinmm '-Wl,--kill-at'
    if($LASTEXITCODE){throw 'Unified DLL build failed'}
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -s -mwindows -static-libgcc src/launcher.c -o build/touhou_hfr.exe
    if($LASTEXITCODE){throw 'Unified launcher build failed'}
    Copy-Item -LiteralPath build/touhou_hfr.dll -Destination build/dinput8.dll
    Copy-Item -LiteralPath touhou_hfr.ini -Destination build/touhou_hfr.ini
    Write-Output 'Built one DLL and launcher supporting TH11 and TH12.'
} finally {Pop-Location;$env:PATH=$savedPath}
