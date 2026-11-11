# Deterministic native gameplay and Python asset/API regressions.
# -Smoke additionally builds/stages the game and runs isolated live GL checks.
param([switch]$Smoke)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $cmake = $null
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) { $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
    }
    if (-not $cmake -or -not (Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $cmake -S tests -B engine/build/regressions -A x64
    if ($LASTEXITCODE) { throw 'Regression configure failed' }
    & $cmake --build engine/build/regressions --config Release
    if ($LASTEXITCODE) { throw 'Regression build failed' }
    & $ctest --test-dir engine/build/regressions -C Release --output-on-failure --output-junit results.xml
    if ($LASTEXITCODE) { throw 'Native regression tests failed' }
    foreach ($test in 'test_asset_packaging.py', 'test_spawn_decoration.py', 'test_smoke_validation.py', 'test_nocturne_flow.py') {
        & py -3.13 -m unittest discover -s tools/strafegen -p $test
        if ($LASTEXITCODE) { throw "Python regression failed: $test" }
    }
    & py -3.13 tools/strafegen/test_engine_api.py
    if ($LASTEXITCODE) { throw 'Engine API tests failed' }
    if ($Smoke) {
        & "$PSScriptRoot/build.ps1"
        & "$PSScriptRoot/run.ps1" -StageOnly
        & py -3.13 tools/strafegen/windows_smoke.py --tag regression-menu --menu
        if ($LASTEXITCODE) { throw 'Menu smoke failed' }
        & py -3.13 tools/strafegen/windows_smoke.py --tag regression-nocturne --map strafe64_nocturne --duration 20 --set r_water=1 --set r_shafts=0.35 --set r_grade=1 --set r_postProcess=1 --set r_hdr=1
        if ($LASTEXITCODE) { throw 'Nocturne GL2 smoke failed' }
        & py -3.13 tools/strafegen/windows_smoke.py --tag regression-fallback --renderer opengl1 --map strafe64_nocturne --duration 10
        if ($LASTEXITCODE) { throw 'GL1 fallback smoke failed' }
    }
    Write-Host 'PASS: STRAFE64 regression suite'
} finally {
    Pop-Location
}
