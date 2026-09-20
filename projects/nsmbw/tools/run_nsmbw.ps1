# Launches build_nsmbw\NSMBWCompiled.exe for a fixed time with the given env vars, writes
# stdout/stderr to build_nsmbw\nsmbw_<Tag>.log / .err.log, and takes a window screenshot every
# -ShotEvery seconds into -ShotDir (default: build_nsmbw\shots). Kills the game at the end.
#
#   .\projects\nsmbw\tools\run_nsmbw.ps1 -Tag dl1 -Seconds 120 -ShotEvery 10 `
#       -EnvVars @('NSMBW_LOG_STATE_CHANGES=1','NSMBW_AUTO_PRESS_SELFTEST=1')
#
# NSMBW_AUTO_PRESS_SELFTEST=1 presses A every 60 ticks (through the strap screens and into the
# title / file select); NSMBW_AUTO_PRESS_TICKS=<n> bounds that window.
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$Seconds = 120,
    [int]$ShotEvery = 15,
    [string[]]$EnvVars = @(),
    [string]$ShotDir = ""
)
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $tools "..\..\..")
$B = Join-Path $root "build_nsmbw"
if ($ShotDir -eq "") { $ShotDir = Join-Path $B "shots" }
New-Item -ItemType Directory -Force $ShotDir | Out-Null
foreach ($kv in $EnvVars) { $k, $v = $kv.Split('=', 2); [Environment]::SetEnvironmentVariable($k, $v, 'Process') }
$out = Join-Path $B "nsmbw_$Tag.log"; $err = Join-Path $B "nsmbw_$Tag.err.log"
$p = Start-Process -FilePath (Join-Path $B "NSMBWCompiled.exe") -WorkingDirectory $B `
    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
$t = 0
while ($t -lt $Seconds) {
    Start-Sleep -Seconds 1; $t++
    if ($p.HasExited) { Write-Output "EXITED at ${t}s code=$($p.ExitCode)"; break }
    if (($t % $ShotEvery) -eq 0) { & (Join-Path $tools "shot.ps1") -ProcId $p.Id -OutPath (Join-Path $ShotDir "${Tag}_t$t.png") | Out-Null }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "killed after ${Seconds}s" }
Write-Output ("err lines: " + (Get-Content $err | Measure-Object -Line).Lines)
