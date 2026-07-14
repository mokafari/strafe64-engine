# Build the STRAFE 64 engine + mod for Windows (MSVC x64, Release) into engine/build.
#
#   scripts\build.ps1            configure (if needed) + build
#   scripts\build.ps1 -Clean     wipe engine\build and rebuild from scratch
#
# Produces engine\build\Release\ : ioquake3.exe, ioq3ded.exe, the renderer DLLs
# (renderer_opengl1/2.dll), SDL2.dll, and the modded baseq3\{qagame,cgame,ui}.dll
# that run.ps1 deploys.
#
# Requires Visual Studio 2022 (the "Desktop development with C++" workload,
# which bundles CMake + Ninja). No separate CMake install is needed.
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $root 'engine'
$build  = Join-Path $engine 'build'

# locate the CMake bundled with VS 2022 (fall back to one on PATH)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cmake = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $c = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $c) { $cmake = $c }
    }
}
if (-not $cmake) { $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source }
if (-not $cmake) { throw "cmake not found - install VS 2022 'Desktop development with C++' or add cmake to PATH" }

if ($args -contains '-Clean' -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

# BUILD_GAME_QVMS=OFF   : STRAFE 64 runs native game DLLs (vm_* 0); the QVM libc
#                         subset lacks pow() used by the timebind curve.
# USE_CODEC_MOD/MP3=OFF : tracker/MP3 music need pkg-config + external libs that
#                         aren't available on Windows. Ogg Vorbis/Opus (internal)
#                         still cover the game's music.
$cfgArgs = @('-DBUILD_GAME_QVMS=OFF','-DUSE_CODEC_MOD=OFF','-DUSE_CODEC_MP3=OFF')

# FreeType gives the HUD its real Share Tech Mono atlas (else the cgame falls back
# to the 5x7 LED "matrix" font). We enable it from a vcpkg-installed freetype, but
# deliberately do NOT use vcpkg's global toolchain file: that toolchain puts
# vcpkg's include/ on the compiler's global search path, and vcpkg's include/ also
# holds libjpeg-turbo's jpeglib.h (v62). The engine compiles its internal jpeg-9f
# (v90) sources, so that stray header makes every .jpg fail at runtime with
# "Wrong JPEG library version: library is 90, caller expects 62". Instead we point
# CMake's FindFreetype straight at an isolated freetype-ONLY include dir + the
# vcpkg import lib, and hand-copy the runtime DLLs after the build. jpeg/zlib/png
# for the engine itself stay 100% internal.
#   one-time setup:  C:\vcpkg\vcpkg.exe install freetype:x64-windows
$ftLib = 'C:\vcpkg\installed\x64-windows\lib\freetype.lib'
$ftVcpkgInc = 'C:\vcpkg\installed\x64-windows\include'
$ftBin = 'C:\vcpkg\installed\x64-windows\bin'
$ftInc = 'C:\vcpkg\ft-isolated\include'
$freetypeOn = (Test-Path $ftLib) -and (Test-Path (Join-Path $ftVcpkgInc 'ft2build.h'))
if ($freetypeOn) {
    # (re)build the isolated include dir: freetype headers only, no jpeglib.h
    if (Test-Path 'C:\vcpkg\ft-isolated') { Remove-Item -Recurse -Force 'C:\vcpkg\ft-isolated' }
    New-Item -ItemType Directory -Force -Path $ftInc | Out-Null
    Copy-Item (Join-Path $ftVcpkgInc 'ft2build.h') $ftInc -Force
    Copy-Item (Join-Path $ftVcpkgInc 'freetype') $ftInc -Recurse -Force
    $cfgArgs += @('-DUSE_FREETYPE=ON',"-DFREETYPE_LIBRARY=$ftLib",
                  "-DFREETYPE_INCLUDE_DIR_ft2build=$ftInc","-DFREETYPE_INCLUDE_DIR_freetype2=$ftInc")
    Write-Host 'freetype: enabled (isolated vcpkg headers; engine jpeg stays internal)'
} else {
    $cfgArgs += '-DUSE_FREETYPE=OFF'
    Write-Host 'freetype: NOT found -> HUD uses the LED fallback font. Run: C:\vcpkg\vcpkg.exe install freetype:x64-windows'
}

& $cmake -S $engine -B $build -G "Visual Studio 17 2022" -A x64 @cfgArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# Without vcpkg's toolchain there's no applocal step, so stage freetype's runtime
# DLLs (and its transitive deps) next to the binaries by hand.
if ($freetypeOn) {
    $relOut = Join-Path $build 'Release'
    foreach ($dll in 'freetype.dll','libpng16.dll','z.dll','zlib1.dll','bz2.dll','brotlidec.dll','brotlicommon.dll') {
        $s = Join-Path $ftBin $dll; if (Test-Path $s) { Copy-Item $s $relOut -Force }
    }
    Write-Host "freetype: staged runtime DLLs -> $relOut"
}

Write-Host "built -> $build\Release"
