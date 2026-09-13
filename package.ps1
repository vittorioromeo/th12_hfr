param([string]$Version='0.2.0-test',[string]$Compiler='C:\msys64\mingw32\bin\gcc.exe',
    [switch]$IncludeExperimental64,[string]$Compiler64='C:\msys64\mingw64\bin\gcc.exe')
$ErrorActionPreference='Stop'
if($Version -notmatch '^[A-Za-z0-9.-]+$'){throw 'Invalid version'}
& (Join-Path $PSScriptRoot 'build.ps1') -Compiler $Compiler
if($IncludeExperimental64){& (Join-Path $PSScriptRoot 'build64.ps1') -Compiler $Compiler64}
$name="touhou_hfr_v$Version"
$stage=Join-Path $PSScriptRoot ("build/package-"+[guid]::NewGuid().ToString('N'))
$package=Join-Path $stage $name
$source=Join-Path $package 'source'
New-Item -ItemType Directory -Path $source -Force | Out-Null
foreach($file in @('dinput8.dll','touhou_hfr.dll','touhou_hfr.exe','touhou_hfr.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "build/$file") -Destination $package
}
if($IncludeExperimental64) {
    foreach($file in @('touhou_hfr64.exe','touhou_hfr64.dll')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "build/$file") -Destination $package
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'shaders') -Destination $package -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TH06NC_DEVNOTES.md') -Destination $package
foreach($file in @('README.md','ARCHITECTURE.md','DEVNOTES.md','DEVNOTES_RUNTIME.md','TH10_DEVNOTES.md','TH11_DEVNOTES.md','TH13_DEVNOTES.md','ADDING_A_GAME.md','TH11_README.md','TH12_README.md','install.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $package}
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
foreach($file in @('build.ps1','build.sh','package.ps1','package.sh','test.ps1','test_th11.ps1','install.ps1','README.md','ARCHITECTURE.md','DEVNOTES.md','DEVNOTES_RUNTIME.md','TH10_DEVNOTES.md','TH11_DEVNOTES.md','TH13_DEVNOTES.md','ADDING_A_GAME.md','TH11_README.md','TH12_README.md','touhou_hfr.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $source
}
foreach($file in @('build64.ps1','test64.ps1','build64.sh','test64.sh','TH06NC_DEVNOTES.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $source
}
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'releases') | Out-Null
$archive=Join-Path $PSScriptRoot "releases/$name.zip"
if(Test-Path -LiteralPath $archive){Copy-Item -LiteralPath $archive -Destination (Join-Path $stage 'previous-release.zip')}
Compress-Archive -LiteralPath $package -DestinationPath $archive -Force
Write-Output "Release: $archive"
