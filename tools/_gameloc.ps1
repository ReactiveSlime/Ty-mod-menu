# Locate "TY the Tasmanian Tiger" across all Steam libraries.
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
                $roots += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    foreach ($r in $roots) {
        if (-not $r) { continue }
        $c = Join-Path $r "steamapps\common\TY the Tasmanian Tiger"
        if (Test-Path (Join-Path $c "TY.exe")) { return $c }
    }
    return $null
}
