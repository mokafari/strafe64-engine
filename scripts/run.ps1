# Launch STRAFE 64 on the locally-built Windows engine with OpenArena's free
# assets. Build first with scripts\build.ps1.
#
#   scripts\run.ps1                windowed, main menu
#   scripts\run.ps1 -Fullscreen    fullscreen, main menu
#   scripts\run.ps1 -Fullscreen -NativeResolution  match the current display
#   scripts\run.ps1 -Resolution 2560x1440          explicit window size
#   scripts\run.ps1 -Bots 4        bot match: 4 bots on aggressor
#   scripts\run.ps1 -Bots 4 -Map q3dm6
#   scripts\run.ps1 -Dedicated     dedicated server (aggressor)
#   scripts\run.ps1 -Psx           STRAFE 64 PSX low-fi preset (combine with above)
#   scripts\run.ps1 -StageOnly     rebuild/stage assets without starting the game
#
# vm_game/vm_cgame/vm_ui 0 load the modded native DLLs from baseoa\ (requires
# sv_pure 0): qagame + cgame carry the movement/race mod, ui.dll is the STRAFE 64
# reskinned main menu. Override the asset location with -OA <path> or $env:OA.
param(
    [switch]$Fullscreen,
    [ValidatePattern('^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$')]
    [string]$Resolution,
    [switch]$NativeResolution,
    [switch]$ExperimentalSlowmo,
    [int]$Bots = 0,
    [string]$Map = 'aggressor',
    [switch]$Dedicated,
    [switch]$Psx,
    [switch]$StageOnly,
    [string]$OA = $env:OA
)
$ErrorActionPreference = 'Stop'
if ($Resolution -and $NativeResolution) { throw 'Choose either -Resolution or -NativeResolution.' }
$videoArgs = @()
if ($Resolution) {
    $width, $height = $Resolution.Split('x')
    if ([int]$width -lt 320 -or [int]$height -lt 240 -or [int]$width -gt 16384 -or [int]$height -gt 16384) {
        throw 'Resolution must be between 320x240 and 16384x16384.'
    }
    $videoArgs = @('+set','r_mode','-1','+set','r_customwidth',$width,'+set','r_customheight',$height,'+set','r_customPixelAspect','1')
} elseif ($NativeResolution -or $Fullscreen) {
    $videoArgs = @('+set','r_mode','-2')
}
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
# art, textures/strafe64/*). Build photo assets first: emitting the shared pak
# alone silently omits skies and materials when skytex/ has not been built.
$sharedPk3 = Join-Path $sg 'generated\zzz_strafe64_shader.pk3'
$py = $null
$pyArgs = @()
foreach ($candidate in 'python', 'py', 'python3') {
    $command = Get-Command $candidate -ErrorAction SilentlyContinue
    if (-not $command) { continue }
    $probeArgs = @()
    if ($candidate -eq 'py') { $probeArgs = @('-3') }
    try {
        & $command.Source @probeArgs -c 'import sys, PIL, numpy; assert sys.version_info >= (3, 10)' 2>$null
    } catch { continue }
    if ($LASTEXITCODE -eq 0) { $py = $command.Source; $pyArgs = $probeArgs; break }
}
if (-not $py) { throw 'Asset generation requires Python 3.10+, Pillow and NumPy. Install with: py -3 -m pip install pillow numpy' }
Push-Location $sg
try {
    & $py @pyArgs 'build_assets.py' '--if-needed'
    if ($LASTEXITCODE -ne 0) { throw 'Photo asset build failed; assets were not deployed.' }
    & $py @pyArgs 'strafegen.py' '--emit-shared' 'generated'
    if ($LASTEXITCODE -ne 0) { throw 'Shared texture pack build failed; assets were not deployed.' }
} finally { Pop-Location }
Copy-Item $sharedPk3 (Join-Path $baseoa 'zzz_strafe64_shader.pk3') -Force
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
    # Lattice/surf/daily maps have other prefixes but are still playable packs.
    Get-ChildItem $genDir -Filter '*.pk3' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike 'zzz_*' } |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $baseoa $_.Name) -Force }
}

$common = @('+set','com_basegame','baseoa','+set','fs_basepath',$OA,'+set','sv_pure','0','+set','vm_game','0')

$common += @('+set','com_slowmo',$(if ($ExperimentalSlowmo) {'1'} else {'0'}))
if ($ExperimentalSlowmo -and $Dedicated) { throw 'Experimental slow motion requires local play.' }

if ($StageOnly) { Write-Host "Staged Windows assets -> $baseoa"; return }

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
elseif ($PSBoundParameters.ContainsKey('Map')) { $extra = @('+map', $Map) }

$client = @('+set','vm_cgame','0','+set','vm_ui','0','+set','r_fullscreen',$fs)
& (Join-Path $rel 'ioquake3.exe') @common @client @psxArgs @videoArgs @extra
