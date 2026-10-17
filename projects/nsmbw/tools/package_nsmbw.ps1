# Assembles a folder someone else can run: build_nsmbw\NSMBWCompiled.exe plus every file it
# loads from its own directory, and nothing else (no logs, shots, profiles, or nsmbw_data -
# that folder holds this machine's shader cache and controller bindings and is recreated on
# first run). Writes dist\NSMBWCompiled\ and dist\NSMBWCompiled-<commit>.zip.
#
#   .\projects\nsmbw\tools\package_nsmbw.ps1
#   .\projects\nsmbw\tools\package_nsmbw.ps1 -Private
#
# -Private builds the developer's own no-setup folder instead (dist\NSMBWCompiled-private\, not
# zipped, NOT for sharing - it contains the extracted game): the same files plus portable.txt, so
# Config.toml, the NAND save and controller bindings live in UserData\ next to the exe; the
# extracted disc in disc\ with UserData\Config.toml's dvd_root = '../disc' (relative paths resolve
# from the config file's folder); the HD pack in nsmbw_data\texture_replacements\ with
# texture_replacements = true; and copies of the current save and controls.
#
# Why an explicit required list: inside this checkout, the runtime falls back to
# runtime\assets\ for wii_bootstrap and dsp_coef.bin, so a missing file never shows up here. The
# first run on another PC (2026-09-27) failed on both, one launch at a time. Test a package from
# a folder OUTSIDE the repo, or that same fallback hides the gap again.
#
# The public package is built from build_release\ (configured with -DNSMBW_RELEASE=ON: first-launch
# disc picker, nsmbw_disc_setup.cpp) and ships DolphinTool.exe from dolphin_tool\ (gitignored; copy
# it out of an official Dolphin x64 release together with Dolphin's license) to extract the disc.
param(
    [string]$Version = "0.1.0",
    [string]$OutDir = "",
    [switch]$Private,
    [string]$DiscDir = "",        # -Private: extracted disc (sys\ + files\); default NSMBW-Files
    [string]$TexturePackDir = ""  # -Private: folder holding the pack's SMN\; default build_nsmbw\nsmbw_data\texture_replacements
)
$ErrorActionPreference = 'Stop'
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $tools "..\..\..")
$B = Join-Path $root $(if ($Private) { "build_nsmbw" } else { "build_release" })
$dolphin = Join-Path $root "dolphin_tool"
if (-not $Private -and -not (Test-Path (Join-Path $dolphin 'DolphinTool.exe'))) {
    throw "dolphin_tool\DolphinTool.exe is missing - the release needs it to extract the player's disc."
}
if ($OutDir -eq "") { $OutDir = Join-Path $root "dist" }

# Everything the exe needs next to it. The DLLs are the ones it imports that Windows doesn't
# ship (checked with llvm-objdump -p); any other *.dll in build_nsmbw is copied too.
$required = @(
    'NSMBWCompiled.exe',
    'SDL3.dll', 'webgpu_dawn.dll', 'dxcompiler.dll', 'dxil.dll',
    'libc++.dll', 'libunwind.dll', 'libpng16.dll', 'libz.dll',
    'dsp_coef.bin',                             # AX mixer resampling table (ax_mix.cpp)
    'initial_pipeline_cache.db',                # seed shader recipes (merge_pipeline_cache.py)
    'wii_bootstrap\shared2\wc24\nwc24msg.cfg'   # first-run NAND seed (nand_path.h)
)
$missing = $required | Where-Object { -not (Test-Path (Join-Path $B $_)) }
if ($missing) { throw "build_nsmbw is missing: $($missing -join ', ') - rebuild NSMBWCompiled first." }

$pkg = Join-Path $OutDir "NSMBWCompiled"
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg -Confirm:$false }
New-Item -ItemType Directory -Force $pkg | Out-Null
Copy-Item (Join-Path $B 'NSMBWCompiled.exe'), (Join-Path $B 'dsp_coef.bin'), (Join-Path $B 'initial_pipeline_cache.db') $pkg
Get-ChildItem $B -Filter *.dll | Copy-Item -Destination $pkg
Copy-Item -Recurse (Join-Path $B 'wii_bootstrap') $pkg
Copy-Item (Join-Path $tools 'PLAYING.txt') $pkg

if ($Private) {
    if ($DiscDir -eq "") { $DiscDir = Join-Path $root "NSMBW-Files" }
    if ($TexturePackDir -eq "") { $TexturePackDir = Join-Path $B "nsmbw_data\texture_replacements" }
    if (-not (Test-Path (Join-Path $DiscDir "sys\fst.bin"))) { throw "$DiscDir is not an extracted disc (no sys\fst.bin)" }
    $priv = Join-Path $OutDir "NSMBWCompiled-private"
    if (Test-Path $priv) { Remove-Item -Recurse -Force $priv -Confirm:$false }
    Move-Item $pkg $priv
    Remove-Item (Join-Path $priv 'PLAYING.txt')
    Set-Content -Path (Join-Path $priv 'portable.txt') -Encoding ascii `
        -Value 'Portable install: settings and save live in UserData\ next to the exe.'
    Write-Output "copying the disc ..."
    New-Item -ItemType Directory (Join-Path $priv 'disc') | Out-Null
    Copy-Item -Recurse (Join-Path $DiscDir 'sys'), (Join-Path $DiscDir 'files') (Join-Path $priv 'disc')
    if (Test-Path (Join-Path $TexturePackDir 'SMN')) {
        Write-Output "copying the texture pack ..."
        $tex = Join-Path $priv 'nsmbw_data\texture_replacements'
        New-Item -ItemType Directory -Force $tex | Out-Null
        Copy-Item -Recurse (Join-Path $TexturePackDir 'SMN') $tex
    }
    $user = Join-Path $priv 'UserData'
    New-Item -ItemType Directory -Force $user | Out-Null
    # Single-quoted TOML strings take backslashes literally (PLAYING.txt explains why that matters).
    $config = "[video]`ntexture_replacements = true`n`n[paths]`ndvd_root = '../disc'`n"
    [System.IO.File]::WriteAllText((Join-Path $user 'Config.toml'), $config)
    $appData = Join-Path $env:LOCALAPPDATA 'WiiCompiled'
    $save = Join-Path $appData 'NAND\title\00010004\534d4e50'
    if (Test-Path $save) {
        $dst = Join-Path $user 'NAND\title\00010004'
        New-Item -ItemType Directory -Force $dst | Out-Null
        Copy-Item -Recurse $save $dst
    }
    $controls = Join-Path $appData 'nsmbw_controls.ini'
    if (Test-Path $controls) { Copy-Item $controls $user }
    $files = (Get-ChildItem -Recurse $priv -File).Count
    $mb = (Get-ChildItem -Recurse $priv -File | Measure-Object Length -Sum).Sum / 1MB
    Write-Output ("private folder: {0} files, {1:N0} MB -> {2}" -f $files, $mb, $priv)
    return
}

# Name the zip after the commit, marked -dirty when the exe was built from uncommitted changes,
# so a bug report can be matched to source.
Copy-Item (Join-Path $dolphin 'DolphinTool.exe') $pkg
New-Item -ItemType Directory (Join-Path $pkg 'licenses') | Out-Null
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $pkg 'licenses\NSMBWCompiled-LICENSE.txt')
Get-ChildItem $dolphin -Filter 'COPYING*' | ForEach-Object { Copy-Item $_.FullName (Join-Path $pkg "licenses\Dolphin-$($_.Name).txt") }
$commit = (git -C $root rev-parse --short HEAD).Trim()
if (git -C $root status --porcelain --untracked-files=no) { $commit += '-dirty' }
$zip = Join-Path $OutDir "NSMBWCompiled-v$Version-$commit.zip"
if (Test-Path $zip) { Remove-Item $zip -Confirm:$false }
Compress-Archive -Path $pkg -DestinationPath $zip
$files = (Get-ChildItem -Recurse $pkg -File).Count
Write-Output ("{0} files -> {1} ({2:N0} MB)" -f $files, $zip, ((Get-Item $zip).Length / 1MB))
