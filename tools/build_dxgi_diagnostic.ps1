param([string]$Compiler='C:\msys64\mingw64\bin\gcc.exe')
$ErrorActionPreference='Stop'
$compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
$savedPath=$env:PATH
$env:PATH="$(Split-Path -Parent $compilerPath);$savedPath"
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $output='build/proton-proxy-diagnostic'
    New-Item -ItemType Directory -Force $output | Out-Null
    & $compilerPath -std=gnu11 -O2 -Wall -Wextra -shared -s -static-libgcc -DHFR_PROXY_DIAGNOSTICS src/proxy_dxgi.c -o "$output/dxgi.dll"
    if($LASTEXITCODE){throw 'Diagnostic proxy build failed'}
    @'
Touhou HFR: New Classic / Proton diagnostic proxy (2026-09-16)

This is a logging build, not a confirmed fix. It changes only the dxgi autoloader.

1. Exit the game. Keep a backup of the existing game-folder dxgi.dll outside the
   game folder, then replace it with the dxgi.dll from this package.
2. Keep your existing touhou_hfr64.dll and touhou_hfr.ini. This package deliberately
   contains neither a runtime DLL nor a settings file.
3. Keep the Steam launch options used for the previous test:
   PROTON_LOG=1 WINEDEBUG=+loaddll,+debugstr WINEDLLOVERRIDES="dxgi=n,b" %command%
4. Launch through Steam, reach the title screen, try F11, then quit.
5. Return touhou_hfr_proxy.log from beside the game, touhou_hfr.log if it appears,
   and the freshly generated steam-4659620.log from your home directory. If no
   proxy log appears, report that explicitly. Proxy messages also go to Proton's log.
6. Restore the original dxgi.dll after collecting the logs.

The diagnostic proxy appends to its own log and labels each process/thread. It
does not change the HFR runtime, simulation settings or the executable.
'@ | Set-Content -LiteralPath "$output/README.txt" -Encoding utf8
    # Explicit file list: never package the stand-in runtime used by the startup tests.
    $archive='build/touhou-hfr-dxgi-diagnostic-2026-09-16.zip'
    Compress-Archive -LiteralPath "$output/dxgi.dll","$output/README.txt" -DestinationPath $archive -Force
    Get-FileHash -LiteralPath "$output/dxgi.dll",$archive -Algorithm SHA256
} finally {
    Pop-Location
    $env:PATH=$savedPath
}
