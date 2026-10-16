# Assemble / refresh a clean, GitHub-ready TyMod repository (source only).
#
#   - copies the mod source (proxy + tools + docs), but NOT vendor/
#   - adds imgui + minhook as git submodules pinned to the exact commits that
#     are currently vendored under vendor\, so the build matches what was tested
#   - (re)initialises a git repo and commits. If the output folder is already a
#     repo, its history and remote are preserved, so publishing a new build is
#     just:  pwsh -File tools\source.ps1  &&  git -C <OutDir> push
#
# First publish:
#   cd <OutDir>
#   git remote add origin https://github.com/<you>/<repo>.git
#   git branch -M main
#   git push -u origin main
param(
    [string]$OutDir = "",
    [string]$ImguiUrl   = "https://github.com/ocornut/imgui.git",
    [string]$MinhookUrl = "https://github.com/TsudaKageyu/minhook.git"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $OutDir) { $OutDir = Join-Path $root "dist\TyMod-repo" }

$isRepo = Test-Path (Join-Path $OutDir ".git")
$remote = $null
if ($isRepo) { try { $remote = (git -C $OutDir remote get-url origin 2>$null) } catch {} }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Refresh only what we manage; leave .git and vendor\ (the submodule working
# copies) untouched so re-publishing keeps history/remotes and stays buildable.
foreach ($m in @("proxy", "tools", "README.md", "IDEAS.md", ".gitignore", ".gitmodules")) {
    $p = Join-Path $OutDir $m
    if (Test-Path $p) { Remove-Item $p -Recurse -Force }
}

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

# --- project docs (the personal publishing runbook stays local) ---
Get-ChildItem $root -File |
    Where-Object { $_.Extension -eq ".md" -and $_.Name -ne "PUBLISHING.md" } |
    Copy-Item -Destination $OutDir

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

# --- stage source, then declare the submodules ---
if (-not $isRepo) { git init -q $OutDir }
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

# Add the submodules as gitlinks. This must run AFTER add -A: an uninitialised
# submodule shows up as a deletion, which add -A would otherwise stage.
git -C $OutDir update-index --add --cacheinfo 160000,$imguiSha,vendor/imgui
git -C $OutDir update-index --add --cacheinfo 160000,$minhookSha,vendor/minhook

$email = git -C $OutDir config user.email
if ($email) {
    git -C $OutDir diff --cached --quiet
    if ($LASTEXITCODE -ne 0) {
        git -C $OutDir commit -q -m "TyMod: update source"
        Write-Host "Committed source changes." -ForegroundColor Green
    } else {
        Write-Host "No source changes to commit." -ForegroundColor Yellow
    }
} else {
    Write-Host "No git identity configured; staged only. Set user.name/user.email, then commit." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Repo: $OutDir"
git -C $OutDir submodule status
Write-Host ""
Write-Host "Publish:" -ForegroundColor Cyan
if ($remote) {
    Write-Host "  git -C `"$OutDir`" push"
} else {
    Write-Host "  git -C `"$OutDir`" remote add origin https://github.com/<you>/<repo>.git"
    Write-Host "  git -C `"$OutDir`" branch -M main"
    Write-Host "  git -C `"$OutDir`" push -u origin main"
}
