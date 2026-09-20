# Self-test for test-games.ps1's parsing and INI handling: no game, no Windows. Loads the
# script's functions and helper types from its syntax tree and runs them on canned input.
#   pwsh tools/test_games_selftest.ps1
$ErrorActionPreference = 'Stop'
$path = Join-Path (Split-Path $PSScriptRoot -Parent) 'test-games.ps1'
$errors = $null; $tokens = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors) { $errors | ForEach-Object { Write-Host "$($_.Extent.StartLineNumber): $($_.Message)" }; throw 'test-games.ps1 does not parse' }
foreach ($f in $ast.FindAll({ $args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false)) { . ([scriptblock]::Create($f.Extent.Text)) }
$type = $ast.Find({ $args[0] -is [System.Management.Automation.Language.CommandAst] -and $args[0].GetCommandName() -eq 'Add-Type' -and $args[0].Extent.Text -match 'TypeDefinition' }, $true)
. ([scriptblock]::Create($type.Extent.Text))
function Check([bool]$ok, [string]$what) { if (-not $ok) { throw "FAIL: $what" } }

$s = ConvertFrom-StatsLine 'stats: 354.20 presents/s (target 360), extra ticks 23, skipped ticks 0, sub calls 3588, frame calls 1221, logical 1.000, long gaps 7/5, ticks/s 358.80, subtick polls 12 applied 0, repeated frames 1500/1800'
Check ($s.Presents -eq 354.2 -and $s.Target -eq 360 -and $s.Ticks -eq 358.8 -and $s.Polls -eq 12 -and $s.Repeated -eq 1500 -and $s.Frames -eq 1800) 'stats line'
Check ($null -eq (ConvertFrom-StatsLine 'stats seconds=1.0 presents=2')) 'the debug stats line is not a stats line'

$ini = Join-Path ([System.IO.Path]::GetTempPath()) "hfr-selftest-$PID.ini"
"[hfr]`nfps=0 ; cap`nlog=0`n[video]`nfilter=bilinear" | Set-Content -LiteralPath $ini
Set-IniValues $ini @{ 'hfr.fps' = 120; 'video.sharpen' = 'cas'; 'fixed60.predict' = 0; 'hfr.log' = 1 }
Check ((Get-IniValue $ini 'hfr' 'fps' 0) -eq '120') 'replace a key'
Check ((Get-IniValue $ini 'HFR' 'log' 0) -eq '1') 'sections are case-insensitive'
Check ((Get-IniValue $ini 'video' 'sharpen' '') -eq 'cas' -and (Get-IniValue $ini 'video' 'filter' '') -eq 'bilinear') 'add a key to a section, keep its others'
Check ((Get-IniValue $ini 'fixed60' 'predict' 1) -eq '0') 'add a section'
Check ((Get-IniValue $ini 'hfr' 'absent' 7) -eq 7) 'default'
Check (@(Get-Content $ini | Where-Object { $_ -match '^fps=' }).Count -eq 1) 'no duplicate keys'
Remove-Item -LiteralPath $ini

$flat = New-Object int[] 10000; $noise = New-Object int[] 10000; $r = New-Object System.Random 1
for ($i = 0; $i -lt 10000; $i++) { $flat[$i] = 0x202020; $v = $r.Next(256); $noise[$i] = ($v -shl 16) -bor ($v -shl 8) -bor $v }
Check ([Hfr.Pixels]::Spread($flat) -eq 0) 'a flat picture has no spread'
Check ([Hfr.Pixels]::Spread($noise) -gt 40) 'noise has spread'
$half = $flat.Clone(); for ($i = 0; $i -lt 5000; $i++) { $half[$i] = 0xffffff }
$c = [Hfr.Pixels]::Changed($flat, $half, 40); Check ($c -gt 0.49 -and $c -lt 0.51) 'half the picture changed'
Check ([Hfr.Pixels]::Changed($flat, $flat, 40) -eq 0) 'nothing changed'
Check ([System.Runtime.InteropServices.Marshal]::SizeOf([type][Hfr.Win+MONITORINFO]) -eq 40) 'MONITORINFO is 40 bytes'
'PASS: test-games.ps1 parses; stats, INI and pixel helpers behave'
