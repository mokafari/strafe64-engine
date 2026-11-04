# Windows display sizes

The graphics menu offers 1080p, 1440p, 4K, 5K, 8K and ultrawide presets alongside
display-reported modes and the currently running resolution. These presets are
also available if display-mode detection returns nothing. Fullscreen support
for a particular size depends on the display/driver; native is the safest match.

From PowerShell in the repository root:

```powershell
# Fullscreen uses the desktop's native resolution by default.
.\scripts\run.ps1 -Fullscreen -Map strafe64_marble -Bots 4
# Explicit size, windowed (add -Fullscreen if desired).
.\scripts\run.ps1 -Resolution 2560x1440 -Map strafe64_marble
.\scripts\run.ps1 -Resolution 3840x2160
.\scripts\run.ps1 -Resolution 3440x1440
# Explicit native size; can also be combined with -Fullscreen.
.\scripts\run.ps1 -NativeResolution
```

Without resolution/fullscreen options the launcher retains the saved size.
`-Resolution` and `-NativeResolution` are mutually exclusive. Custom dimensions
must be within 320x240 to 16384x16384; GPU and display limits still apply.
Native mode uses SDL's desktop dimensions, which can differ from Windows'
DPI-scaled logical size. Higher pixel counts increase GPU cost.

In-game console equivalents:

```text
seta r_mode -1
seta r_customwidth 2560
seta r_customheight 1440
seta r_customPixelAspect 1
vid_restart
```

Use `seta r_mode -2` followed by `vid_restart` to return to native desktop size.
The graphics menu preserves native mode when applying unrelated settings without
changing the selected resolution.

Validation on 2026-11-04: Windows build, 11 native cases and 49 Python checks passed.
Marble GL2 smoke runs at 2560x1440 and 3840x2160 exited cleanly and produced
screenshots with those exact pixel dimensions. Other presets are enumerated and
tested as settings, not individually benchmarked or visually certified.
