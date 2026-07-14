# Launch STRAFE 64 on the locally-built Windows engine with OpenArena's free
# assets. Build first with scripts\build.ps1.
#
#   scripts\run.ps1                windowed, main menu
#   scripts\run.ps1 -Fullscreen    fullscreen, main menu
#   scripts\run.ps1 -Bots 4        bot match: 4 bots on aggressor
#   scripts\run.ps1 -Bots 4 -Map q3dm6
#   scripts\run.ps1 -Dedicated     dedicated server (aggressor)
#   scripts\run.ps1 -Psx           STRAFE 64 PSX low-fi preset (combine with above)
#
# vm_game/vm_cgame/vm_ui 0 load the modded native DLLs from baseoa\ (requires
# sv_pure 0): qagame + cgame carry the movement/race mod, ui.dll is the STRAFE 64
# reskinned main menu. Override the asset location with -OA <path> or $env:OA.
param(
    [switch]$Fullscreen,
    [int]$Bots = 0,
    [string]$Map = 'aggressor',
    [switch]$Dedicated,
    [switch]$Psx,
    [string]$OA = $env:OA
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$rel  = Join-Path $root 'engine\build\Release'
$sg   = Join-Path $root 'tools\strafegen'
if (-not $OA) { $OA = Join-Path $root 'assets\openarena' }

if (-not (Test-Path (Join-Path $rel 'ioquake3.exe'))) { throw "engine not built - run scripts\build.ps1 first" }
if (-not (Test-Path (Join-Path $OA 'baseoa')))        { throw "OpenArena assets not found at $OA" }

# Deploy all three modded DLLs from the same build output. They share networked
# headers (config strings, playerState layout), so a stale cgame against a fresh
# qagame makes the client exit on load - always deploy them together.
$baseoa = Join-Path $OA 'baseoa'
if (Test-Path (Join-Path $rel 'baseq3\qagame.dll')) {
    foreach ($d in 'qagame','cgame','ui') { Copy-Item (Join-Path $rel "baseq3\$d.dll") (Join-Path $baseoa "$d.dll") -Force }
}
# stage the canonical rulesets so "/exec strafe64|arena|sword_arena|arena_vg" work in-game
foreach ($c in 'strafe64','arena','sword_arena','arena_vg','autoexec') {
    $src = Join-Path $sg "$c.cfg"; if (Test-Path $src) { Copy-Item $src (Join-Path $baseoa "$c.cfg") -Force }
}
# Deploy the shared strafe64 shader+texture pak (speed-trail glow, skybox, console
# art, textures/strafe64/*). Without it the strafe64/* shaders resolve to missing
# textures and the trails render black. Generated offline by strafegen (python +
# Pillow); best-effort regen, then reuse whatever's there if python is missing.
$sharedPk3 = Join-Path $sg 'generated\zzz_strafe64_shader.pk3'
$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($py) { Push-Location $sg; & $py 'strafegen.py' '--emit-shared' 'generated' 2>&1 | Out-Null; Pop-Location }
if (Test-Path $sharedPk3) { Copy-Item $sharedPk3 (Join-Path $baseoa 'zzz_strafe64_shader.pk3') -Force }
# Share Tech Mono HUD font pak (fonts/strafe64.ttf); needs a FreeType-enabled
# engine build to render, else the cgame uses its LED fallback font.
$fontPk3 = Join-Path $sg 'generated\zzz_strafe64_font.pk3'
if (Test-Path $fontPk3) { Copy-Item $fontPk3 (Join-Path $baseoa 'zzz_strafe64_font.pk3') -Force }
# Combat pak (katana model + sword sounds + strafe64/lattice|datamosh|sword_cut
# shaders, assembled from tools/strafegen/assets/sword) and any generated strafe64
# maps (built by `strafegen.py <seed> [--arena|--length N] --pk3`). Deploy whatever
# has been generated into tools/strafegen/generated.
$genDir = Join-Path $sg 'generated'
foreach ($extra in 'zzz_strafe64_combat.pk3','zzz_strafe64_hud.pk3') {
    $p = Join-Path $genDir $extra; if (Test-Path $p) { Copy-Item $p (Join-Path $baseoa $extra) -Force }
}
if (Test-Path $genDir) {
    Get-ChildItem $genDir -Filter 'strafe64*.pk3' -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $baseoa $_.Name) -Force }
}

$common = @('+set','com_basegame','baseoa','+set','fs_basepath',$OA,'+set','sv_pure','0','+set','vm_game','0')

if ($Dedicated) {
    $ded = @('+set','dedicated','1','+map',$Map)
    & (Join-Path $rel 'ioq3ded.exe') @common @ded
    return
}

$psxArgs = @()
if ($Psx) { Copy-Item (Join-Path $sg 'psx.cfg') (Join-Path $baseoa 'psx.cfg') -Force; $psxArgs = @('+exec','psx.cfg') }

$fs = if ($Fullscreen) { '1' } else { '0' }
$extra = @()
if ($Bots -gt 0) { $extra = @('+set','bot_minplayers',"$Bots",'+set','g_spSkill','3','+map',$Map) }

$client = @('+set','vm_cgame','0','+set','vm_ui','0','+set','r_fullscreen',$fs)
& (Join-Path $rel 'ioquake3.exe') @common @client @psxArgs @extra
