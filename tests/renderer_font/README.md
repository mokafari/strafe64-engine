# Font loader regression test

This harness compiles the real `tr_font.c` with real FreeType. GPU uploads and
filesystem reads are stubbed; it needs a TrueType font but no GL context.
It checks repeated registration, reuse at capacity, resource release, invalid
font cleanup, custom TTF precedence, and legacy-only font pack compatibility.

Configure with CMake and your FreeType installation, for example on this Windows
checkout after `scripts/build.ps1` has prepared the isolated headers:

```powershell
cmake -S tests/renderer_font -B engine/build/font-regression -A x64 `
  -DFREETYPE_LIBRARY=C:/vcpkg/installed/x64-windows/lib/freetype.lib `
  -DFREETYPE_INCLUDE_DIR_ft2build="$PWD/engine/build/ft-isolated/include" `
  -DFREETYPE_INCLUDE_DIR_freetype2="$PWD/engine/build/ft-isolated/include"
cmake --build engine/build/font-regression --config Release
$env:PATH = "$PWD/engine/build/Release;" + $env:PATH
engine/build/font-regression/Release/font_regression.exe C:/path/to/font.ttf
```

If CMake is not on PATH, use the executable bundled with Visual Studio, as in
`scripts/build.ps1`. The test returns nonzero on failure and uses explicit checks
that remain enabled in Release builds. It tracks FreeType face lifetimes and
engine-owned buffers, rather than estimating leaks from process memory usage.
