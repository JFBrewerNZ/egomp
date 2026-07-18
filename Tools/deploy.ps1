<#
.SYNOPSIS
  Build EgoMP.dll (Release|Win32) and deploy it next to the game.

.DESCRIPTION
  One command for the whole cycle that was previously manual:
    1. Close any running Fable/EgoMP clients (they lock EgoMP.dll).
    2. msbuild Core\Core.vcxproj Release|Win32.
    3. Copy the built DLL to egomp\EgoMP.dll and egomp\Release\EgoMP.dll
       (msbuild outputs to Core\Release\; both deploy locations get updated).
  msbuild's own post-build step tries to copy to a stale D:\ path and always
  "fails" harmlessly -- this script ignores that and does the real copy.

.PARAMETER Launch
  After a successful deploy, launch N clients (default 0). Launched by
  double-click equivalent (foreground) so the DirectInput mouse grab succeeds.

.PARAMETER KeepClients
  Do not close running clients first. Build will fail if the DLL is locked;
  use only when you know nothing is holding it.

.EXAMPLE
  .\Tools\deploy.ps1
  .\Tools\deploy.ps1 -Launch 2
#>
param(
    [int]$Launch = 0,
    [switch]$KeepClients
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot          # ...\egomp
$proj = Join-Path $repo "Core\Core.vcxproj"
$built = Join-Path $repo "Core\Release\EgoMP.dll"
$deploy1 = Join-Path $repo "EgoMP.dll"
$deploy2 = Join-Path $repo "Release\EgoMP.dll"
$exe = Join-Path $repo "EgoMP.exe"

function Wait-FileUnlocked {
    param([string]$Path, [int]$TimeoutSec = 25)
    if (-not (Test-Path $Path)) { return }   # nothing to unlock yet
    for ($i = 0; $i -lt $TimeoutSec; $i++) {
        try {
            $s = [System.IO.File]::Open($Path, 'Open', 'ReadWrite', 'None')
            $s.Close(); return
        } catch {
            Start-Sleep -Seconds 1
        }
    }
    throw "Timed out waiting for $Path to be released (a client may be hung; " +
          "close it manually)."
}

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -requires Microsoft.Component.MSBuild `
                        -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($p) { return $p }
    }
    $fallback = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $fallback) { return $fallback }
    throw "MSBuild.exe not found (install VS 2022 or the Build Tools)."
}

# 1. Close clients so the DLL isn't locked.
if (-not $KeepClients) {
    $running = Get-Process Fable, EgoMP, EgoMPServer -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "Closing $($running.Count) running client/server process(es)..." -ForegroundColor Yellow
        $running | Stop-Process -Force
    }
    # A hung client can take several seconds to release its handle on the DLL,
    # so poll for the file to actually unlock rather than guessing a sleep.
    Wait-FileUnlocked $deploy1
}

# 2. Build.
$msbuild = Find-MSBuild
Write-Host "Building Core (Release|Win32)..." -ForegroundColor Cyan
& $msbuild $proj /p:Configuration=Release /p:Platform=Win32 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
if (-not (Test-Path $built)) { throw "Build reported success but $built is missing." }

# 3. Deploy.
Copy-Item $built $deploy1 -Force
if (Test-Path (Split-Path $deploy2)) { Copy-Item $built $deploy2 -Force }
$size = (Get-Item $deploy1).Length
Write-Host "Deployed EgoMP.dll ($size bytes) -> $deploy1" -ForegroundColor Green
if (Test-Path $deploy2) { Write-Host "                       -> $deploy2" -ForegroundColor Green }

# Optional launch.
if ($Launch -gt 0) {
    if (-not (Test-Path $exe)) { throw "Launcher not found: $exe" }
    for ($i = 1; $i -le $Launch; $i++) {
        Write-Host "Launching client $i/$Launch..." -ForegroundColor Cyan
        Start-Process -FilePath $exe -WorkingDirectory $repo
        Start-Sleep -Seconds 7   # let its DirectInput init finish before the next
    }
}

Write-Host "Done." -ForegroundColor Green
