# scripts/screenshot-qvim.ps1
#
# Launches qvim.exe with a file (and optional init.vim) and saves a PNG of the
# rendered window. Use for visual validation of paint-path changes — ligatures,
# emoji, selection rendering, cursor shapes, etc.
#
# Qt Quick on Windows ignores WM_PRINTCLIENT (PrintWindow returns black) and
# SetForegroundWindow is gated by Windows' foreground-steal prevention, so this
# script does: launch -> wait -> SetForegroundWindow -> PrintWindow with
# PW_RENDERFULLCONTENT (0x2, captures DirectComposition/hardware surfaces).
#
# Example:
#   pwsh scripts/screenshot-qvim.ps1 -File D:\some.txt -InitFile D:\some.vim
#
# Returns the PNG path on stdout on success.

param(
    [string]$Exe       = "D:\qvim\build\dev\Debug\qvim.exe",
    [string]$File      = "",
    [string]$InitFile  = "",                          # -u <path> for nvim
    [string[]]$ExtraArgs = @(),                       # appended to argv
    [int]$SettleMs     = 5000,
    [string]$OutPath   = "D:\qvim\_qvim_screenshot.png"
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName System.Drawing

# Build argv: -u <init> -i NONE  <extra>  <file>
$argv = @()
if ($InitFile) { $argv += @("-u", $InitFile) } else { $argv += @("-u", "NONE") }
$argv += @("-i", "NONE")
$argv += $ExtraArgs
if ($File) { $argv += $File }

Write-Host "Launching qvim: $Exe $($argv -join ' ')"
$proc = Start-Process -FilePath $Exe -ArgumentList $argv -PassThru
Start-Sleep -Milliseconds $SettleMs

if (-not ("Win32Capture" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Win32Capture {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT pvAttribute, int cb);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
}

# Refresh the process object so MainWindowHandle is populated after the GUI shows.
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Error "qvim has no main window after ${SettleMs}ms — bump -SettleMs."
    $proc | Stop-Process -Force -ErrorAction SilentlyContinue
    exit 1
}

if ([Win32Capture]::IsIconic($hwnd)) {
    [Win32Capture]::ShowWindow($hwnd, 9) | Out-Null  # SW_RESTORE
    Start-Sleep -Milliseconds 300
}
[Win32Capture]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

# Prefer the DWM extended-frame rect (excludes drop shadow) over GetWindowRect.
$rect = New-Object Win32Capture+RECT
$dwmExtendedFrameBounds = 9
$hr = [Win32Capture]::DwmGetWindowAttribute($hwnd, $dwmExtendedFrameBounds, [ref]$rect,
    [System.Runtime.InteropServices.Marshal]::SizeOf($rect))
if ($hr -ne 0) {
    [Win32Capture]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
}
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) {
    Write-Error "Invalid window rect: ${w}x${h}"
    $proc | Stop-Process -Force -ErrorAction SilentlyContinue
    exit 1
}

$bmp = New-Object System.Drawing.Bitmap $w, $h
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $gfx.GetHdc()
$ok = [Win32Capture]::PrintWindow($hwnd, $hdc, 2)   # PW_RENDERFULLCONTENT
$gfx.ReleaseHdc($hdc)
$gfx.Dispose()
if (-not $ok) {
    Write-Error "PrintWindow failed"
    $bmp.Dispose()
    $proc | Stop-Process -Force -ErrorAction SilentlyContinue
    exit 1
}

$outDir = Split-Path -Parent $OutPath
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

$proc | Stop-Process -Force -ErrorAction SilentlyContinue
Write-Output $OutPath
