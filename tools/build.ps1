# Build the TyMod OpenAL32.dll proxy (x86). Run with PowerShell 7.
$ErrorActionPreference = "Stop"

$root  = Split-Path $PSScriptRoot -Parent
$proxy = Join-Path $root "proxy"
$build = Join-Path $root "build"
$exportsTxt = Join-Path $PSScriptRoot "soft_oal_exports.txt"

$vs = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars32.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars32.bat not found: $vcvars" }

# Import the MSVC x86 environment into this session (needed for cl/lib on PATH).
$envLines = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envLines) {
    $i = $line.IndexOf('=')
    if ($i -gt 0) {
        Set-Item -Path ("env:" + $line.Substring(0, $i)) -Value $line.Substring($i + 1) -ErrorAction SilentlyContinue
    }
}

New-Item -ItemType Directory -Force -Path $build | Out-Null

$names = Select-String -LiteralPath $exportsTxt -Pattern "^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)" |
    ForEach-Object { $_.Matches[0].Groups[1].Value } | Where-Object { $_ -and $_ -ne "NONAME" }
if (-not $names) { throw "Failed to parse exports from $exportsTxt" }
Write-Host "Parsed $($names.Count) OpenAL exports."

# soft_oal.def -> soft_oal.lib, so the linker accepts our forwarders.
$plainDef = "LIBRARY soft_oal`r`nEXPORTS`r`n" + (($names | ForEach-Object { "    $_" }) -join "`r`n") + "`r`n"
Set-Content -LiteralPath (Join-Path $build "soft_oal.def") -Value $plainDef -Encoding ascii
& lib /nologo /def:"$build\soft_oal.def" /machine:x86 /out:"$build\soft_oal.lib"
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed ($LASTEXITCODE)" }

# exports.def forwards every OpenAL32 export to soft_oal.<name>.
$fwdDef = "LIBRARY OpenAL32`r`nEXPORTS`r`n" + (($names | ForEach-Object { "    $_=soft_oal.$_" }) -join "`r`n") + "`r`n"
Set-Content -LiteralPath (Join-Path $proxy "exports.def") -Value $fwdDef -Encoding ascii

$minhook = Join-Path $root "vendor\minhook"
$imgui   = Join-Path $root "vendor\imgui"
$sources = @(
    "dllmain.cpp", "log.cpp", "mod.cpp", "overlay.cpp", "noclip.cpp", "health.cpp", "camera.cpp",
    "$minhook\src\buffer.c", "$minhook\src\hook.c", "$minhook\src\trampoline.c", "$minhook\src\hde\hde32.c",
    "$imgui\imgui.cpp", "$imgui\imgui_draw.cpp", "$imgui\imgui_tables.cpp", "$imgui\imgui_widgets.cpp",
    "$imgui\backends\imgui_impl_win32.cpp", "$imgui\backends\imgui_impl_opengl3.cpp"
)
$inc = @(
    "/I$minhook\include", "/I$minhook\src", "/I$minhook\src\hde",
    "/I$imgui", "/I$imgui\backends"
)

Push-Location $proxy
try {
    & cl /nologo /LD /O2 /MT /EHsc /W3 @inc $sources `
        /link /DEF:exports.def "$build\soft_oal.lib" /OUT:"$build\OpenAL32.dll"
    if ($LASTEXITCODE -ne 0) { throw "cl failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

Write-Host "Built: $build\OpenAL32.dll" -ForegroundColor Green
