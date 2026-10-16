# Install TyMod into TY the Tasmanian Tiger.
# Backs up the original OpenAL32.dll and replaces it with the TyMod proxy
# (which forwards audio to the game's own soft_oal.dll).
#
# Usage:
#   Install.ps1                 auto-detect the game (Steam)
#   Install.ps1 -GameDir "D:\...\TY the Tasmanian Tiger"
#   Install.ps1 -Uninstall      restore the original OpenAL32.dll
param(
    [string]$GameDir,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$srcDir = $PSScriptRoot

# The proxy sits next to this script (shareable package) or in ..\build (dev tree).
$built = Join-Path $srcDir "OpenAL32.dll"
if (-not (Test-Path $built)) { $built = Join-Path (Split-Path $srcDir -Parent) "build\OpenAL32.dll" }

function Find-TyGameDir {
    $roots = @()
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) {
        $steam = (Get-ItemProperty "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam" -ErrorAction SilentlyContinue).InstallPath
    }
    if ($steam) {
        $roots += $steam
        $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            foreach ($m in (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' -AllMatches).Matches) {
                $roots += $m.Groups[1].Value
            }
        }
    }
    foreach ($r in $roots) {
        $c = Join-Path $r "steamapps\common\TY the Tasmanian Tiger"
        if (Test-Path (Join-Path $c "TY.exe")) { return $c }
    }
    return $null
}

if (-not $GameDir) { $GameDir = Find-TyGameDir }
if (-not $GameDir) { throw "Could not find the game. Pass -GameDir 'D:\...\TY the Tasmanian Tiger'." }

$target = Join-Path $GameDir "OpenAL32.dll"
$bak    = Join-Path $GameDir "OpenAL32.dll.tymod-backup"

if ($Uninstall) {
    if (-not (Test-Path $bak)) { throw "No backup found at $bak. Verifying game files in Steam will also restore it." }
    Copy-Item $bak $target -Force
    Remove-Item $bak -Force
    Write-Host "TyMod uninstalled. Original OpenAL32.dll restored." -ForegroundColor Green
    return
}

if (-not (Test-Path (Join-Path $GameDir "TY.exe"))) { throw "TY.exe not found in $GameDir" }
if (-not (Test-Path (Join-Path $GameDir "soft_oal.dll"))) {
    Write-Host "Warning: soft_oal.dll is missing from the game folder; audio may not work." -ForegroundColor Yellow
}
if (-not (Test-Path $built)) { throw "Mod DLL not found. Place Install.ps1 next to OpenAL32.dll (or build it first)." }

# Back up the original once, then replace it with the proxy.
$builtHash = (Get-FileHash $built -Algorithm SHA256).Hash
$curHash   = if (Test-Path $target) { (Get-FileHash $target -Algorithm SHA256).Hash } else { "" }

if ($curHash -eq $builtHash) {
    Write-Host "TyMod is already installed." -ForegroundColor Yellow
} else {
    if (-not (Test-Path $bak)) {
        Copy-Item $target $bak -Force
        Write-Host "Backed up original OpenAL32.dll -> OpenAL32.dll.tymod-backup"
    }
    Copy-Item $built $target -Force
    Write-Host "Installed TyMod -> $target" -ForegroundColor Green
}

Write-Host ""
Write-Host "Launch the game and press INSERT for the TyMod menu." -ForegroundColor Cyan
Write-Host "To uninstall later: Install.ps1 -Uninstall"
