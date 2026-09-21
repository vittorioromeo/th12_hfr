<#
.SYNOPSIS
    Touhou HFR - unattended in-game tests on Windows. Launches each installed game and judges
    the patch from touhou_hfr.log, the window and the screen.

.DESCRIPTION
    Three tiers, each a superset of the one before in cost:

      (default)  one launch per game: install, F10 size cycle, F11 menu (log and pixels),
                 frame statistics, attract-mode demo, crash, clean exit.
      -Matrix    several short launches per game, each with one INI setting changed: frame cap,
                 sub-stepping off, plain Direct3D 9, every shader filter, sharpening, internal
                 resolution, texture upscaling, and the touhou_hfr.exe launcher route.
      -Drive     plays: starts a stage with injected keys, holds fire and moves for
                 -DriveSeconds, and checks the simulation rate, the shot cycle and crashes.

    The INI of each game is copied aside before a launch and put back after it, whatever
    happens. A copy left behind by a killed run is restored at the next start.

    Every launch's log and screenshots are kept under -OutDir, with summary.txt.

    Keys are sent with SendInput carrying both the virtual key (which the patch polls with
    GetAsyncKeyState) and the scan code (which DirectInput reads). A key is only ever sent
    while the game is the foreground window; losing focus aborts that step, not the run.

.PARAMETER Root
    Folder holding the "(TH08) ...", "(TH10) ..." game folders. Default: this script's folder.

.PARAMETER Repo
    A checkout of the repository, for the expected signature counts (tools\th*_signatures.json)
    and the expected DLL (build\touhou_hfr.dll). Default: this script's folder if it is one.

.PARAMETER Games
    Which to test, e.g. -Games TH11,TH12. Default: every patched game found.

.PARAMETER Cases
    Run only the named cases, e.g. -Cases base,filter-scalefx. -List prints the names.

.EXAMPLE
    .\test-games.ps1 'G:\Touhou'
    .\test-games.ps1 'G:\Touhou' -Matrix -Drive
    .\test-games.ps1 'G:\Touhou' -Games TH14 -Cases drive -DriveSeconds 40
    .\test-games.ps1 'G:\Touhou' -IdleSeconds 0            # quick pass: no statistics, no demo
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]   $Root,
    [string]   $Repo,
    [string[]] $Games,
    [string[]] $Cases,
    [switch]   $Matrix,
    [switch]   $Drive,
    [switch]   $List,
    [int]      $Presses = 5,
    [int]      $IdleSeconds = 45,
    [int]      $QuickSeconds = 9,
    [int]      $DriveSeconds = 20,
    [int]      $LaunchTimeoutSec = 40,
    [string]   $OutDir,
    [switch]   $KeepOpen
)

$ErrorActionPreference = 'Stop'
# Tolerate -Games TH10,TH11 arriving as one string (powershell -File does that).
if ($Games) { $Games = @($Games | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToUpper() }) }
if ($Cases) { $Cases = @($Cases | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() }) }

# ------------------------------------------------------------------ what differs per game
# Demo:  the attract-mode demo goes through the replay hooks and logs "replay playback".
# Stage: a stage start is logged ("stage N first frame"), so -Drive can tell it got in.
# Fixed: the simulation stays at 60 Hz ([fixed60]); [hfr] substep does not apply.
# Shots: debug=1 reports shots per game frame, so the shot cycle can be judged.
$Traits = @{
    TH08 = @{ Demo = $false; Stage = $false; Fixed = $true;  Shots = $false }
    TH10 = @{ Demo = $true;  Stage = $true;  Fixed = $false; Shots = $false }
    TH11 = @{ Demo = $true;  Stage = $true;  Fixed = $false; Shots = $false }
    TH12 = @{ Demo = $true;  Stage = $true;  Fixed = $false; Shots = $false }
    TH13 = @{ Demo = $true;  Stage = $true;  Fixed = $false; Shots = $false }
    TH14 = @{ Demo = $true;  Stage = $true;  Fixed = $false; Shots = $true  }
    TH15 = @{ Demo = $true;  Stage = $true;  Fixed = $false; Shots = $false }
}

# ------------------------------------------------------------------ the cases
# Ini:     'section.key' = value, applied over the game's own INI for this launch only.
# Expect:  regexes that must match a log line.  Forbid: regexes that must not.
# Skip:    scriptblock($game, $traits, $refresh) -> reason string, or $null to run.
$Shaders = 'mmpx', 'scalefx', 'super-xbr', 'xbr-lv2'
$AllCases = @(
    @{ Name = 'base'; Kind = 'base'; Ini = @{} }
    @{ Name = 'cap-120'; Kind = 'quick'; Ini = @{ 'hfr.fps' = 120 }; Target = 120
       Skip = { param($g, $t, $hz) if ($hz -and $hz -lt 144) { "display is $hz Hz" } } }
    @{ Name = 'substep-off'; Kind = 'quick'; Ini = @{ 'hfr.substep' = 0 }
       Expect = @('^logic rate: .*substep=0')
       Skip = { param($g, $t, $hz) if ($t.Fixed) { 'fixed-logic game' } } }
    @{ Name = 'fixed-interp-off'; Kind = 'quick'; Ini = @{ 'fixed60.interpolate' = 0; 'fixed60.predict' = 0 }
       Expect = @('presentation: interpolate=0 predict=0')
       Skip = { param($g, $t, $hz) if (-not $t.Fixed) { 'not a fixed-logic game' } } }
    @{ Name = 'plain-d3d9'; Kind = 'quick'; Ini = @{ 'hfr.d3d9ex' = 0; 'fixed60.d3d9ex' = 0 }
       Expect = @('^Direct3DCreate9 hooked \(9Ex=0\)') }
    @{ Name = 'vsync'; Kind = 'quick'; Ini = @{ 'hfr.vsync' = 1; 'fixed60.vsync' = 1; 'hfr.fps' = 0 } }
    @{ Name = 'internal-x2'; Kind = 'quick'; Ini = @{ 'video.internal_scale' = 2 }
       Expect = @('^internal resolution x2') }
    @{ Name = 'texture-x2'; Kind = 'quick'; Ini = @{ 'video.texture_scale' = 2 }
       Expect = @('^textures: upscaling x2', '^textures: [1-9]\d* upscaled') }
    @{ Name = 'sharpen-cas'; Kind = 'quick'; Ini = @{ 'video.sharpen' = 'cas' }
       Expect = @('^video: sharpening with cas') }
    @{ Name = 'launcher'; Kind = 'quick'; Ini = @{}; Launcher = $true
       Skip = { param($g, $t, $hz) if (-not (Test-Path -LiteralPath (Join-Path $g.Dir 'touhou_hfr.exe'))) { 'no touhou_hfr.exe here' } } }
    @{ Name = 'drive'; Kind = 'drive'; Ini = @{ 'hfr.debug' = 1 } }
)
foreach ($s in $Shaders) {
    $AllCases += @{ Name = "filter-$s"; Kind = 'quick'; Ini = @{ 'video.filter' = $s; 'video.window_scale' = 200 }
                    Expect = @("^shaders: $([regex]::Escape($s)) (pass \d+ )?ready", "^video: filter \d+ = $([regex]::Escape($s)).*<- selected") }
}
# What no launch may log, whatever the case.
$NeverOk = @(
    'site patch .* failed', 'INTERNAL ERROR', 'Patch transaction failed', '^EXCEPTION ',
    '^CONFLICT: ', 'DRAW GUARD FAILED', 'draw guard: drawing changed gameplay state',
    'failed to compile', 'compiled but the device refused', '^menu: unavailable',
    'HFR inactive', 'no patches applied', 'no hooks applied', 'scaling disabled',
    'pose history allocation failed', 'Sprite hook installation failed'
)

if ($List) {
    $AllCases | ForEach-Object { '{0,-18} {1}' -f $_.Name, $_.Kind }
    exit 0
}

# ------------------------------------------------------------------ Win32
# A type cannot be redefined in a PowerShell session, so a second run in the same window keeps
# the first run's. After editing the C# below, open a new window.
if (('Hfr.Win' -as [type]) -and -not (('Hfr.Pixels' -as [type]) -and [Hfr.Win].GetMethod('Key'))) {
    throw 'This PowerShell window holds Hfr.Win from a different version of this script, and a type cannot be replaced. Open a new window, or start it with: powershell -File test-games.ps1 ...'
}
if (-not ('Hfr.Win' -as [type])) { Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Hfr {
public static class Win {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO mi);

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO { public int Size; public RECT Monitor, Work; public uint Flags; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit)]   struct InputUnion { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public InputUnion u; }

    // Virtual key for GetAsyncKeyState, scan code for DirectInput and raw input. The arrow
    // keys are extended keys; without the flag they arrive as the numeric pad.
    public static bool Key(ushort vk, bool down) {
        INPUT[] i = new INPUT[1];
        i[0].type = 1;
        i[0].u.ki.vk = vk;
        i[0].u.ki.scan = (ushort)MapVirtualKey(vk, 0);
        bool extended = vk >= 0x21 && vk <= 0x2e;
        i[0].u.ki.flags = (down ? 0u : 2u) | (extended ? 1u : 0u);
        return SendInput(1, i, Marshal.SizeOf(typeof(INPUT))) == 1;
    }
}
public static class Pixels {
    static int Luma(int p) { return (((p >> 16) & 255) * 77 + ((p >> 8) & 255) * 150 + (p & 255) * 29) >> 8; }
    // Standard deviation of luma, 0..~128. A cleared or dead window is 0.
    public static double Spread(int[] px) {
        if (px == null || px.Length == 0) return 0;
        double sum = 0, sq = 0; int n = 0;
        for (int i = 0; i < px.Length; i += 7) { int l = Luma(px[i]); sum += l; sq += (double)l * l; ++n; }
        double mean = sum / n; return Math.Sqrt(Math.Max(0, sq / n - mean * mean));
    }
    // Fraction of pixels whose luma moved by more than `threshold`.
    public static double Changed(int[] a, int[] b, int threshold) {
        if (a == null || b == null || a.Length != b.Length || a.Length == 0) return -1;
        int n = 0, hit = 0;
        for (int i = 0; i < a.Length; i += 3) { ++n; if (Math.Abs(Luma(a[i]) - Luma(b[i])) > threshold) ++hit; }
        return (double)hit / n;
    }
}
}
'@ }

$VK = @{ F10 = 0x79; F11 = 0x7A; Z = 0x5A; X = 0x58; Esc = 0x1B; Left = 0x25; Right = 0x27; Shift = 0x10 }
try { [void][Hfr.Win]::SetProcessDPIAware() } catch { }
$CanDraw = $true
try { Add-Type -AssemblyName System.Drawing } catch { $CanDraw = $false }

# ------------------------------------------------------------------ helpers
function Get-ClientSize([IntPtr]$hwnd) {
    $r = New-Object Hfr.Win+RECT
    if ([Hfr.Win]::GetClientRect($hwnd, [ref]$r)) { return "$($r.Right)x$($r.Bottom)" }
    return '?'
}
function Get-MonitorSize([IntPtr]$hwnd) {
    $mi = New-Object Hfr.Win+MONITORINFO
    $mi.Size = [System.Runtime.InteropServices.Marshal]::SizeOf([type][Hfr.Win+MONITORINFO])
    $m = [Hfr.Win]::MonitorFromWindow($hwnd, 2)
    if ([Hfr.Win]::GetMonitorInfo($m, [ref]$mi)) { return "$($mi.Monitor.Right - $mi.Monitor.Left)x$($mi.Monitor.Bottom - $mi.Monitor.Top)" }
    return '?'
}

# TH08 destroys its first window and makes another, so the handle is looked up every time.
function Get-GameWindow($proc) {
    $proc.Refresh()
    if ($proc.HasExited) { return [IntPtr]::Zero }
    return $proc.MainWindowHandle
}
function Set-GameFocus($proc) {
    $h = Get-GameWindow $proc
    if ($h -eq [IntPtr]::Zero) { return $false }
    if ([Hfr.Win]::GetForegroundWindow() -eq $h) { return $true }
    [void][Hfr.Win]::ShowWindow($h, 9)
    [void][Hfr.Win]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 500
    return ([Hfr.Win]::GetForegroundWindow() -eq (Get-GameWindow $proc))
}
# Never type into anything but the game.
function Send-KeyState($proc, [int]$vk, [bool]$down) {
    if ($down -and [Hfr.Win]::GetForegroundWindow() -ne (Get-GameWindow $proc)) { return $false }
    return [Hfr.Win]::Key([uint16]$vk, $down)
}
function Send-Key($proc, [int]$vk, [int]$holdMs = 120) {
    if (-not (Send-KeyState $proc $vk $true)) { return $false }
    Start-Sleep -Milliseconds $holdMs
    [void][Hfr.Win]::Key([uint16]$vk, $false)
    return $true
}

# The patch holds the log open for writing, so always read a copy.
function Read-Log([string]$log) {
    if (-not (Test-Path -LiteralPath $log)) { return @() }
    $tmp = [System.IO.Path]::GetTempFileName()
    try { Copy-Item -LiteralPath $log -Destination $tmp -Force; return @(Get-Content -LiteralPath $tmp) }
    catch { return @() }
    finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
}
function Count-Lines($lines, [string]$pattern) { return @($lines | Select-String -Pattern $pattern).Count }
function Wait-Log([string]$log, [string]$pattern, [int]$seconds, $proc) {
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) {
        if ($proc -and $proc.HasExited) { return $false }
        if ((Count-Lines (Read-Log $log) $pattern) -gt 0) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# "stats: 360.00 presents/s (target 360), ... ticks/s 60.00, subtick polls 0 applied 0, repeated frames 1500/1800"
function ConvertFrom-StatsLine([string]$line) {
    if ($line -notmatch '^stats: ([\d.]+) presents/s \(target (\d+)\)') { return $null }
    $s = [ordered]@{ Presents = [double]$Matches[1]; Target = [int]$Matches[2]; Ticks = 0.0; Polls = 0; Repeated = 0; Frames = 0; Skipped = 0 }
    if ($line -match 'ticks/s ([\d.]+)')            { $s.Ticks = [double]$Matches[1] }
    if ($line -match 'subtick polls (\d+)')         { $s.Polls = [int]$Matches[1] }
    if ($line -match 'skipped ticks (\d+)')         { $s.Skipped = [int]$Matches[1] }
    if ($line -match 'repeated frames (\d+)/(\d+)') { $s.Repeated = [int]$Matches[1]; $s.Frames = [int]$Matches[2] }
    return [pscustomobject]$s
}
function Get-Stats($lines) {
    return @($lines | Where-Object { $_ -like 'stats: *' } | ForEach-Object { ConvertFrom-StatsLine $_ } | Where-Object { $_ })
}

# Section-aware INI edit that leaves every other line alone.
function Set-IniValues([string]$path, [hashtable]$values) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $path) { foreach ($l in (Get-Content -LiteralPath $path)) { $lines.Add($l) } }
    foreach ($full in $values.Keys) {
        $section, $key = $full.Split('.', 2); $value = $values[$full]
        $start = -1; $end = $lines.Count
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^\s*\[(.+?)\]\s*$') {
                if ($start -ge 0) { $end = $i; break }
                if ($Matches[1] -ieq $section) { $start = $i }
            }
        }
        if ($start -lt 0) { $lines.Add("[$section]"); $lines.Add("$key=$value"); continue }
        $done = $false
        for ($i = $start + 1; $i -lt $end; $i++) {
            if ($lines[$i] -match ('^\s*' + [regex]::Escape($key) + '\s*=')) { $lines[$i] = "$key=$value"; $done = $true; break }
        }
        if (-not $done) { $lines.Insert($start + 1, "$key=$value") }
    }
    [System.IO.File]::WriteAllLines($path, $lines)     # ANSI-safe: the patch reads it with GetPrivateProfile
}
function Get-IniValue([string]$path, [string]$section, [string]$key, $default) {
    if (-not (Test-Path -LiteralPath $path)) { return $default }
    $in = $false
    foreach ($l in (Get-Content -LiteralPath $path)) {
        if ($l -match '^\s*\[(.+?)\]\s*$') { $in = ($Matches[1] -ieq $section); continue }
        if ($in -and $l -match ('^\s*' + [regex]::Escape($key) + '\s*=\s*([^;]*)')) { return $Matches[1].Trim() }
    }
    return $default
}

function Get-Shot($proc) {
    if (-not $CanDraw) { return $null }
    try {
        $h = Get-GameWindow $proc
        $r = New-Object Hfr.Win+RECT; $p = New-Object Hfr.Win+POINT
        if (-not [Hfr.Win]::GetClientRect($h, [ref]$r) -or $r.Right -lt 16 -or $r.Bottom -lt 16) { return $null }
        [void][Hfr.Win]::ClientToScreen($h, [ref]$p)
        $bmp = New-Object System.Drawing.Bitmap($r.Right, $r.Bottom, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        $gfx.CopyFromScreen($p.X, $p.Y, 0, 0, $bmp.Size); $gfx.Dispose()
        $data = $bmp.LockBits((New-Object System.Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height)), 'ReadOnly', $bmp.PixelFormat)
        $px = New-Object int[] ($bmp.Width * $bmp.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
        $bmp.UnlockBits($data)
        return [pscustomobject]@{ Bitmap = $bmp; Pixels = $px }
    } catch { return $null }
}
function Save-Shot($shot, [string]$path) {
    if ($shot) { try { $shot.Bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) } catch { } }
}

function Get-ExpectedSignatures([string]$tag) {
    if (-not $Repo) { return 0 }
    $json = Join-Path $Repo ('tools\{0}_signatures.json' -f $tag.ToLower())
    if (Test-Path -LiteralPath $json) { try { return @((Get-Content -LiteralPath $json -Raw | ConvertFrom-Json)).Count } catch { } }
    return 0
}

# ------------------------------------------------------------------ one launch
# Starts the game under this case's INI, runs the body, and always closes the game, restores
# the INI and files the log.
function Invoke-Launch($g, $case, [scriptblock]$body) {
    $ini = Join-Path $g.Dir 'touhou_hfr.ini'; $bak = "$ini.hfrtest"
    $log = Join-Path $g.Dir 'touhou_hfr.log'
    $ctx = [pscustomobject]@{ Game = $g; Case = $case; Log = $log; Ini = $ini; Proc = $null; Notes = (New-Object System.Collections.Generic.List[string])
                              Cells = [ordered]@{}; Refresh = 0; Prefix = (Join-Path $OutDir "$($g.Tag)-$($case.Name)"); Lines = @() }
    $hadIni = Test-Path -LiteralPath $ini
    try {
        if ($hadIni) { Copy-Item -LiteralPath $ini -Destination $bak -Force }
        $over = @{ 'hfr.log' = 1 }; foreach ($k in $case.Ini.Keys) { $over[$k] = $case.Ini[$k] }
        Set-IniValues $ini $over
        Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue

        $started = Get-Date
        if ($case.Launcher) {
            [void](Start-Process -FilePath (Join-Path $g.Dir 'touhou_hfr.exe') -WorkingDirectory $g.Dir -PassThru)
        } else {
            $ctx.Proc = Start-Process -FilePath $g.Exe -WorkingDirectory $g.Dir -PassThru
        }
        $deadline = $started.AddSeconds($LaunchTimeoutSec); $hwnd = [IntPtr]::Zero
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 400
            if ($case.Launcher -and -not $ctx.Proc) {
                $ctx.Proc = Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($g.Exe)) -ErrorAction SilentlyContinue |
                    Where-Object { $_.StartTime -ge $started.AddSeconds(-1) } | Select-Object -First 1
                continue
            }
            if (-not $ctx.Proc) { continue }
            if ($ctx.Proc.HasExited) { break }
            $hwnd = Get-GameWindow $ctx.Proc
            if ($hwnd -ne [IntPtr]::Zero) { break }
        }
        if (-not $ctx.Proc)        { $ctx.Cells.Launch = 'FAIL'; $ctx.Notes.Add('the game process never appeared'); return $ctx }
        if ($ctx.Proc.HasExited)   { $ctx.Cells.Launch = 'FAIL'; $ctx.Notes.Add("the game exited during start-up, code $($ctx.Proc.ExitCode)"); return $ctx }
        if ($hwnd -eq [IntPtr]::Zero) { $ctx.Cells.Launch = 'FAIL'; $ctx.Notes.Add("no window within $LaunchTimeoutSec s"); return $ctx }
        $ctx.Cells.Launch = 'ok'
        [void](Wait-Log $log '^menu: ready' 15 $ctx.Proc)
        Start-Sleep -Seconds 2                       # TH08 has replaced its window by now
        if (-not (Set-GameFocus $ctx.Proc)) { $ctx.Notes.Add('the window never got focus: key results are unreliable. Run from an interactive console and do not click away') }

        $hz = @(Read-Log $log | Select-String -Pattern '^display refresh detected: (\d+) Hz' | Select-Object -Last 1)
        if ($hz) { $ctx.Refresh = [int]$hz[0].Matches[0].Groups[1].Value }

        $null = & $body $ctx

        Test-Health $ctx
    }
    catch { $ctx.Notes.Add("script error: $($_.Exception.Message)") }
    finally {
        # Each step on its own: nothing that fails here may stop the INI going back.
        try { foreach ($k in $VK.Values) { [void][Hfr.Win]::Key([uint16]$k, $false) } } catch { }     # nothing stays held
        try {
            if ($ctx.Proc -and -not $ctx.Proc.HasExited -and -not $KeepOpen) {
                [void]$ctx.Proc.CloseMainWindow()
                if (-not $ctx.Proc.WaitForExit(6000)) {
                    $ctx.Proc.Kill(); [void]$ctx.Proc.WaitForExit(5000)
                    $ctx.Notes.Add('the game ignored the close request and was killed'); $ctx.Cells.Exit = 'killed'
                } else { $ctx.Cells.Exit = 'ok' }
            }
        } catch { $ctx.Notes.Add("could not close the game: $($_.Exception.Message)") }
        Start-Sleep -Milliseconds 400
        try { if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination "$($ctx.Prefix).log" -Force } } catch { }
        try {
            if ($hadIni) { Move-Item -LiteralPath $bak -Destination $ini -Force } else { Remove-Item -LiteralPath $ini -Force -ErrorAction SilentlyContinue }
        } catch { Write-Host "  !! could not restore $ini; the original is $bak"; $ctx.Notes.Add("the INI was not restored: $bak holds the original") }
    }
    return $ctx
}

# What every launch is judged on.
function Test-Health($ctx) {
    $lines = Read-Log $ctx.Log; $ctx.Lines = $lines; $tag = $ctx.Game.Tag
    if (-not $lines) { $ctx.Cells.Build = 'no log'; $ctx.Notes.Add('touhou_hfr.log is missing or empty'); return }

    $install = @($lines | Select-String -Pattern '^Installed TH')
    if ($install.Count -eq 1 -and $install[0].Line -match '(\d+) verified signatures') {
        $n = [int]$Matches[1]; $want = Get-ExpectedSignatures $tag
        if     ($want -eq 0)   { $ctx.Cells.Build = "$n (?)" }
        elseif ($n -eq $want)  { $ctx.Cells.Build = 'ok' }
        else { $ctx.Cells.Build = "$n<>$want"; $ctx.Notes.Add("signature count $n, the checkout expects ${want}: the game is running another build") }
    } elseif ($install.Count -gt 1) { $ctx.Cells.Build = "x$($install.Count)"; $ctx.Notes.Add("the patch installed $($install.Count) times in one process") }
    else { $ctx.Cells.Build = 'none'; $ctx.Notes.Add('no install line: the patch did not install') }

    $bad = @($lines | Select-String -Pattern ($NeverOk -join '|'))
    $ctx.Cells.Log = if ($bad.Count) { "$($bad.Count) bad" } else { 'ok' }
    foreach ($b in ($bad | Select-Object -First 6)) { $ctx.Notes.Add("log: $($b.Line)") }

    foreach ($e in @($ctx.Case.Expect)) { if ($e -and (Count-Lines $lines $e) -eq 0) { $ctx.Notes.Add("expected a log line matching /$e/") } }
    foreach ($f in @($ctx.Case.Forbid)) { if ($f -and (Count-Lines $lines $f) -gt 0) { $ctx.Notes.Add("unexpected log line matching /$f/") } }

    $ctx.Proc.Refresh()
    if ($ctx.Proc.HasExited) { $ctx.Cells.Crash = "exit $($ctx.Proc.ExitCode)"; $ctx.Notes.Add("the game exited on its own, code $($ctx.Proc.ExitCode)") }
    elseif ([Hfr.Win]::IsHungAppWindow((Get-GameWindow $ctx.Proc))) { $ctx.Cells.Crash = 'hung'; $ctx.Notes.Add('the window is not responding') }
    else { $ctx.Cells.Crash = 'ok' }
}

# Frame statistics from the lines written since `skip`. `ticks` is 'target', 60, or $null.
function Test-Stats($ctx, [int]$skip, $ticks, [int]$target = 0) {
    $stats = @(Get-Stats (Read-Log $ctx.Log))
    if ($stats.Count -gt $skip) { $stats = @($stats | Select-Object -Skip $skip) }
    if (-not $stats) { $ctx.Cells.Fps = 'no stats'; $ctx.Notes.Add('no stats line: the patch is not driving frames'); return }
    $s = $stats[-1]
    $ctx.Cells.Fps = "$([int]$s.Presents)/$($s.Target)"
    if ($s.Target -le 0 -or $s.Presents / $s.Target -lt 0.85) { $ctx.Notes.Add("presenting at $([int]$s.Presents)/s against a target of $($s.Target)") }
    if ($target -and $s.Target -ne $target) { $ctx.Notes.Add("target is $($s.Target), this case expects $target") }
    if (-not $target -and $ctx.Refresh -and [math]::Abs($s.Target - $ctx.Refresh) -gt 2 -and -not (Get-IniValue $ctx.Ini 'hfr' 'fps' 0)) {
        $ctx.Notes.Add("the patch targets $($s.Target) Hz on a $($ctx.Refresh) Hz display")
    }
    $want = if ($ticks -eq 'target') { $s.Target } else { $ticks }
    if ($want) {
        $ctx.Cells.Ticks = "$([int]$s.Ticks)/$want"
        if ([math]::Abs($s.Ticks - $want) / $want -gt 0.12) { $ctx.Notes.Add("simulation at $([int]$s.Ticks) ticks/s, expected about $want") }
    }
    # A repeated frame is a present with no new tick behind it: expect 1 - ticks/presents.
    if ($s.Frames -gt 0 -and $s.Presents -gt 0) {
        $seen = $s.Repeated / $s.Frames; $due = [math]::Max(0.0, 1.0 - $s.Ticks / $s.Presents)
        if ([math]::Abs($seen - $due) -gt 0.15) { $ctx.Notes.Add(('repeated frames {0:P0}, but {1:N0} ticks/s under {2:N0} presents/s predicts {3:P0}' -f $seen, $s.Ticks, $s.Presents, $due)) }
    }
}

# ------------------------------------------------------------------ case bodies
$BaseBody = {
    param($ctx)
    $log = $ctx.Log; $proc = $ctx.Proc; $traits = $Traits[$ctx.Game.Tag]

    # The picture is there at all.
    $shot = Get-Shot $proc; Save-Shot $shot "$($ctx.Prefix)-title.png"
    if ($shot) {
        $spread = [Hfr.Pixels]::Spread($shot.Pixels)
        $ctx.Cells.Picture = if ($spread -ge 4) { 'ok' } else { 'BLANK' }
        if ($spread -lt 4) { $ctx.Notes.Add(('the window is one flat colour (luma spread {0:N1}); see {1}-title.png' -f $spread, (Split-Path $ctx.Prefix -Leaf))) }
    } else { $ctx.Cells.Picture = 'n/a' }

    # F10: exactly one "size cycle" line per press. Two means the game has a handler of its own
    # and native_size_cycle is wrong in its profile. The first press after loading is sometimes
    # swallowed, so one uncounted press goes first.
    $before = Count-Lines (Read-Log $log) 'window: size cycle'
    [void](Send-Key $proc $VK.F10); Start-Sleep -Milliseconds 800
    $prev = Count-Lines (Read-Log $log) 'window: size cycle'
    Write-Host ("    F10 warm-up: {0}" -f $(if ($prev -gt $before) { 'registered' } else { 'ignored' }))
    $start = $prev; $sizes = @(); $double = 0
    # One more than a whole cycle (three presets and borderless), so every step is seen.
    $n = [math]::Max($Presses, 5)
    for ($i = 1; $i -le $n; $i++) {
        [void](Set-GameFocus $proc)
        [void](Send-Key $proc $VK.F10); Start-Sleep -Milliseconds 900
        $now = Count-Lines (Read-Log $log) 'window: size cycle'; $delta = $now - $prev; $prev = $now
        if ($delta -gt 1) { $double++ }
        $size = Get-ClientSize (Get-GameWindow $proc); $sizes += $size
        Write-Host ("    F10 {0}: +{1} line(s), client {2}" -f $i, $delta, $size)
    }
    $cycles = $prev - $start
    if     ($double)            { $ctx.Cells.F10 = 'DOUBLE'; $ctx.Notes.Add('F10 steps twice per press: the game has an F10 handler of its own') }
    elseif ($cycles -eq $n)     { $ctx.Cells.F10 = "ok ($cycles/$n)" }
    elseif ($cycles -eq 0)      { $ctx.Cells.F10 = 'DEAD'; $ctx.Notes.Add('F10 does nothing') }
    else                        { $ctx.Cells.F10 = "$cycles/$n"; $ctx.Notes.Add("F10 gave $cycles log lines for $n presses") }
    if ($cycles -gt 0) {
        $distinct = @($sizes | Select-Object -Unique); $desktop = Get-MonitorSize (Get-GameWindow $proc)
        $odd = @($distinct | Where-Object { @('640x480', '960x720', '1280x960', $desktop, '?') -notcontains $_ })
        # The presets are 100/150/200 % of the game's own resolution, skipping those that do not fit
        # the work area, then borderless. TH14 on can run at 1280x960, where only 100 % fits a
        # 1440-line desktop, so the number of sizes to expect depends on both.
        $want = 3
        if ($sizes[-1] -match '^\d+x\d+$' -and $desktop -match '^(\d+)x(\d+)$') {
            $dw = [int]$Matches[1]; $dh = [int]$Matches[2]
            $native = @($distinct | Where-Object { $_ -match '^\d+x\d+$' -and $_ -ne $desktop } | Sort-Object { [int]($_ -split 'x')[0] })[0]
            if ($native) {
                $nw, $nh = $native -split 'x' | ForEach-Object { [int]$_ }
                $fit = @(100, 150, 200 | Where-Object { $nw * $_ / 100 -le $dw - 16 -and $nh * $_ / 100 -le $dh - 80 }).Count
                $want = [math]::Min(3, [math]::Max(1, $fit) + 1)
            }
        }
        if ($distinct.Count -lt $want) { $ctx.Notes.Add("F10 cycled in the log but the window only took $($distinct -join ', ') (expected $want sizes)") }
        if ($odd)                  { $ctx.Notes.Add("unexpected client sizes: $($odd -join ', ') (desktop $desktop)") }
        $lines = Read-Log $log
        if ((Count-Lines $lines 'size cycle -> borderless') -gt 0 -and $distinct -notcontains $desktop) { $ctx.Notes.Add("borderless fullscreen was cycled to but the client never became $desktop") }
    }
    # Leave it in a window for the rest.
    for ($i = 0; $i -lt 4 -and (Get-ClientSize (Get-GameWindow $proc)) -eq (Get-MonitorSize (Get-GameWindow $proc)); $i++) { [void](Send-Key $proc $VK.F10); Start-Sleep -Milliseconds 900 }

    # F11: the log says it opened, the pixels say it was drawn. A menu bound to a dead device
    # logs "opened" and shows nothing, which is what TH08 did before its device recreation
    # was handled.
    [void](Set-GameFocus $proc)
    $a = Get-Shot $proc
    $before = Count-Lines (Read-Log $log) '^menu: (opened|closed)'
    [void](Send-Key $proc $VK.F11); Start-Sleep -Milliseconds 1200
    $b = Get-Shot $proc; Save-Shot $b "$($ctx.Prefix)-menu.png"
    [void](Send-Key $proc $VK.F11); Start-Sleep -Milliseconds 900
    $c = Get-Shot $proc
    $delta = (Count-Lines (Read-Log $log) '^menu: (opened|closed)') - $before
    if     ($delta -eq 2) { $ctx.Cells.F11 = 'ok' }
    elseif ($delta -eq 0) { $ctx.Cells.F11 = 'DEAD'; $ctx.Notes.Add('F11 did not open the menu') }
    else                  { $ctx.Cells.F11 = "$delta/2"; $ctx.Notes.Add("F11 produced $delta menu lines instead of 2") }
    if ($delta -ge 1 -and $a -and $b -and $c) {
        $open = [Hfr.Pixels]::Changed($a.Pixels, $b.Pixels, 40); $idle = [Hfr.Pixels]::Changed($a.Pixels, $c.Pixels, 40)
        Write-Host ('    F11 pixels: {0:P1} changed with the menu open, {1:P1} without' -f $open, $idle)
        if ($open -ge 0 -and $open -lt [math]::Max(0.03, $idle + 0.03)) {
            $ctx.Cells.F11 = 'UNSEEN'; $ctx.Notes.Add(('the menu logged "opened" but the picture did not change ({0:P1}); see {1}-menu.png' -f $open, (Split-Path $ctx.Prefix -Leaf)))
        }
    }
    if (Count-Lines (Read-Log $log) 'menu: key seen but the game does not have focus') { $ctx.Notes.Add('the patch saw the menu key without focus') }

    # Idle: frame statistics (every 5 s) and the attract-mode demo (about 30 s in).
    if ($IdleSeconds -gt 0) {
        Write-Host "    idling up to ${IdleSeconds}s..."
        $end = (Get-Date).AddSeconds($IdleSeconds)
        while ((Get-Date) -lt $end -and -not $proc.HasExited) {
            Start-Sleep -Seconds 2
            $peek = Read-Log $log
            if ((Count-Lines $peek '^stats: ') -ge 2 -and (-not $traits.Demo -or (Count-Lines $peek 'replay playback started') -gt 0)) { break }
        }
        $lines = Read-Log $log
        $demo = Count-Lines $lines 'replay playback started'
        # Once the demo is playing the simulation is at the replay's 60; before it, the title
        # screen runs at the display's rate unless the game is a fixed-logic one.
        $ticks = if ($traits.Fixed -or $demo -or -not [int](Get-IniValue $ctx.Ini 'hfr' 'substep' 1)) { $null } else { 'target' }
        Test-Stats $ctx 1 $ticks
        if (-not $traits.Demo)  { $ctx.Cells.Demo = 'n/a' }
        elseif ($demo)          { $ctx.Cells.Demo = 'ok' }
        else                    { $ctx.Cells.Demo = 'none'; $ctx.Notes.Add("no attract-mode demo within ${IdleSeconds}s") }
    }
}

$QuickBody = {
    param($ctx)
    $traits = $Traits[$ctx.Game.Tag]
    [void](Wait-Log $ctx.Log '^stats: ' ($QuickSeconds + 8) $ctx.Proc)
    Start-Sleep -Seconds ([math]::Max(0, $QuickSeconds - 5))
    $shot = Get-Shot $ctx.Proc; Save-Shot $shot "$($ctx.Prefix).png"
    if ($shot -and [Hfr.Pixels]::Spread($shot.Pixels) -lt 4) { $ctx.Cells.Picture = 'BLANK'; $ctx.Notes.Add('the window is one flat colour') } elseif ($shot) { $ctx.Cells.Picture = 'ok' }
    $substep = [int](Get-IniValue $ctx.Ini 'hfr' 'substep' 1)
    $ticks = if ($traits.Fixed) { 60 } elseif ($substep) { 'target' } else { $null }
    Test-Stats $ctx 0 $ticks ([int]$ctx.Case.Target)
}

# Start a stage and play it. Z is accepted by every menu between the title and the stage and is
# the fire button once there, so it is pressed until the log says a stage began.
$DriveBody = {
    param($ctx)
    $log = $ctx.Log; $proc = $ctx.Proc; $traits = $Traits[$ctx.Game.Tag]
    Start-Sleep -Seconds 4
    $in = $false
    for ($i = 0; $i -lt 30; $i++) {
        if (-not (Set-GameFocus $proc)) { $ctx.Cells.Drive = 'no focus'; $ctx.Notes.Add('lost focus before the stage began; drive abandoned'); return }
        [void](Send-Key $proc $VK.Z 90); Start-Sleep -Milliseconds 1100
        if ($traits.Stage) { if (Count-Lines (Read-Log $log) '^stage \d+ first frame \(recording\)') { $in = $true; break } }
        elseif ($i -ge 9) { break }
    }
    if ($traits.Stage -and -not $in) {
        # Not a fault of the patch: the game did not take injected keys (or a menu wanted
        # something other than Z). Reported, not judged.
        $ctx.Cells.Drive = 'no stage'; Write-Host '    the game never started a stage from injected keys; nothing judged'
        return
    }
    $ctx.Cells.Drive = if ($in) { 'in stage' } else { 'blind' }
    $statsBefore = @(Get-Stats (Read-Log $log)).Count
    $censusBefore = Count-Lines (Read-Log $log) '^site census'

    Write-Host "    holding fire and moving for ${DriveSeconds}s..."
    $end = (Get-Date).AddSeconds($DriveSeconds); $dir = $VK.Left; $lost = $false
    if (-not (Send-KeyState $proc $VK.Z $true)) { $lost = $true }
    while ((Get-Date) -lt $end -and -not $lost -and -not $proc.HasExited) {
        if (-not (Send-KeyState $proc $dir $true)) { $lost = $true; break }
        Start-Sleep -Milliseconds 450
        [void][Hfr.Win]::Key([uint16]$dir, $false)
        $dir = if ($dir -eq $VK.Left) { $VK.Right } else { $VK.Left }
    }
    $shot = Get-Shot $proc; Save-Shot $shot "$($ctx.Prefix)-stage.png"
    [void][Hfr.Win]::Key([uint16]$VK.Z, $false)
    if ($lost) { $ctx.Notes.Add('the game lost focus while being driven; the rest of this case is not reliable') }

    # In a stage the simulation runs at the display's rate when sub-stepping is on.
    $substep = [int](Get-IniValue $ctx.Ini 'hfr' 'substep' 1)
    $ticks = if ($traits.Fixed) { 60 } elseif ($substep) { 'target' } else { $null }
    Test-Stats $ctx $statsBefore $ticks
    $s = @(Get-Stats (Read-Log $log)) | Select-Object -Skip $statsBefore
    if ($s) { $ctx.Cells.Polls = ($s | Measure-Object -Property Polls -Maximum).Maximum }
    # Sub-tick input: every sub-stepped game polls between frame boundaries while a stage runs.
    if ($s -and $in -and -not $traits.Fixed -and $substep -and $ctx.Cells.Polls -eq 0 -and [int](Get-IniValue $ctx.Ini 'hfr' 'subtick_input' 1)) {
        $ctx.Notes.Add('no sub-tick input polls in a stage: the input path is not described, or the stage start was not seen')
    }

    # TH14's firing cycle once stopped after two steps while the button was held. Shots per
    # game frame, window by window: every window after the first must have some.
    if ($traits.Shots) {
        $rates = @(Read-Log $log | Select-String -Pattern '^site census.* over (\d+) frames: .*shots=([\d.]+)' | Select-Object -Skip $censusBefore |
                   Where-Object { [int]$_.Matches[0].Groups[1].Value -gt 100 } | ForEach-Object { [double]$_.Matches[0].Groups[2].Value })
        if ($rates.Count -ge 3) {
            $held = $rates | Select-Object -Skip 1 | Select-Object -SkipLast 1
            $lo = ($held | Measure-Object -Minimum).Minimum; $hi = ($held | Measure-Object -Maximum).Maximum
            $ctx.Cells.Shots = ('{0:N2}-{1:N2}' -f $lo, $hi)
            if ($lo -le 0.05)             { $ctx.Notes.Add("firing stopped while the button was held (shots per frame by window: $($rates -join ', '))") }
            # No evenness test: the counter is live shots per frame, which follows how many are on
            # screen (16 to 90 in one stage while firing never paused), not how many were fired.
        } else { $ctx.Cells.Shots = 'too short'; Write-Host '    not enough census windows to judge the shot cycle; raise -DriveSeconds' }
    }
    [void](Send-Key $proc $VK.Esc); Start-Sleep -Milliseconds 800
}

# ------------------------------------------------------------------ find the games
if (-not $Root) { $Root = $PSScriptRoot }
if (-not (Test-Path -LiteralPath $Root)) { throw "Root folder not found: $Root" }
if (-not $Repo -and (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'tools'))) { $Repo = $PSScriptRoot }
if (-not $OutDir) { $OutDir = Join-Path $Root ('hfr-test-results\' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
[void](New-Item -ItemType Directory -Force -Path $OutDir)

$found = @(); $skipped = @()
foreach ($dir in (Get-ChildItem -LiteralPath $Root -Directory)) {
    if ($dir.Name -notmatch '^\((TH\d\d)\)') { continue }
    $tag = $Matches[1]; $exe = Join-Path $dir.FullName "$($tag.ToLower()).exe"
    if ($Games -and ($Games -notcontains $tag)) { continue }
    if (-not $Traits.ContainsKey($tag))                                    { $skipped += "$($dir.Name): not a supported game"; continue }
    if (-not (Test-Path -LiteralPath $exe))                                { $skipped += "$($dir.Name): no $($tag.ToLower()).exe"; continue }
    if (-not (Test-Path -LiteralPath (Join-Path $dir.FullName 'dinput8.dll'))) { $skipped += "$($dir.Name): dinput8.dll is not installed"; continue }
    $found += [pscustomobject]@{ Tag = $tag; Dir = $dir.FullName; Exe = $exe }
    # Left behind by a run that was killed: put the player's INI back before anything else.
    $stale = Join-Path $dir.FullName 'touhou_hfr.ini.hfrtest'
    if (Test-Path -LiteralPath $stale) { Move-Item -LiteralPath $stale -Destination (Join-Path $dir.FullName 'touhou_hfr.ini') -Force; Write-Host "restored $($tag)'s INI from an interrupted run" }
}
$found = @($found | Sort-Object Tag)

$run = @($AllCases | Where-Object {
    if ($Cases) { $Cases -contains $_.Name }
    else { $_.Kind -eq 'base' -or ($Matrix -and $_.Kind -eq 'quick') -or ($Drive -and $_.Kind -eq 'drive') }
})

Write-Host ''
Write-Host "Touhou HFR in-game tests: $($found.Count) game(s), $($run.Count) case(s) each"
Write-Host "Root:    $Root"
Write-Host "Results: $OutDir"
if ($Repo) { Write-Host "Repo:    $Repo" } else { Write-Host 'Repo:    none given, so builds are reported and not judged' }
foreach ($s in $skipped) { Write-Host "skipped  $s" }
if (-not $found) { Write-Host 'Nothing to test: no game folder under this root has dinput8.dll in it.'; exit 2 }

# ------------------------------------------------------------------ installed files
# Every game should carry the same runtime, and the one this checkout built.
$fileNotes = @()
$built = if ($Repo) { Join-Path $Repo 'build\touhou_hfr.dll' } else { $null }
$builtHash = if ($built -and (Test-Path -LiteralPath $built)) { (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash } else { $null }
$hashes = @{}
foreach ($g in $found) {
    foreach ($f in 'dinput8.dll', 'touhou_hfr.dll') {
        $p = Join-Path $g.Dir $f
        if (Test-Path -LiteralPath $p) { $hashes["$($g.Tag)\$f"] = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
    }
}
$groups = @($hashes.GetEnumerator() | Group-Object Value)
if ($groups.Count -gt 1) {
    $fileNotes += 'the installed runtimes are not all the same file:'
    foreach ($grp in $groups) { $fileNotes += ('   {0}  {1}' -f $grp.Name.Substring(0, 12), (($grp.Group | ForEach-Object { $_.Key }) -join ', ')) }
}
if ($builtHash) {
    $other = @($hashes.GetEnumerator() | Where-Object { $_.Value -ne $builtHash } | ForEach-Object { $_.Key })
    if ($other) { $fileNotes += "not the DLL in $built`: $($other -join ', ')" }
}
foreach ($n in $fileNotes) { Write-Host "files    $n" }
Write-Host ''

# ------------------------------------------------------------------ run
$results = @()
foreach ($g in $found) {
    Write-Host ('=' * 78); Write-Host "$($g.Tag)   $($g.Dir)"
    $refresh = 0
    foreach ($case in $run) {
        $why = if ($case.Skip) { & $case.Skip $g $Traits[$g.Tag] $refresh } else { $null }
        if ($why) { Write-Host "  $($case.Name): skipped ($why)"; continue }
        Write-Host "  $($case.Name)"
        $body = switch ($case.Kind) { 'base' { $BaseBody } 'quick' { $QuickBody } 'drive' { $DriveBody } }
        $ctx = @(Invoke-Launch $g $case $body)[-1]
        if ($ctx.Refresh) { $refresh = $ctx.Refresh }
        $verdict = if ($ctx.Notes.Count -eq 0) { 'PASS' } else { 'CHECK' }
        $cells = ($ctx.Cells.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join '  '
        Write-Host "    $verdict  $cells"
        foreach ($n in $ctx.Notes) { Write-Host "      - $n" }
        $results += [pscustomobject]@{ Game = $g.Tag; Case = $case.Name; Verdict = $verdict; Cells = $cells; Notes = @($ctx.Notes) }
    }
}

# ------------------------------------------------------------------ summary
$summary = @()
$summary += "Touhou HFR in-game tests  $(Get-Date -Format 'yyyy-MM-dd HH:mm')  root $Root"
$summary += $fileNotes | ForEach-Object { "files: $_" }
$summary += ''
foreach ($r in $results) {
    $summary += ('{0,-5} {1,-18} {2,-6} {3}' -f $r.Game, $r.Case, $r.Verdict, $r.Cells)
    foreach ($n in $r.Notes) { $summary += "        - $n" }
}
$bad = @($results | Where-Object { $_.Verdict -ne 'PASS' })
$summary += ''
$summary += if ($bad.Count -eq 0 -and -not $fileNotes) { "All $($results.Count) launches passed." }
            else { "$($bad.Count) of $($results.Count) launches need attention$(if ($fileNotes) { ', and the installed files differ' })." }
$summary | Set-Content -LiteralPath (Join-Path $OutDir 'summary.txt') -Encoding UTF8
Write-Host ('=' * 78); Write-Host 'SUMMARY   (also in summary.txt, with each launch''s log and screenshots beside it)'; Write-Host ''
$summary | ForEach-Object { Write-Host $_ }
if ($bad.Count -eq 0 -and -not $fileNotes) { exit 0 } else { exit 1 }
