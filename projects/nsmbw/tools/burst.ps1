# Capture a quick burst of screenshots of a running NSMBWCompiled window (for checking whether
# something animates frame to frame). shot.ps1 sleeps 700 ms per shot to let the window come to
# the front; this focuses once and then grabs $Count frames $IntervalMs apart.
#   burst.ps1 -ProcId <pid> -OutPrefix build_nsmbw/shots/coins -Count 8 -IntervalMs 130
param([int]$ProcId, [string]$OutPrefix, [int]$Count = 8, [int]$IntervalMs = 130)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WB {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Get-Process -Id $ProcId -ErrorAction SilentlyContinue
if (-not $p) { Write-Output "NOPROC"; exit 1 }
$h = $p.MainWindowHandle
[void][WB]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700
$r = New-Object WB+RECT
[void][WB]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
for ($i = 0; $i -lt $Count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $bmp.Save("${OutPrefix}_$i.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Start-Sleep -Milliseconds $IntervalMs
}
Write-Output "OK $Count frames -> ${OutPrefix}_*.png"
