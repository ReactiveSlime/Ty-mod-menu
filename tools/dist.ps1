# Build TyMod and package it for sharing/uploading.
# Produces:
#   dist\<Version>\OpenAL32.dll  (+ README.txt)
#   dist\<Version>.zip
#
# Usage:
#   tools\dist.ps1                 # build + package
#   tools\dist.ps1 -SkipBuild      # package the existing build
#   tools\dist.ps1 -Version 1.1    # override the version label
param(
    [string]$Version = "1.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

$built  = Join-Path $root "build\OpenAL32.dll"
$dist   = Join-Path $root "dist"
$stage  = Join-Path $dist "TyMod-$Version"
$zip    = Join-Path $dist "TyMod-$Version.zip"

if (-not $SkipBuild) {
    Write-Host "Building..." -ForegroundColor Cyan
    & "$PSScriptRoot\build.ps1"
}

if (-not (Test-Path $built)) { throw "Proxy not built. Run tools\build.ps1 first." }

# Fresh staging folder.
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $built (Join-Path $stage "OpenAL32.dll") -Force
Copy-Item (Join-Path $PSScriptRoot "install.ps1") (Join-Path $stage "Install.ps1") -Force

$bat = @"
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
echo.
pause
"@
Set-Content (Join-Path $stage "Install.bat") -Value $bat -Encoding ASCII

$unbat = @"
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -Uninstall %*
echo.
pause
"@
Set-Content (Join-Path $stage "Uninstall.bat") -Value $unbat -Encoding ASCII

$readme = @"
TyMod $Version
============
An in-game trainer/mod menu for TY the Tasmanian Tiger (Steam PC).

Features
--------
  * No-Clip flight  - fly freely through the level with a chase camera that
                      keeps Ty centred on screen.
  * Infinite Health - Ty never takes damage.

Controls
--------
  INSERT          Toggle the TyMod menu
  (No-Clip)       W/A/S/D move, Space/Ctrl up/down, Shift = fast
                  Mouse looks around, E / Q turn the movement heading

Install
-------
  Easiest: double-click Install.bat. It auto-detects the game, backs up the
  original OpenAL32.dll, and installs the mod.

  Or from a terminal:
      powershell -ExecutionPolicy Bypass -File Install.ps1
      powershell -ExecutionPolicy Bypass -File Install.ps1 -Uninstall

  Or manually: copy OpenAL32.dll into your TY the Tasmanian Tiger folder
  (Steam: right-click game -> Manage -> Browse local files), overwriting
  the existing file.

  * The mod replaces the game's OpenAL32.dll with a small loader that also
    forwards audio to the game's own soft_oal.dll, so sound keeps working.
  * Install.ps1 saves the original as OpenAL32.dll.tymod-backup in the game
    folder, so you can uninstall at any time.

Launch the game and press INSERT for the menu.

Uninstall
---------
  Easiest: double-click Uninstall.bat.

  Or from a terminal:
      powershell -ExecutionPolicy Bypass -File Install.ps1 -Uninstall

  (You can also delete OpenAL32.dll and verify/repair the game files in Steam.)

Notes
-----
  - Requires the Steam version of the game (it ships soft_oal.dll).
  * Logs to tymod.log in the game folder if you need to troubleshoot.
"@
Set-Content -LiteralPath (Join-Path $stage "README.txt") -Value $readme -Encoding UTF8

# Zip it (overwrite any previous zip).
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

Write-Host ""
Write-Host "Packaged:" -ForegroundColor Green
Get-ChildItem $stage | ForEach-Object { Write-Host ("  " + $stage + "\" + $_.Name) }
Write-Host ("  " + $zip) -ForegroundColor Green
