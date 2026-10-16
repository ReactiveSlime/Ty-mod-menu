# Assemble a clean, GitHub-ready TyMod repository.
#
#   - copies the mod source (proxy + tools + docs), but NOT vendor/
#   - adds imgui + minhook as git submodules pinned to the exact commits that
#     are currently vendored under vendor\, so the build matches what was tested
#   - initialises a git repo and makes an initial commit (if a git identity is set)
#
# Then publish it:
#   cd <OutDir>
#   git remote add origin https://github.com/<you>/<repo>.git
#   git branch -M main
#   git push -u origin main
#
# (Anyone cloning needs  --recursive  or  git submodule update --init --recursive.)
param(
    [string]$OutDir = "",
    [string]$ImguiUrl   = "https://github.com/ocornut/imgui.git",
    [string]$MinhookUrl = "https://github.com/TsudaKageyu/minhook.git"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $OutDir) { $OutDir = Join-Path $root "dist\TyMod-repo" }

if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# --- proxy source only (.cpp/.h), no build artifacts ---
$dst = Join-Path $OutDir "proxy"
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Get-ChildItem (Join-Path $root "proxy") -File |
    Where-Object { $_.Extension -in ".cpp", ".h" } | Copy-Item -Destination $dst

# --- tools (scripts + data files), minus the local release-packaging script ---
$dst = Join-Path $OutDir "tools"
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Get-ChildItem (Join-Path $root "tools") |
    Where-Object { $_.Name -ne "dist.ps1" } |
    Copy-Item -Destination $dst -Recurse -Force

# --- project docs ---
Get-ChildItem $root -File | Where-Object { $_.Extension -in ".md" } | Copy-Item -Destination $OutDir

# --- .gitignore ---
$gitignore = @"
# Build output
build/
dist/
proxy/*.obj
proxy/*.exp
proxy/*.lib
proxy/exports.def
*.obj
*.exp
*.lib
*.pdb
*.dll

# Runtime
tymod.log

# Local dev artifacts
backup/
snapshots/
"@
Set-Content (Join-Path $OutDir ".gitignore") -Value $gitignore -Encoding ASCII

# --- pin submodule commits to the currently vendored clones (fallback constant) ---
function Get-VendorSha($path, $fallback) {
    if (Test-Path $path) {
        try { $h = (git -C $path rev-parse HEAD 2>$null); if ($h) { return $h.Trim() } } catch {}
    }
    return $fallback
}
$imguiSha   = Get-VendorSha (Join-Path $root "vendor\imgui")   "098c71133c32b7b946ae2b6db27fc8a4d0629d68"
$minhookSha = Get-VendorSha (Join-Path $root "vendor\minhook") "8af6b4acae5a9388fd742b56fa79ece89d96f823"

# --- init repo, stage source, declare submodules ---
git init -q $OutDir
git -C $OutDir add -A

$gitmodules = @"
[submodule "vendor/imgui"]
`tpath = vendor/imgui
`turl = $ImguiUrl
[submodule "vendor/minhook"]
`tpath = vendor/minhook
`turl = $MinhookUrl
"@
Set-Content (Join-Path $OutDir ".gitmodules") -Value $gitmodules -Encoding ASCII
git -C $OutDir add .gitmodules

# Add the two submodules as gitlinks (no clone needed; points at the pinned commits).
git -C $OutDir update-index --add --cacheinfo 160000,$imguiSha,vendor/imgui
git -C $OutDir update-index --add --cacheinfo 160000,$minhookSha,vendor/minhook

$email = git -C $OutDir config user.email
if ($email) {
    git -C $OutDir commit -q -m "TyMod: initial commit"
    Write-Host "Committed. Source tree ready to push:" -ForegroundColor Green
} else {
    Write-Host "Staged everything (no git identity configured, so not committed)." -ForegroundColor Yellow
    Write-Host "Set one, then commit:"
    Write-Host "  git -C `"$OutDir`" config user.name `"Your Name`""
    Write-Host "  git -C `"$OutDir`" config user.email `"you@example.com`""
    Write-Host "  git -C `"$OutDir`" commit -m `"TyMod: initial commit`""
}

Write-Host ""
Write-Host "Repo: $OutDir"
git -C $OutDir submodule status
Write-Host ""
Write-Host "Publish:" -ForegroundColor Cyan
Write-Host "  cd `"$OutDir`""
Write-Host "  git remote add origin https://github.com/<you>/<repo>.git"
Write-Host "  git branch -M main"
Write-Host "  git push -u origin main"
