# Run a portable copy of NSMBWCompiled (its own UserData\ save + config, so the developer's real save
# is never touched), wait for a scene, and screenshot it with PrintWindow.
#   portable_shot.ps1 -PortableDir <dir with exe + portable.txt> -Tag w2 -Scene 3 -EnvVars @('NSMBW_DEBUG_NO_FOG=1')
param([string]$PortableDir, [string]$Tag, [int]$Scene = 3, [string[]]$EnvVars = @(), [int]$SettleSec = 12, [int]$TimeoutSec = 150)
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$B = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $tools))) "build_nsmbw"
$all = @('NSMBW_AUTO_PRESS_SELFTEST=1', "NSMBW_AUTO_PRESS_STOP_SCENE=$Scene") + $EnvVars
foreach ($kv in $all) { $k, $v = $kv.Split('=', 2); [Environment]::SetEnvironmentVariable($k, $v, 'Process') }
$err = Join-Path $B "nsmbw_$Tag.err.log"
$p = Start-Process -FilePath (Join-Path $PortableDir "NSMBWCompiled.exe") -WorkingDirectory $B `
    -RedirectStandardOutput (Join-Path $B "nsmbw_$Tag.log") -RedirectStandardError $err -PassThru
$pattern = "createRoot\(profile=0x{0:X}\)" -f $Scene
$t = 0; while ($t -lt $TimeoutSec -and -not $p.HasExited) { Start-Sleep 1; $t++; if (Select-String -Path $err -Pattern $pattern -Quiet -ErrorAction SilentlyContinue) { break } }
Start-Sleep $SettleSec
& (Join-Path $tools "burst.ps1") -ProcId $p.Id -OutPrefix (Join-Path $B "shots\$Tag") -Count 1 -IntervalMs 10 | Out-Null
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Output "$Tag : scene $Scene after ${t}s -> shots\${Tag}_0.png"
