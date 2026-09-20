param([string]$Version='0.6.1-test',[string]$Compiler='C:\msys64\mingw32\bin\gcc.exe',
    [switch]$IncludeExperimental64,[string]$Compiler64='C:\msys64\mingw64\bin\gcc.exe')
$ErrorActionPreference='Stop'
if($Version -notmatch '^[A-Za-z0-9.-]+$'){throw 'Invalid version'}
& (Join-Path $PSScriptRoot 'build.ps1') -Compiler $Compiler
if($IncludeExperimental64){& (Join-Path $PSScriptRoot 'build64.ps1') -Compiler $Compiler64}
$name="touhou_hfr_v$Version"
$stage=Join-Path $PSScriptRoot ("build/package-"+[guid]::NewGuid().ToString('N'))
$package=Join-Path $stage $name
$source=Join-Path $package 'source'
try {
New-Item -ItemType Directory -Path $source -Force | Out-Null

# What a player unzips: the runtime, its configuration template, the installer and the docs.
foreach($file in @('dinput8.dll','touhou_hfr.dll','touhou_hfr.exe','touhou_hfr.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "build/$file") -Destination $package
}
if($IncludeExperimental64) {
    foreach($file in @('touhou_hfr64.exe','touhou_hfr64.dll','dxgi.dll')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "build/$file") -Destination $package
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'shaders') -Destination $package -Recurse
# Docs are copied wholesale rather than enumerated: a new document ships without
# anyone remembering to add it here, and to package.sh, and to both source lists.
# One name per Join-Path: Windows PowerShell 5.1 will not take an array as -ChildPath.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'docs') -Destination $package -Recurse
foreach($file in @('README.md','CHANGELOG.md','ARCHITECTURE.md','ADDING_A_GAME.md','install.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $package
}
# d3d8to9 is linked into the runtime; BSD-2-Clause wants its licence with the binary.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party/d3d8to9/LICENSE.md') -Destination (Join-Path $package 'D3D8TO9_LICENSE.md')

# ... and the complete source beside it, so a release can be rebuilt from itself.
foreach($dir in @('src','tools','third_party','shaders')) {
    $base=Join-Path $PSScriptRoot $dir
    foreach($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        if($file.FullName -match '[\\/]__pycache__[\\/]' -or $file.Extension -eq '.pyc'){continue}
        $relative=$file.FullName.Substring($PSScriptRoot.Length+1)
        $dest=Join-Path $source $relative
        New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $dest
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'docs') -Destination $source -Recurse
foreach($file in @('build.ps1','build.sh','build64.ps1','build64.sh','test.ps1','test.sh',
                   'test64.ps1','test64.sh','test-games.ps1','package.ps1','package.sh','install.ps1',
                   'touhou_hfr.ini','README.md','CHANGELOG.md','ARCHITECTURE.md','ADDING_A_GAME.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $source
}

New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'releases') | Out-Null
$archive=Join-Path $PSScriptRoot "releases/$name.zip"
Compress-Archive -LiteralPath $package -DestinationPath $archive -Force
Write-Output "Release: $archive"
} finally { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
