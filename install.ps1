param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$SourceDirectory
)
$ErrorActionPreference='Stop'
$gamePath=(Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
if(-not $SourceDirectory) {
    $SourceDirectory=if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'build/touhou_hfr.dll')) {Join-Path $PSScriptRoot 'build'} else {$PSScriptRoot}
}
$sourcePath=(Resolve-Path -LiteralPath $SourceDirectory).Path
foreach($name in @('touhou_hfr.dll','touhou_hfr.exe','touhou_hfr.ini')) {
    if(-not (Test-Path -LiteralPath (Join-Path $sourcePath $name) -PathType Leaf)){throw "Missing build file: $name"}
}
$supported=@()
foreach($name in @('th11.exe','th11e.exe','th12.exe','th12e.exe')) {
    $exe=Join-Path $gamePath $name
    if(Test-Path -LiteralPath $exe) {
        $check=Start-Process -FilePath (Join-Path $sourcePath 'touhou_hfr.exe') -ArgumentList @('--check',('"'+$exe+'"')) -WindowStyle Hidden -Wait -PassThru
        if($check.ExitCode -eq 0){$supported+=$name}
    }
}
if(-not $supported.Count){throw 'No supported executable found; nothing installed.'}
foreach($process in Get-Process) {
    if($process.Path -and [IO.Path]::GetDirectoryName($process.Path).TrimEnd('\') -eq $gamePath) {
        throw "Close $($process.ProcessName) before installing."
    }
}
$copies=@{
    'dinput8.dll'='touhou_hfr.dll'
    'touhou_hfr.dll'='touhou_hfr.dll'
    'touhou_hfr.exe'='touhou_hfr.exe'
}
# Existing shortcuts/launchers must not load an old runtime beside the new proxy.
foreach($game in @('th11','th12')) {
    foreach($ext in @('dll','exe')) {
        $legacy="${game}_hfr.$ext"
        if(Test-Path -LiteralPath (Join-Path $gamePath $legacy)){$copies[$legacy]="touhou_hfr.$ext"}
    }
}
$backup=Join-Path $gamePath ('hfr-backups/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup | Out-Null
$records=@()
foreach($name in $copies.Keys) {
    $dest=Join-Path $gamePath $name;$exists=Test-Path -LiteralPath $dest
    $records+=@{name=$name;existed=$exists}
    if($exists){Copy-Item -LiteralPath $dest -Destination (Join-Path $backup $name)}
}
$ini=Join-Path $gamePath 'touhou_hfr.ini'
$newIni=-not (Test-Path -LiteralPath $ini)
if($newIni){$records+=@{name='touhou_hfr.ini';existed=$false}}
@{gameDirectory=$gamePath;files=$records} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
try {
    foreach($name in $copies.Keys){Copy-Item -LiteralPath (Join-Path $sourcePath $copies[$name]) -Destination (Join-Path $gamePath $name)}
    if($newIni) {
        $configSource=Join-Path $sourcePath 'touhou_hfr.ini'
        foreach($legacy in @('th11_hfr.ini','th12_hfr.ini')) {
            if(Test-Path -LiteralPath (Join-Path $gamePath $legacy)){$configSource=Join-Path $gamePath $legacy;break}
        }
        Copy-Item -LiteralPath $configSource -Destination $ini
    }
    $hash=(Get-FileHash -LiteralPath (Join-Path $sourcePath 'touhou_hfr.dll')).Hash
    foreach($name in $copies.Keys) {
        $expected=(Get-FileHash -LiteralPath (Join-Path $sourcePath $copies[$name])).Hash
        if((Get-FileHash -LiteralPath (Join-Path $gamePath $name)).Hash -ne $expected){throw "Verification failed: $name"}
    }
} catch {
    foreach($record in $records) {
        $dest=Join-Path $gamePath $record.name
        if($record.existed){Copy-Item -LiteralPath (Join-Path $backup $record.name) -Destination $dest -Force}
        elseif(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest}
    }
    throw
}
Write-Output "Installed the unified patch for $($supported -join ', ')"
Write-Output "Backup: $backup"
Write-Output "DLL SHA256: $hash"
