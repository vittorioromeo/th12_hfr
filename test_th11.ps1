param([Parameter(Mandatory=$true)][string[]]$GameExe,[string]$Compiler='C:\msys64\mingw32\bin\gcc.exe',[string]$Python='C:\Python313\python.exe')
& (Join-Path $PSScriptRoot 'build.ps1') -Compiler $Compiler
& (Join-Path $PSScriptRoot 'test.ps1') -GameExe $GameExe -Compiler $Compiler -Python $Python
