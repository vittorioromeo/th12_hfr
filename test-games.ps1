<#
.SYNOPSIS
    Touhou HFR - unattended in-game smoke test for TH10 to TH14 on Windows.

.DESCRIPTION
    For each patched game this launches the executable and, without any human at the controls,
    checks:

      launch      the process starts, a window appears, and it is still responding at the end
      build       the install line reports the signature count this build should have
      patching    no site patch failed, no internal error, no failed transaction
      F10         the window size cycle steps exactly once per press, to sane sizes
      F11         the in-game menu opens and closes
      frame rate  the patch is actually driving frames at the display's rate, not just installed
      replay      the title screen's attract-mode demo plays back through the replay hooks
      crash       no exception was reported and the process did not die on its own

    Folders without dinput8.dll are skipped and listed: those are clean copies of the game with
    no patch in them, and testing one would only report that nothing happens.

    Why key injection works here: the patch reads F10 and F11 with GetAsyncKeyState, which
    keybd_event updates. The games themselves read the keyboard through DirectInput and would
    ignore injected keys - but nothing here needs the game to see a key, only the patch. That is
    also why this script cannot start a stage, fire a shot or pick a replay from a menu.

    One F10 press should produce exactly one "window: size cycle ->" line. TWO lines for one
    press is the specific failure this exists to catch: the game would have an F10 handler of
    its own on top of the patch's, meaning the profile change that gave it one was wrong.

.PARAMETER Root
    The folder holding the (TH10)... to (TH14)... game folders.

.PARAMETER Games
    Which to test, e.g. -Games TH11,TH12. Default: every patched game found.

.PARAMETER Presses
    F10 presses per game. Default 5.

.PARAMETER IdleSeconds
    How long to leave each game alone at the title screen, for the frame-rate statistics and the
    attract-mode demo. The patch writes statistics every 5 seconds and the demo usually starts
    within about 30. Default 45. Use -IdleSeconds 0 to skip both (much faster, less coverage).

.EXAMPLE
    .\test-games.ps1 'G:\Touhou'
    .\test-games.ps1 'G:\Touhou' -Games TH14 -Presses 8
    .\test-games.ps1 'G:\Touhou' -IdleSeconds 0     # quick pass, no frame-rate or demo check
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]   $Root,
    [string[]] $Games,
    [int]      $Presses = 5,
    [int]      $IdleSeconds = 45,
    [int]      $LaunchTimeoutSec = 40,
    [switch]   $KeepOpen
)

$ErrorActionPreference = 'Stop'

# How many signatures the install line should report. Read from this repo's own frozen
# signature tables rather than written down here: a hardcoded number is wrong the first time
# anyone adds a hook, and silently wrong, which is worse than not checking at all. It is a more
# reliable build fingerprint than the version string, which is only bumped at a release.
function Get-ExpectedSignatures([string]$tag) {
    $json = Join-Path $PSScriptRoot ('tools\{0}_signatures.json' -f $tag.ToLower())
    if (Test-Path -LiteralPath $json) {
        try { return @((Get-Content -LiteralPath $json -Raw | ConvertFrom-Json)).Count } catch { return 0 }
    }
    return 0
}

# The sizes the patch's own cycle uses: 100%, 150% and 200% of the games' native 640x480, plus
# borderless, which is the desktop. Anything else is worth a look.
$KnownSizes = @('640x480', '960x720', '1280x960')

Add-Type -Namespace Hfr -Name Win -MemberDefinition @'
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern void keybd_event(
        byte bVk, byte bScan, uint dwFlags, System.UIntPtr dwExtraInfo);
    public struct RECT { public int Left, Top, Right, Bottom; }
'@

$VK_F10 = 0x79; $VK_F11 = 0x7A; $KEYEVENTF_UP = 0x0002; $SW_RESTORE = 9

function Get-ClientSize([IntPtr]$hwnd) {
    $r = New-Object Hfr.Win+RECT
    if ([Hfr.Win]::GetClientRect($hwnd, [ref]$r)) { return "$($r.Right)x$($r.Bottom)" }
    return '?'
}

function Send-Key([int]$vk) {
    [Hfr.Win]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120                       # held across at least one poll
    [Hfr.Win]::keybd_event([byte]$vk, 0, $KEYEVENTF_UP, [UIntPtr]::Zero)
}

# The patch holds the log open for writing, so always read a copy.
function Read-LogCopy([string]$log) {
    if (-not (Test-Path -LiteralPath $log)) { return @() }
    $tmp = [System.IO.Path]::GetTempFileName()
    try {
        Copy-Item -LiteralPath $log -Destination $tmp -Force
        return @(Get-Content -LiteralPath $tmp)
    } catch { return @() }
    finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
}
function Count-Lines([string[]]$lines, [string]$pattern) {
    return @($lines | Select-String -Pattern $pattern).Count
}

$RefreshHz = 0
try {
    $RefreshHz = [int](Get-CimInstance -ClassName Win32_VideoController |
        Where-Object { $_.CurrentRefreshRate } |
        Select-Object -First 1 -ExpandProperty CurrentRefreshRate)
} catch { }

# ---------------------------------------------------------------- find the games

if (-not (Test-Path -LiteralPath $Root)) { throw "Root folder not found: $Root" }

$found = @(); $skipped = @()
foreach ($dir in (Get-ChildItem -LiteralPath $Root -Directory)) {
    if ($dir.Name -notmatch '^\((TH\d\d)\)') { continue }
    $tag = $Matches[1]
    $exe = Join-Path $dir.FullName "$($tag.ToLower()).exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        $skipped += [pscustomobject]@{ Name = $dir.Name; Why = "no $($tag.ToLower()).exe" }; continue
    }
    if ($Games -and ($Games -notcontains $tag)) { continue }
    # dinput8.dll is the marker that matters: it is what loads the patch when the game's own
    # executable is started, which is how this script starts it. A folder with only
    # touhou_hfr.dll is patched for the launcher route, not this one - name it, do not hide it.
    $hasProxy    = Test-Path -LiteralPath (Join-Path $dir.FullName 'dinput8.dll')
    $hasLauncher = Test-Path -LiteralPath (Join-Path $dir.FullName 'touhou_hfr.dll')
    if (-not $hasProxy) {
        $why = if ($hasLauncher) { 'HFR present, but no dinput8.dll (launcher route only)' }
               else              { 'clean game, HFR not installed' }
        $skipped += [pscustomobject]@{ Name = $dir.Name; Why = $why }; continue
    }
    $found += [pscustomobject]@{ Tag = $tag; Dir = $dir.FullName; Exe = $exe }
}
$found = $found | Sort-Object Tag

Write-Host ''
Write-Host "Touhou HFR smoke test    $($found.Count) patched game(s), $Presses F10 presses, ${IdleSeconds}s idle each"
Write-Host "Root: $Root"
if ($RefreshHz) { Write-Host "Display refresh: $RefreshHz Hz" }
else { Write-Host 'Display refresh: unknown (the frame-rate check will use the log target only)' }
if ($skipped) {
    Write-Host ''; Write-Host 'Skipped:'
    foreach ($s in ($skipped | Sort-Object Name)) { Write-Host ("  {0,-56} {1}" -f $s.Name, $s.Why) }
}
Write-Host ''
if (-not $found) { Write-Host 'Nothing to test: no folder under this root has dinput8.dll in it.'; exit 2 }

$results = @()

foreach ($g in $found) {
    Write-Host ("=" * 76)
    Write-Host "$($g.Tag)   $($g.Dir)"
    $log = Join-Path $g.Dir 'touhou_hfr.log'
    $row = [ordered]@{
        Game = $g.Tag; Launch = 'FAIL'; Build = '-'; Patching = '-'
        F10 = '-'; F11 = '-'; Fps = '-'; Demo = '-'; Crash = '-'; Verdict = 'FAIL'
    }
    $notes = @()
    $proc = $null; $killed = $false
    try {
        # ---------------------------------------------------------------- launch
        $proc = Start-Process -FilePath $g.Exe -WorkingDirectory $g.Dir -PassThru
        $deadline = (Get-Date).AddSeconds($LaunchTimeoutSec)
        $hwnd = [IntPtr]::Zero
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 400
            if ($proc.HasExited) { break }
            $proc.Refresh()
            if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle; break }
        }
        if ($proc.HasExited) {
            Write-Host '  LAUNCH: the process exited on its own during startup'
            $row.Crash = "exit $($proc.ExitCode)"; $results += [pscustomobject]$row; continue
        }
        if ($hwnd -eq [IntPtr]::Zero) {
            Write-Host "  LAUNCH: no window within $LaunchTimeoutSec s"
            $results += [pscustomobject]$row; continue
        }
        $row.Launch = 'ok'
        Write-Host "  launched, window $hwnd"
        Start-Sleep -Seconds 3

        # ---------------------------------------------------------------- build and patching
        $lines = Read-LogCopy $log
        if (-not $lines) {
            Write-Host '  LOG: touhou_hfr.log is missing or empty - is log=1 set in this INI?'
            $row.Build = 'no log'; $row.Patching = 'no log'
            $notes += 'no log: set log=1'
        } else {
            $install = $lines | Select-String -Pattern '^Installed TH' | Select-Object -First 1
            if ($install) {
                Write-Host "  $($install.Line)"
                if ($install.Line -match '(\d+) verified signatures') {
                    $n = [int]$Matches[1]; $want = Get-ExpectedSignatures $g.Tag
                    if ($want -eq 0)      { $row.Build = "$n (?)" ; Write-Host "  (no signature table in tools\, so the build is reported and not judged)" }
                    elseif ($n -eq $want) { $row.Build = 'ok' }
                    else { $row.Build = "$n<>$want"; $notes += "signature count $n, this tree expects $want - the game is running a different build from this checkout" }
                } else { $row.Build = 'unparsed' }
            } else { $row.Build = 'no install line'; $notes += 'no install line: the patch did not install' }

            $rate = $lines | Select-String -Pattern '^logic rate:' | Select-Object -Last 1
            if ($rate) { Write-Host "  $($rate.Line)" }

            $bad = @($lines | Select-String -Pattern 'site patch .* failed|INTERNAL ERROR|Patch transaction failed')
            $row.Patching = if ($bad.Count -eq 0) { 'ok' } else { "$($bad.Count) bad" }
            foreach ($b in $bad) { Write-Host "  ! $($b.Line)"; $notes += $b.Line }
        }

        # ---------------------------------------------------------------- focus
        [void][Hfr.Win]::ShowWindow($hwnd, $SW_RESTORE)
        [void][Hfr.Win]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 600
        $hasFocus = ([Hfr.Win]::GetForegroundWindow() -eq $hwnd)
        if (-not $hasFocus) {
            Write-Host '  FOCUS: could not bring the window to the foreground. The patch only acts'
            Write-Host '         on F10/F11 while the game has focus, so those results are not'
            Write-Host '         meaningful. Run from an interactive console and do not click away.'
            $notes += 'window never got focus; F10/F11 results unreliable'
        }

        # ---------------------------------------------------------------- F10 size cycle
        # One uncounted warm-up press first. On a real run TH11 swallowed its first press while
        # TH10 did not, which makes the count depend on how long the game had been up rather
        # than on the thing being tested. The warm-up is reported, not hidden: if it is ignored
        # every time on one game, that is worth knowing, but it must not skew the 1-line-per-
        # press check that the double-handler test depends on.
        Start-Sleep -Milliseconds 1500
        $warmBefore = Count-Lines (Read-LogCopy $log) 'window: size cycle'
        Send-Key $VK_F10
        Start-Sleep -Milliseconds 700
        $warmAfter = Count-Lines (Read-LogCopy $log) 'window: size cycle'
        $warmOk = ($warmAfter -gt $warmBefore)
        Write-Host ("  F10 warm-up press: {0} (not counted)" -f $(if ($warmOk) { 'registered' } else { 'ignored' }))

        $prev  = $warmAfter
        $start = $prev
        $sizes = @()
        for ($i = 1; $i -le $Presses; $i++) {
            Send-Key $VK_F10
            Start-Sleep -Milliseconds 700           # resize, redraw and the log write
            $now   = Count-Lines (Read-LogCopy $log) 'window: size cycle'
            $delta = $now - $prev; $prev = $now
            $size  = Get-ClientSize $hwnd; $sizes += $size
            $note  = switch ($delta) {
                1       { '' }
                0       { '   <-- no response' }
                default { "   <-- $delta lines for ONE press" }
            }
            Write-Host ("  F10 {0}: +{1} line(s), client {2}{3}" -f $i, $delta, $size, $note)
        }
        $cycles = $prev - $start
        $distinct = @($sizes | Select-Object -Unique)
        $odd = @($distinct | Where-Object { $KnownSizes -notcontains $_ -and $_ -ne '?' })
        if ($cycles -eq $Presses)          { $row.F10 = "ok ($cycles/$Presses)" }
        elseif ($cycles -eq 0)             { $row.F10 = 'DEAD'; $notes += 'F10 does nothing' }
        elseif ($cycles -eq $Presses * 2)  { $row.F10 = 'DOUBLE'; $notes += 'F10 steps twice per press - this game has an F10 handler of its own' }
        else                               { $row.F10 = "$cycles/$Presses"; $notes += "F10 gave $cycles lines for $Presses presses" }
        if ($distinct.Count -lt 2 -and $cycles -gt 0) { $notes += 'F10 logged a cycle but the window size never changed' }
        # Borderless is the desktop size, so one unexpected size is normal; several are not.
        if ($odd.Count -gt 1) { $notes += "unexpected window sizes: $($odd -join ', ')" }
        Write-Host ("  F10 sizes: {0}" -f ($distinct -join ' '))

        # ---------------------------------------------------------------- F11 menu
        $before = Count-Lines (Read-LogCopy $log) '^menu: (opened|closed)'
        Send-Key $VK_F11; Start-Sleep -Milliseconds 900
        Send-Key $VK_F11; Start-Sleep -Milliseconds 900
        $lines  = Read-LogCopy $log
        $after  = Count-Lines $lines '^menu: (opened|closed)'
        $opened = @($lines | Select-String -Pattern '^menu: (opened|closed)' | Select-Object -Last 2)
        $noFocusSeen = Count-Lines $lines 'menu: key seen but the game does not have focus'
        if     ($after - $before -eq 2) { $row.F11 = 'ok' }
        elseif ($after - $before -eq 0) { $row.F11 = 'DEAD'; $notes += 'F11 did not open the menu' }
        else                            { $row.F11 = "$($after - $before)/2"; $notes += "F11 produced $($after-$before) menu lines instead of 2" }
        if ($noFocusSeen) { $notes += 'the patch saw the menu key without focus' }
        foreach ($o in $opened) { Write-Host "  $($o.Line)" }
        if ($lines | Select-String -Pattern '^menu: unavailable') { $notes += 'menu reported unavailable' }

        # ---------------------------------------------------------------- idle: frame rate, demo
        if ($IdleSeconds -gt 0) {
            Write-Host "  idling ${IdleSeconds}s for frame statistics and the attract-mode demo..."
            # Stop as soon as both things being waited for have happened: a statistics line
            # (written every 5 s) and the attract-mode demo. On the games measured so far that
            # cuts the wait roughly in half.
            $end = (Get-Date).AddSeconds($IdleSeconds)
            $responded = $true
            while ((Get-Date) -lt $end) {
                Start-Sleep -Seconds 2
                if ($proc.HasExited) { break }
                $proc.Refresh()
                if (-not $proc.Responding) { $responded = $false }
                $peek = Read-LogCopy $log
                if ((Count-Lines $peek '^stats: ') -gt 0 -and
                    (Count-Lines $peek 'replay playback started') -gt 0) {
                    Write-Host ("  both signals in after {0:N0}s" -f ($IdleSeconds - ($end - (Get-Date)).TotalSeconds))
                    break
                }
            }
            if (-not $responded) { $notes += 'the window stopped responding while idling' }
            $lines = Read-LogCopy $log

            $stat = $lines | Select-String -Pattern '^stats: ' | Select-Object -Last 1
            if ($stat -and $stat.Line -match 'stats: ([\d.]+) presents/s \(target (\d+)\)') {
                $pps = [double]$Matches[1]; $target = [int]$Matches[2]
                Write-Host "  $($stat.Line)"
                $ratio = if ($target -gt 0) { $pps / $target } else { 0 }
                if ($ratio -ge 0.85) { $row.Fps = "ok ($([int]$pps)/$target)" }
                else { $row.Fps = "$([int]$pps)/$target"; $notes += "presenting at $([int]$pps)/s against a target of $target" }
                if ($RefreshHz -and [math]::Abs($target - $RefreshHz) -gt 2) {
                    $notes += "the patch targets $target Hz but the display reports $RefreshHz Hz"
                }
                # Repeated frames are presents with no new logic tick behind them, so the
                # honest expectation is 1 - ticks/presents, not zero. During the attract-mode
                # demo the logic rate drops to 60 while presenting at the display rate, which
                # makes 5 of every 6 presents a repeat - correct, and a flat threshold would
                # have called it a fault.
                if ($stat.Line -match 'ticks/s ([\d.]+)' ) { $tps = [double]$Matches[1] } else { $tps = 0 }
                if ($stat.Line -match 'repeated frames (\d+)/(\d+)') {
                    $rep = [int]$Matches[1]; $tot = [int]$Matches[2]
                    if ($tot -gt 0 -and $target -gt 0 -and $tps -gt 0) {
                        $observed = $rep / $tot
                        $expected = [math]::Max(0.0, 1.0 - ($tps / $target))
                        Write-Host ("  repeated frames {0}/{1} = {2:P0}; logic {3:N0}/s against {4} presented = {5:P0} expected" -f $rep, $tot, $observed, $tps, $target, $expected)
                        if ([math]::Abs($observed - $expected) -gt 0.15) {
                            $notes += ("repeated frames {0:P0}, but {1:N0} logic ticks/s against {2} presented predicts {3:P0}" -f $observed, $tps, $target, $expected)
                        }
                    }
                }
            } else { $row.Fps = 'no stats'; $notes += 'no stats line - the patch may not be driving frames' }

            $demo = @($lines | Select-String -Pattern 'replay playback started')
            if ($demo.Count -gt 0) {
                $row.Demo = "ok ($($demo.Count))"
                Write-Host "  $($demo[-1].Line)"
            } else {
                $row.Demo = 'none'
                $notes += "no attract-mode demo within ${IdleSeconds}s - raise -IdleSeconds, or this game needs longer"
            }
        }

        # ---------------------------------------------------------------- crash
        $lines = Read-LogCopy $log
        $exc = @($lines | Select-String -Pattern '^EXCEPTION ')
        $proc.Refresh()
        if ($proc.HasExited) {
            $row.Crash = "exited ($($proc.ExitCode))"; $notes += "the game exited on its own, code $($proc.ExitCode)"
        } elseif ($exc.Count -gt 0) {
            $row.Crash = "$($exc.Count) exc"
            foreach ($e in $exc) { Write-Host "  ! $($e.Line)"; $notes += $e.Line }
        } else { $row.Crash = 'ok' }

        # ---------------------------------------------------------------- verdict
        $row.Verdict = if ($notes.Count -eq 0) { 'PASS' } else { 'CHECK' }
    }
    catch {
        Write-Host "  ERROR: $($_.Exception.Message)"
        $notes += "script error: $($_.Exception.Message)"
        $row.Verdict = 'ERROR'
    }
    finally {
        if ($proc -and -not $proc.HasExited -and -not $KeepOpen) {
            [void]$proc.CloseMainWindow()
            if (-not $proc.WaitForExit(5000)) { $killed = $true; $proc.Kill(); [void]$proc.WaitForExit(5000) }
            Write-Host ('  closed' + $(if ($killed) { ' (had to be killed - it ignored the close request)' } else { '' }))
        }
        Start-Sleep -Milliseconds 500
    }
    $obj = [pscustomobject]$row
    Add-Member -InputObject $obj -NotePropertyName Notes -NotePropertyValue $notes
    $results += $obj
    Write-Host ''
}

Write-Host ("=" * 76)
Write-Host 'SUMMARY   (paste this back)'
Write-Host ''
$results | Format-Table Game, Launch, Build, Patching, F10, F11, Fps, Demo, Crash, Verdict -AutoSize
foreach ($r in $results) {
    if ($r.Notes.Count) {
        Write-Host "$($r.Game):"
        foreach ($n in $r.Notes) { Write-Host "   - $n" }
    }
}
Write-Host ''
$bad = @($results | Where-Object { $_.Verdict -ne 'PASS' })
if ($bad.Count -eq 0) { Write-Host 'All games passed.'; exit 0 }
Write-Host "$($bad.Count) game(s) need attention: $(($bad.Game) -join ', ')"
exit 1
