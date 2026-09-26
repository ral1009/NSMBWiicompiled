# Capture a quick burst of screenshots of a running NSMBWCompiled window (for checking whether
# something animates frame to frame). Uses PrintWindow(PW_RENDERFULLCONTENT), which asks the window
# to render itself, so it works even when Windows refuses to bring the window to the front
# (a screen copy then captures whatever is on top instead).
#   burst.ps1 -ProcId <pid> -OutPrefix build_nsmbw/shots/coins -Count 8 -IntervalMs 130
param([int]$ProcId, [string]$OutPrefix, [int]$Count = 8, [int]$IntervalMs = 130)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WB {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Get-Process -Id $ProcId -ErrorAction SilentlyContinue
if (-not $p) { Write-Output "NOPROC"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "NOWINDOW"; exit 1 }
$r = New-Object WB+RECT
[void][WB]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
for ($i = 0; $i -lt $Count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][WB]::PrintWindow($h, $hdc, 2)   # 2 = PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $bmp.Save("${OutPrefix}_$i.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Start-Sleep -Milliseconds $IntervalMs
}
Write-Output "OK $Count frames -> ${OutPrefix}_*.png"
