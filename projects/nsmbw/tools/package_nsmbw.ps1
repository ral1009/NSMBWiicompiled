# Assembles a folder someone else can run: build_nsmbw\NSMBWCompiled.exe plus every file it
# loads from its own directory, and nothing else (no logs, shots, profiles, or nsmbw_data -
# that folder holds this machine's shader cache and controller bindings and is recreated on
# first run). Writes dist\NSMBWCompiled\ and dist\NSMBWCompiled-<commit>.zip.
#
#   .\projects\nsmbw\tools\package_nsmbw.ps1
#
# Why an explicit required list: inside this checkout, the runtime falls back to
# runtime\assets\ for wii_bootstrap and dsp_coef.bin, so a missing file never shows up here. The
# first run on another PC (2026-09-27) failed on both, one launch at a time. Test a package from
# a folder OUTSIDE the repo, or that same fallback hides the gap again.
param(
    [string]$OutDir = ""
)
$ErrorActionPreference = 'Stop'
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $tools "..\..\..")
$B = Join-Path $root "build_nsmbw"
if ($OutDir -eq "") { $OutDir = Join-Path $root "dist" }

# Everything the exe needs next to it. The DLLs are the ones it imports that Windows doesn't
# ship (checked with llvm-objdump -p); any other *.dll in build_nsmbw is copied too.
$required = @(
    'NSMBWCompiled.exe',
    'SDL3.dll', 'webgpu_dawn.dll', 'dxcompiler.dll', 'dxil.dll',
    'libc++.dll', 'libunwind.dll', 'libpng16.dll', 'libz.dll',
    'dsp_coef.bin',                             # AX mixer resampling table (ax_mix.cpp)
    'wii_bootstrap\shared2\wc24\nwc24msg.cfg'   # first-run NAND seed (nand_path.h)
)
$missing = $required | Where-Object { -not (Test-Path (Join-Path $B $_)) }
if ($missing) { throw "build_nsmbw is missing: $($missing -join ', ') - rebuild NSMBWCompiled first." }

$pkg = Join-Path $OutDir "NSMBWCompiled"
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg -Confirm:$false }
New-Item -ItemType Directory -Force $pkg | Out-Null
Copy-Item (Join-Path $B 'NSMBWCompiled.exe'), (Join-Path $B 'dsp_coef.bin') $pkg
Get-ChildItem $B -Filter *.dll | Copy-Item -Destination $pkg
Copy-Item -Recurse (Join-Path $B 'wii_bootstrap') $pkg
Copy-Item (Join-Path $tools 'PLAYING.txt') $pkg

# Name the zip after the commit, marked -dirty when the exe was built from uncommitted changes,
# so a bug report can be matched to source.
$commit = (git -C $root rev-parse --short HEAD).Trim()
if (git -C $root status --porcelain --untracked-files=no) { $commit += '-dirty' }
$zip = Join-Path $OutDir "NSMBWCompiled-$commit.zip"
if (Test-Path $zip) { Remove-Item $zip -Confirm:$false }
Compress-Archive -Path $pkg -DestinationPath $zip
$files = (Get-ChildItem -Recurse $pkg -File).Count
Write-Output ("{0} files -> {1} ({2:N0} MB)" -f $files, $zip, ((Get-Item $zip).Length / 1MB))
