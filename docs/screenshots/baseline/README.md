# Nocturne colour baseline (2026-11-02)

Starting point for the "hyper-real ethereal" grade work. Map `strafe64_nocturne`, 1280x720, first-person, no HUD, no weapon.
Raw numbers: `baseline.json`. Re-sample after any change with `python3 -I sample.py` (same regions, same shots).

## Pinned state

| Group | Setting |
|---|---|
| Grade (shipped defaults) | `r_grade 1`, `r_gradeContrast 1.06`, `r_gradeSaturation 1.08`, `r_gradeTemp 0.04`, `r_vignette 0.18`, `r_filmGrain 0.03`, `r_fxaa 1` |
| Time grade / dynamic | `cg_timeGrade 0`, `r_gradeLiveSat 1`, `r_gradeLiveTemp 0`, `g_timeBind 0`, `timescale 1`, `cg_killcam 0` |
| Other post | `r_bodycam 0`, `r_dofAmount 0` (r_dof stays on, but no blur), `r_dofAutoFocus 0` |
| Left at shipped defaults | `r_hdr 1`, `r_toneMap 1`, `r_autoExposure 1`, `r_bloom 0.25`, `r_sunShadows 1`, `r_ext_multisample 4` |
| Off (shipped default) | `r_ssao 0`, `r_pbr 0`, `r_cubeMapping 0`, `r_drawSunRays 0` |
| View | `cg_fov 105`, `cg_draw2D 0`, `cg_drawGun 0`, noclip camera at eye height 66 |

Auto-exposure is left on because it is what the player sees. It adapts, so wait about 7 s after each teleport before shooting.
With auto-exposure forced off the floor clips to flat cream, so do not use that as a baseline.

## Shots

| File | View |
|---|---|
| `nocturne-1-portal.jpg` | spawn corner looking at the portal, moon on the left |
| `nocturne-2-axis.jpg` | south end looking down the long axis through the portals |
| `nocturne-3-terrace.jpg` | plaza centre looking at the east terrace and stairs |
| `nocturne-4-colonnade.jpg` | corner colonnade, cloud sea and city on the horizon |
| `nocturne-5-overview.jpg` | high overview from the intermission camera |

## Region colours (mean RGB, shot-dependent)

| Element | Hex | Luma | Notes |
|---|---|---|---|
| Night sky | `#0c1834` ... `#11224a` | 0.08-0.13 | hue ~222 deg, saturation 0.76-0.93 |
| Lit marble floor | `#afd4eb` (shots 1, 5) ... `#fffff6` (shot 2) | 0.81-1.00 | hue ~203 deg, up to 100% clipped |
| Floor in shadow | `#5171a7`, `#6987b0` | 0.43-0.52 | hue ~215 deg, saturation 0.4-0.5 |
| Black marble | `#131a2a`, `#131825` | ~0.10 | blue-black, not neutral |
| White marble wall / pillar | `#4e6aab` (shade), `#b5c5ea` (lit) | 0.41-0.77 | blue cast on shade side |
| Moon | `#888382` ... `#d8d4d5` | 0.52-0.84 | the only neutral/warm highlight |
| Horizon haze | `#76829f` | 0.51 | |

Whole frame: mean RGB about (90, 108, 128), mean luma 0.37-0.44, mean saturation 0.46-0.55.

## What the baseline shows

1. **The floor clips.** In 3 of 5 shots 11-15% of pixels have a channel at 250+. In the axis shot the lit floor is 100% clipped (`#fffff6`), so the marble veining and tile seams vanish. Shots 1 and 5 are the exception, because sky fills the frame and the exposure drops.
2. **The look is blue.** Floor, shade, haze and sky all sit at hue 200-225 deg. Only the moon, the warm pillar lights and the cyan neon break it.
3. **Highlights are a hard cut.** The p95 luma is 0.88-1.0, with no shoulder. The floor goes from 0.8 to white in a few stops.
4. **Blacks are fine.** Only 2-11% of pixels are crushed (luma below 0.03), and the black marble keeps a blue tint at luma 0.10.
5. **No depth cues beyond the sky.** There is no god-ray, halation or ambient occlusion. Shadows are crisp and flat-coloured.

## Round 1: cvars only (`cvarsA/`)

Set: `r_ssao 1`, `r_drawSunRays 1`, `r_bloom 0.4`, `r_gradeContrast 1.10`, `r_gradeSaturation 0.92`, `r_gradeTemp 0.02`, `r_vignette 0.28`, `r_filmGrain 0.02` (all latched ones applied with `vid_restart`).
Everything else as in the baseline. Re-sample with `python3 -I sample.py cvarsA`; numbers in `cvarsA/baseline.json`.

**`r_pbr 1` + `r_cubeMapping 1` made it worse and were dropped.** The floor goes flat white in the first view and the moon blows out (`cvarsA/FAIL-pbr-cubemap-floor.jpg`). Do not enable them without fixing the material and light values first.

| Shot | Mean luma | Clipped % | Crushed % | Lit floor |
|---|---|---|---|---|
| 1 portal | 0.413 -> 0.409 | 4.2 -> 5.6 | 7.1 -> 14.1 | `#b0d6ec` -> `#b4dce7` |
| 2 axis | 0.415 -> 0.398 | 14.9 -> 9.9 | 10.8 -> 12.9 | `#fffff6` -> `#fffffe` (still 100% clipped) |
| 3 terrace | 0.427 -> 0.407 | 10.8 -> 8.0 | 2.2 -> 6.3 | `#e3f5e9` -> `#e8eee9` |
| 4 colonnade | 0.372 -> 0.351 | 11.9 -> 6.2 | 4.6 -> 9.4 | `#faf2e5` -> `#edeae4` |
| 5 overview | 0.440 -> 0.435 | 8.1 -> 8.9 | 4.6 -> 6.6 | `#afd4eb` -> `#b2dae6` |

Read-out:
- Sky, black marble and pillar shade got darker (sky luma 0.09 -> 0.07), and saturation dropped a little. That is the vignette and contrast change doing what it should.
- The lit floor did not move: 0.92-1.0 luma in the three shots where it fills the frame. Clipping fell by only 2-6 points in shots 2-4, and blacks crush more (up to 14%).
- Sun-rays and SSAO are barely visible at this camera height, and bloom 0.4 adds only a faint glow.
- Conclusion: the grade cvars can't recover the floor. The marble is lit too hot for the exposure, and the highlight cut-off is hard. The next step is a shoulder in the grade shader, or less light on the floor (map lights or exposure bias).

## Round 2: grade shader (`shaderB/`)

New in `colorgrade_fp.glsl` and `tr_init.c`. All four default to 0, which should leave the shipped look unchanged (not diffed against the old build):

| Cvar | Does |
|---|---|
| `r_gradeExposure` | exposure bias in stops, applied at the tonemap (before the clamp, so clipped highlights come back). Archived, not a cheat. |
| `r_gradeShoulder` | 0..1 highlight rolloff. Scales all channels by the brightest one's curve, so hue is kept. (The first per-channel version shifted hue to purple/cyan veins.) |
| `r_gradeSplit` | 0..1 split-toning: cool shadows, warm highlights. |
| `r_gradeLift` | 0..~0.1 black lift. |

Set used: round-1 grade with `r_gradeTemp 0.05`, `r_gradeExposure -0.8`, `r_gradeShoulder 0.5`, `r_gradeSplit 0.3`, `r_gradeLift 0.02`. SSAO and sun-rays on, PBR/cubemaps off. Numbers in `shaderB/baseline.json`.

| Shot | Mean luma | Clipped % | Crushed % | Lit floor |
|---|---|---|---|---|
| 1 portal | 0.413 -> 0.291 | 4.2 -> 0.1 | 7.1 -> 19.2 | `#b0d6ec` -> `#8299cf` |
| 2 axis | 0.415 -> 0.287 | 14.9 -> 1.9 | 10.8 -> 14.3 | `#fffff6` -> `#adc0cb` |
| 3 terrace | 0.427 -> 0.267 | 10.8 -> 0.0 | 2.2 -> 14.6 | `#e3f5e9` -> `#8ba5dd` |
| 4 colonnade | 0.372 -> 0.288 | 11.9 -> 1.1 | 4.6 -> 19.5 | `#faf2e5` -> `#a9c2db` |
| 5 overview | 0.440 -> 0.268 | 8.1 -> 0.0 | 4.6 -> 14.7 | `#afd4eb` -> `#7388b8` |

Read-out:
- **Clipping is fixed.** The floor shows its full veining and tile seams, and the moon shows its craters. The long-axis floor went from 100% clipped to 25%.
- **Overshoot on brightness.** The frame lost about 0.14 of mean luma and the sky went near-black (luma 0.04-0.06). Crushed blacks reached 15-20%. -0.8 stops is more than most views need. The long-axis view needs it because auto-exposure over-brightens when dark pillars fill the frame.
- Colour is cooler and more saturated (mean saturation +0.05-0.12): periwinkle floor, cyan neon, warm moon and pillar pools.
- Shots 2 and 4 are framed slightly differently from the baseline because the camera teleport lagged. The `black_inlay` and `haze` rows in shot 4 miss their targets, so only the global rows compare.
- The floor veining looks slightly blotchy at the overview angle (grey patches inside the warm light pools). Not yet investigated.

## Round 3: retuned (`shaderC/`), recommended starting point

`r_gradeExposure -0.65`, `r_gradeShoulder 0.6`, `r_gradeSplit 0.3`, `r_gradeLift 0.03`; the rest as round 2 (`r_gradeTemp 0.05`, contrast 1.10, saturation 0.92, vignette 0.28, grain 0.02, bloom 0.4, SSAO + sun-rays on, PBR/cubemaps off). `-0.5` was tried and clips the long-axis floor again.

| Shot | Mean luma (base / B / C) | Clipped % (base / B / C) | Crushed % (base / B / C) |
|---|---|---|---|
| 1 portal | 0.41 / 0.29 / 0.32 | 4.2 / 0.1 / 0.1 | 7.1 / 19.2 / 7.3 |
| 2 axis | 0.42 / 0.29 / 0.31 | 14.9 / 1.9 / 4.2 | 10.8 / 14.3 / 7.7 |
| 3 terrace | 0.43 / 0.27 / 0.30 | 10.8 / 0.0 / 0.2 | 2.2 / 14.6 / 6.0 |
| 4 colonnade | 0.37 / 0.29 / 0.30 | 11.9 / 1.1 / 1.2 | 4.6 / 19.5 / 6.6 |
| 5 overview | 0.44 / 0.27 / 0.28 | 8.1 / 0.0 / 0.0 | 4.6 / 14.7 / 6.6 |

Read-out:
- Crushed blacks are back near baseline (6-8%), from the black lift plus a lighter bias. Mean saturation sits between baseline and round 2.
- Floor luma is 0.54-0.83 (baseline 0.81-1.0), so veining is visible in every shot. The sky is still darker than baseline (0.055-0.077 vs 0.083-0.13).
- Mean luma is 0.28-0.32, still below the baseline's 0.37-0.44. The remaining gap is the sky and the shade side.
- The long-axis view is still the hard case: 4% clipped and a pale mint floor. A fixed exposure bias cannot fix it, because auto-exposure over-brightens whenever dark pillars fill the frame. The fix belongs in the auto-exposure target (for example, weight toward the lit surfaces or cap how far it brightens), not in the bias.

## Round 4: auto-exposure metering fix (`shaderD/`)

New cvar `r_gradeMeterFloor` (log2 luminance, default -10 = unchanged). Auto-exposure averages log luminance over the whole frame, so dark sky and pillars made it brighten the lit floor until it clipped. The floor clamps each pixel's metered luminance before the average, so dark areas pull less. Code: `calclevels4x_fp.glsl` (first pass reads the floor from `u_Color.x`), passed from `RB_ToneMap`.

Same settings as round 3 plus `r_gradeMeterFloor -5`. Numbers in `shaderD/baseline.json`.

| Shot | Mean luma (base / C / D) | Clipped % (base / C / D) | Crushed % (base / C / D) |
|---|---|---|---|
| 1 portal | 0.41 / 0.32 / 0.32 | 4.2 / 0.1 / 0.1 | 7.1 / 7.3 / 7.4 |
| 2 axis | 0.42 / 0.31 / 0.33 | 14.9 / 4.2 / **0.9** | 10.8 / 7.7 / 11.2 |
| 3 terrace | 0.43 / 0.30 / 0.27 | 10.8 / 0.2 / 0.0 | 2.2 / 6.0 / 6.5 |
| 4 colonnade | 0.37 / 0.30 / 0.32 | 11.9 / 1.2 / 1.3 | 4.6 / 6.6 / 7.2 |
| 5 overview | 0.44 / 0.28 / 0.31 | 8.1 / 0.0 / 0.0 | 4.6 / 6.6 / 4.4 |

- The long-axis view is fixed (4.2% -> 0.9% clipped): floor veining, tile seams and moon craters all show. It is also framed like the baseline again.
- The overview's blotchy veining is gone, so it was an exposure artefact.
- Terrace got slightly darker (luma 0.30 -> 0.27), the other views slightly brighter. Floor luma is 0.60-0.81 everywhere; sky 0.05-0.07.

### Recommended starting preset

```
r_gradeMeterFloor -5
r_gradeExposure -0.65
r_gradeShoulder 0.6
r_gradeSplit 0.3
r_gradeLift 0.03
r_gradeTemp 0.05
r_gradeContrast 1.10
r_gradeSaturation 0.92
r_vignette 0.28
r_filmGrain 0.02
r_bloom 0.4
r_ssao 1          // latched, needs vid_restart
r_drawSunRays 1   // latched
r_pbr 0           // keep off: blows the floor out
r_cubeMapping 0   // keep off
```

Not yet done: these are live cvars on the test instance, not shipped defaults; `cg_timeGrade` is off in all of these shots, so its freeze/surge shift on top of this grade is untested.

## Round 5: lighting rebalance (`shaderE/`), the fix for "sky too dark, light weird"

Diagnosis and targets are in `docs/COLOUR_SCIENCE.md`. The plaza was lit about a stop too hot for its unlit moon sky, so no exposure served both. Changes in `tools/strafegen/strafegen_nocturne.py`, then rebaked (`python3 strafegen_nocturne.py`, about 75 s) and redeployed to `assets/openarena/baseoa/`:
- plaza bake scale 2.2 -> 1.1
- warm pillar lights 500 -> 280, blue portal lights 420 -> 300
- previous bake kept as `generated/nocturne/strafe64_nocturne.v1-bright.pk3`

Grade for this round: `r_gradeExposure -0.35` (was -0.65), `r_gradeShoulder 0.4`, `r_gradeLift 0.02`, `r_gradeSplit 0.3`, `r_gradeMeterFloor -5`; the rest as round 4.

| Shot | P50 luma (base / D / E) | Sky luma (D / E) | Sky-to-floor ratio (D / E) | Clipped % (D / E) |
|---|---|---|---|---|
| 1 portal | 0.24 / 0.12 / **0.22** | 0.07 / 0.10* | 9.2 / **5.2** | 0.1 / 0.1 |
| 2 axis | 0.20 / 0.12 / 0.12 | n/a (box on lintel) | n/a / 5.3 | 0.9 / 1.6 |
| 3 terrace | 0.24 / 0.12 / **0.18** | 0.05 / 0.08 | 13.9 / **8.0** | 0.0 / 0.1 |
| 4 colonnade | 0.20 / 0.12 / **0.17** | 0.04 / 0.05 | 21.6 / 14.7 | 1.3 / 1.1 |
| 5 overview | 0.33 / 0.31 / **0.28** | 0.06 / n/a | 9.4 / **5.1** | 0.0 / 0.1 |

(*) approximate; the sky ROI is in a different place for this view. Numbers: `scopes/metrics.json`.

Result: the sky now reads bright with its stars and Milky Way visible at the same exposure that leaves the floor unclipped; median luma is back to 0.17-0.28 in four of five views (target 0.17-0.30); sky-to-floor ratio fell from 9-22 to 5-8 in three views; the warm pools now read as soft light on the pillars. Clipping is at or just over 1% in two views.

Still failing the targets:
- **Split-toning:** hue gap about 0-4 deg in four views (target >= 30). `r_gradeSplit` has no real effect; needs the linear-HDR approach in the next steps of `COLOUR_SCIENCE.md`.
- **Moon:** blown to cream (no craters) in the axis and portal views at this exposure; the axis view also crushes 17.6% of pixels and has a median of 0.12.
- **P99 luma:** 0.65-0.9 in four views, under the 0.85 target. Some of these shots have no bright object, so the target itself may be too strict.
- **Colonnade sky** is still dark (0.05): that view looks up at the darkest part of the sky.

## Round 6: moon headroom + stronger split-toning (`shaderF/`)

New: `r_gradeWhitePoint` (tonemap white point in stops; default 0 = unchanged; `tr_scene.c`), split-toning reworked (multiplicative, luma-preserving; teal-cool shadows, peach highlights), shoulder made continuous at the knee (the old version left a step that showed as blotchy veining on lit floor). Neon cyan is protected by a gentle highlight tint (an earlier stronger one turned it lime).

Preset: `r_gradeWhitePoint 1.5`, `r_gradeExposure 0.35`, `r_gradeSplit 0.8`, `r_gradeShoulder 0.4`, `r_gradeLift 0.02`, `r_gradeSaturation 1.0`, `r_gradeTemp 0.03`, `r_gradeMeterFloor -5`; rest as round 5. Metrics: `shaderF/metrics.json`.

| Metric | Round 5 (E) | Round 6 (F) |
|---|---|---|
| Clipped % (5 views) | 0.1-1.6 | **0.03-0.09** (all pass) |
| Median luma P50 | 0.12-0.28 | 0.18-0.31 (4 of 5 in target) |
| Sky-to-floor ratio | 5.1-14.7 | 1.9-11.7 (4 of 5 pass) |
| Crushed % axis view | 17.6 | 17.8 (unchanged) |
| Hue gap (shadow vs highlight) | 0-4 deg in 4 views | 4-102 deg (2 of 5 now >= 30) |
| Moon | cream blob | craters visible in portal/axis views |

Still failing: P99 luma is 0.63-0.84 in all views (target 0.85+), the axis view still crushes 17.8% and its sky-box metric lands on a lintel, the colonnade sky is still dark (0.05), and the hue-gap metric is noisy: it averages only the few very bright pixels, so one view's value mostly reflects what object is brightest. Judge by the screenshots too. P99 may be too strict a target for frames with no bright object; reconsider with reference stills.
