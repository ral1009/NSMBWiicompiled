# Start NSMBWCompiled with the given env vars, wait until a pattern appears in its stderr log, then
# grab a screenshot of *this* instance's window and stop it. Used for A/B checks on the title-screen
# demo (e.g. wait for "armed by liquid" = a water level is being drawn).
#   capture_on_log.ps1 -Tag ab_on -Pattern "armed by liquid" -EnvVars @('NSMBW_LOG_LIQUID=1') -DelayMs 1500
param([string]$Tag, [string]$Pattern, [string[]]$EnvVars = @(), [int]$TimeoutSec = 400, [int]$DelayMs = 1500)
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$B = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $tools))) "build_nsmbw"
foreach ($kv in $EnvVars) { $k, $v = $kv.Split('=', 2); [Environment]::SetEnvironmentVariable($k, $v, 'Process') }
$err = Join-Path $B "nsmbw_$Tag.err.log"
$p = Start-Process -FilePath (Join-Path $B "NSMBWCompiled.exe") -WorkingDirectory $B `
    -RedirectStandardOutput (Join-Path $B "nsmbw_$Tag.log") -RedirectStandardError $err -PassThru
$t = 0; $hit = $false
while ($t -lt $TimeoutSec -and -not $p.HasExited) {
    Start-Sleep 1; $t++
    if (Select-String -Path $err -Pattern $Pattern -Quiet -ErrorAction SilentlyContinue) { $hit = $true; break }
}
if ($hit) {
    Start-Sleep -Milliseconds $DelayMs
    & (Join-Path $tools "burst.ps1") -ProcId $p.Id -OutPrefix (Join-Path $B "shots/$Tag") -Count 2 -IntervalMs 400 | Out-Null
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Write-Output "$Tag : pattern seen=$hit after ${t}s"
