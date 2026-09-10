param([string]$Version='0.2.0-test',[string]$Compiler='C:\msys64\mingw32\bin\gcc.exe')
# Compatibility entry: releases now include both game adapters.
& (Join-Path $PSScriptRoot 'package.ps1') -Version $Version -Compiler $Compiler
