# Uninstall TyMod: restore the original OpenAL32.dll.
param([string]$GameDir)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\_gameloc.ps1"

if (-not $GameDir) { $GameDir = Find-TyGameDir }
if (-not $GameDir) { throw "Could not find the TY install. Pass -GameDir 'D:\...\TY the Tasmanian Tiger'." }

$old = Join-Path $GameDir "OpenAL32.dll"
$bak = Join-Path $root "backup\OpenAL32.dll.orig"

if (-not (Test-Path $bak)) { throw "No backup found at $bak - cannot restore." }

Copy-Item $bak $old -Force
Remove-Item (Join-Path $GameDir "tymod.log") -ErrorAction SilentlyContinue
Write-Host "Restored original OpenAL32.dll" -ForegroundColor Green
