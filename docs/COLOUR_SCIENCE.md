# Colour science and look targets (2026-11-02)

Goal look: **hyper-real, ethereal night**: believable light and material response, moonlit and dreamy, with the sky and the ground belonging to one world. This file records what was researched, how we measure, the numeric targets, and what to change next. Tooling is in `tools/colour/metrics.py`, targets in `docs/colour-targets.json`, shots and scopes in `docs/screenshots/baseline/`.

## 0. Look targets from the concept art (2026-11-02, supersedes the proposals in section 4)

Reference set: your four concept images (`assets/concept-art/nocturne-reservoir/{causeway,waterside}.png`, `nocturne-world-v1/{moon-court,rain-gallery}.png`; prompts in the two `generation.json` files). Measured with `tools/colour/metrics.py`; numbers in `docs/screenshots/references/metrics.json`; targets rewritten in `docs/colour-targets.json` (the references pass them, our shots do not).

**What the look is, in numbers** (4 references):

| Trait | Reference | Our latest shots (round 6) |
|---|---|---|
| Mean Oklab chroma | 0.021-0.025 | 0.050-0.065 (2-3x too colourful) |
| Ground chroma (bottom quarter) | 0.017-0.021, neutral grey-ivory stone | 0.053-0.060, periwinkle |
| Sky chroma (top quarter) | 0.024-0.030 | 0.056-0.066 |
| Ground luma / sky luma | 1.3-2.5 | 4-7 |
| Ground luma | 0.21-0.30 | 0.39-0.58 |
| Sky luma | 0.12-0.17 | 0.08-0.15 |
| Black level P1 | 0.030-0.055 (lifted) | 0.013-0.027 |
| Crushed (< 0.03) | 0.04-1.0 % | 2-8 % |
| Highlights P99 | 0.71-0.83 | 0.63-0.84 (already in range, so the old 0.85 target was wrong) |
| Clipped | 0.06-0.45 % | < 0.1 % |
| Warm (amber) pixels, share of coloured pixels | 3-23 % | 0.1-0.7 % (overview 4 %) |
| Shadow hue / highlight hue | about 255 deg (blue) / 65-95 deg (amber): gap 107-172 | gap 4-62 |

**Reading of the art** (what to copy):
1. **Neutral stone, coloured light.** The stone is pale warm-grey ivory and black-veined marble; blue lives in the sky, the shade and the water, not in the material. Ours is a blue-tinted floor, which is where most of the extra chroma comes from.
2. **The ground is darker than we think.** It sits only about 1.3-2.5x brighter than the sky; the moon and the warm lamps are the brightest things in frame. Our plaza is the brightest object by 4-7x.
3. **Warm amber is a real second colour,** carried by many small recessed floor and step strips (and wall lights), with cyan only on a few thin seam lines and a lintel. These warm points are what create the 100+ degree hue gap.
4. **Lifted blacks and almost no clipping:** nothing is pure black, nothing is blown, and highlights stop around 0.8.
5. **Reflective surfaces.** Still water or wet polished stone mirrors the moon, lamps and structures. This is a large part of the "hyper-real" read and we have none of it (r_pbr / cubemaps are off; planar reflection would need map or renderer work).
6. **Structure:** large monolithic forms, human-scale detail (stone joints, chisel marks, cracks), uneven stone, steps with lit nosings, volumetric haze and cloud sea at the horizon.

**Gap list** (ordered by how far we are from the references, and what to try):
1. Floor and wall material: re-tint to a neutral warm-grey (texture or vertex colour), keep the blue in the shade only. Biggest chroma and luma win.
2. Lower the key light on the ground so ground/sky ends up at 1.3-2.5; the moon, sky and the warm strips become the brightest things.
3. Replace the blue split-tone with the references' scheme: cool blue shadows, amber highlights; add many low warm point/strip lights (step nosings, floor strips, column bases) instead of a few big pools.
4. Raise the black level (`r_gradeLift` about 0.04-0.06) and reduce crush.
5. Lower overall saturation (`r_gradeSaturation` about 0.6) once the floor is neutral; do not do this first, or the neon dies.
6. Reflections: water/wet stone planes, then PBR/cubemaps once the floor is not blown out.
7. Detail and haze: stone joints, chisel marks, horizon haze and cloud sea (the overview already has the clouds).

## 1. What went wrong in rounds 2-4 (measured, not by eye)

Your report: the sky is too dark and doesn't fit the world, and the light looks weird (the pillar with an orange lower half and a ragged edge).

| Metric (terrace view) | Baseline | Round 4 | Target |
|---|---|---|---|
| Median luma P50 | 0.24 | 0.12 | 0.17-0.30 |
| Sky region luma | 0.08 | 0.05 | 0.09-0.20 |
| Lit floor luma | 0.94 | 0.64 | 0.50-0.80 |
| Sky-to-floor luma ratio | 11.6 | 13.9 | <= 7.5 |
| Clipped % | 10.7 | 0.0 | <= 1 |
| Shadow vs highlight hue gap | 106 deg | 2 deg | >= 30 deg |

- **I fixed clipping by darkening everything.** A global exposure bias pulls the floor under the clip point, but the sky is an unlit texture, so it falls by the same amount. The floor-to-sky gap actually widened (11.6 -> 13.9). Clipping was a symptom; the cause is the **lighting ratio**: the plaza is lit far hotter than the sky, so no single exposure serves both.
- **The "split-toning" did almost nothing.** Highlight and shadow hue are both about 264 deg (blue); the hue gap went from 106 to 2. The frame is a single narrow blue hue spike on the hue histogram (see `scopes/shaderD_nocturne-3-terrace_scopes.png`), which the grading guides below call flat.
- **Pillar look.** The orange lower half is the map's warm point lights (`light 500`, colour `1 0.78 0.55` in `strafegen_nocturne.py`) hitting the pillar, then pushed into a saturated, bright orange by the lower exposure plus shoulder. I reproduced the look, but I could not cleanly A/B it: the camera drifted between shots. The ragged edge is most likely the light's radius/lightmap cutoff (it also shows as grey patches inside the warm pools in the earlier overview shot); that is a hypothesis, not confirmed.

## 2. Research: how game colour science works

(Sources are listed at the end. Where a source is a forum post or a vendor page I say so.)

**Pipeline order.**
- Light and shade in scene-referred linear HDR; grade in scene-referred space; tonemap to display. Unreal's docs say grading runs in scene-referred linear before tonemapping. Unity's docs say values above 1 are clamped without a tonemapper, which changes the luminance balance.
- Our renderer does the opposite for most of the look: it tonemaps first, then grades the already-clamped LDR image in `RB_ColorGrade`. Anything clipped at the tonemap cannot be recovered there. That is why the shoulder in the grade pass could not fix highlight clipping and why I needed an exposure bias at the tonemap.
- LUT-based grading is the standard tool, but NVIDIA's HDR guide warns about LUTs that ignore sRGB non-linearity; an ACES-style look transform is the suggested alternative.
- Tonemapper choice: filmic curves (ACES, Hable) give a "cinematic" look. A forum report says ACES raises contrast and darkens shadows; Unity calls its Neutral tonemapper a good start for heavy grading. Our tonemapper is Hable's Uncharted 2 curve (shoulder 0.22, linear strength 0.30, angle 0.10, toe 0.20, 0.01/0.30), the same family as the published defaults (A 0.15, B 0.50, C 0.10, D 0.20, E 0.02, F 0.30, white point 11.2).
- Unreal's filmic film-stock defaults, as a reference for curve shape: slope 0.88, toe 0.55, shoulder 0.26, black clip 0, white clip 0.04.

**Exposure and metering (this is where our problem is).**
- Auto-exposure maps a metered average luminance to middle grey (18%). Unreal's histogram mode uses 64 bins of log luminance and lets you meter a **percentile range** (docs suggest a low percent of 70-80 and a high percent of 80-95) instead of the plain mean, so dark or very bright areas do not dominate.
- Exposure = 1 / 2^(EV100 + compensation). Compensation is in stops: +/-1 is 2x. Min/Max EV100 clamp how far auto-exposure may go, and adaptation speed is in stops per second (separate up and down speeds).
- An exposure compensation **curve against average scene EV** is how Unreal keeps nights from auto-exposing to look like day. A vendor's plugin page (marketing, not a standard) describes the same night-exposure control.
- Our metering is a plain average of log luminance over the whole frame, which is why a frame of dark sky and pillars brightens the lit floor. My `r_gradeMeterFloor` clamps the dark end, which works but is a blunt version of percentile metering.

**Night and "ethereal".**
- Cinema "day for night" practice: expose about 2 to 2.5 stops down (a 2004 practitioner post; rule of thumb only), desaturate (rods take over), and tint toward blue. Moonlight itself is not blue (about 4000 K); the blue comes from the Purkinje shift and convention.
- Grading guides warn that a blanket blue tint is a crutch; you need colour separation. Common recipe: cool teal-blue in the shadows (hue about 210-220 on their scale, low saturation), warm accents in highlights and skin, and keep a small warm accent so the frame is not monochrome.
- Bloom is a general glow around bright areas; halation is film's warm red-orange halo around highlights. Both are finishing touches, kept restrained, applied after the tonal edit, with a little grain or fade for an analogue feel.
- Tutorials also say visible daylight detail in the sky is the giveaway of a fake night. Our sky has stars, a moon and a cloud sea, so it should read lit by that moon, not be crushed to black.
- Gap in the research: I found no quantified, authoritative values for moonlight lux, sun-to-sky ratio or night key brightness, and no perception research on photoreal lighting. The numeric targets below are therefore partly proposals (marked P).

**Hyper-real, art-directed.** The Cyberpunk 2077 lighting team's GDC 2022 talk frames the goal as a living digital film set, with shared contrast rules and a defined colour scheme per district. A reference artist's quote I found argues hyper-realism still needs deliberate art direction. Takeaway for us: pick a small palette, give every district shared contrast rules, and use measured references rather than eyeballing.

## 3. Metrics and tools (what we measure and how)

Implemented in `tools/colour/metrics.py` (numpy, Pillow, matplotlib; works on any screenshot):

| Metric | Why |
|---|---|
| Luma percentiles P1 / P5 / P50 / P95 / P99 (Rec.709 Y' of the gamma-encoded frame) | key brightness, black floor, highlight ceiling |
| Clipped % (any channel >= 250) and crushed % (Y' < 0.03) | lost detail at both ends |
| Dynamic range in stops (P99 vs P1, linearised) | contrast |
| Oklab L, mean and P95 chroma | perceptual brightness and colourfulness; Oklab is designed for good lightness and hue prediction |
| Shadow hue, highlight hue, hue gap (mean Oklab a,b of L<0.35 vs L>0.75) | split-tone / colour separation |
| Region luma and hex via `--roi` (sky, floor) and **sky-to-floor ratio** | world cohesion: the metric that would have caught this problem |
| dE_OK mean and P95 between two shots of the same view (`compare` mode) | how much a change moved the image; Euclidean distance in Oklab (x100) is a common choice, with the caveat that its axis scaling is debated |

Scopes sheet per image: luma waveform, RGB parade, log luma histogram with P1/P50/P99 markers, Oklab vectorscope (a-b plane) and chroma-weighted hue histogram. Read them as video colourists do: waveform for brightness by screen position, parade for channel balance, vectorscope for hue and saturation (distance from centre).

How to use:
```
python3 -I tools/colour/metrics.py report SHOTS... -o out_dir --targets docs/colour-targets.json --roi sky=0.3,0.03,0.7,0.2 --roi floor=0.3,0.8,0.7,0.97
python3 -I tools/colour/metrics.py compare REF.jpg TEST.jpg -o out_dir
```
The sky/floor ROI boxes assume the framing of the five nocturne views; in the long-axis shot the "sky" box lands on a lit lintel, so its sky numbers there are not meaningful. Capture protocol: pinned post stack, 1280x720, wait about 7 s per teleport for exposure to settle, same camera positions.

## 4. Earlier targets (proposals; superseded by section 0 and the current `docs/colour-targets.json`)

| Metric | Target | Basis |
|---|---|---|
| Clipped % | <= 1 | research (clipped highlights lose detail) |
| Crushed % | 2-8 | proposal: lifted but not flat |
| Luma P1 | >= 0.01 | proposal: hazy, never pure black |
| Luma P50 | 0.17-0.30 | approved baseline look (0.24) |
| Luma P99 | 0.85-0.97 | proposal: reaches near white, rolls off |
| Mean Oklab chroma | 0.04-0.09 | approved baseline (0.056) |
| Shadow/highlight hue gap | >= 30 deg | research (avoid a monochrome frame) |
| Sky luma | 0.09-0.20 | approved baseline (0.08-0.13) |
| Lit floor luma | 0.50-0.80 | proposal: below the shoulder so veining survives |
| Sky-to-floor luma ratio | <= 7.5 | proposal: within about 3 stops |

Status: **no shot passes**. Baseline fails clip, floor brightness and ratio; round 4 fails median, sky, ratio, highlight ceiling and hue gap. The full per-shot report is the output of the `report` command above (`scopes/metrics.json`). These targets should be re-derived from reference stills you choose (a handful of film or game frames you consider on-target, run through the same tool) before they are final.

## 5. Status after the lighting rebalance (round 5)

Step 1 below is done: the nocturne is now baked about a stop lighter with softer warm pools (see `docs/screenshots/baseline/README.md`, round 5). Median luma is back to 0.17-0.28 in four of five views, sky-to-floor ratio fell from 9-22 to 5-8 in three, and the sky reads bright again at an exposure bias of only -0.35. Split-toning, the blown moon and the colonnade sky are still open.

## 6. Recommended next steps

1. **(Done, round 5)** **Fix the lighting ratio, not the global exposure.** Lower the key/warm light and the plaza floor brightness, and raise the sky's brightness (sky shader scale or `sky_ether_night`) so sky and floor sit within about 3 stops. Then drop `r_gradeExposure` back near 0.
2. **Replace `r_gradeMeterFloor` with percentile metering** (meter between the 70th-95th percentile of log luminance, like Unreal's histogram mode), plus min/max EV clamps and a night compensation curve.
3. **Make split-toning real.** Do it in linear HDR before tonemap (or measure it with the hue-gap metric): cool teal-blue shadows, a small warm accent in highlights; verify the gap with the tool.
4. **Soften the warm pools:** lower light intensity and radius falloff, check lightmap resolution at the cutoff edge, and use the warm tone only as an accent (halation-like) per the guides.
5. Collect 5-10 reference stills, add them under `docs/targets/`, run `report` on them and tighten `colour-targets.json` from the real numbers.
6. Test with `cg_timeGrade` on once the static grade passes.

## Sources

- [Unreal: color grading and the filmic tonemapper](https://dev.epicgames.com/documentation/unreal-engine/color-grading-and-the-filmic-tonemapper-in-unreal-engine)
- [Unreal: auto exposure](https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine)
- [Unreal 4.27 color grading, legacy values](https://docs.unrealengine.com/4.27/RenderingAndGraphics/PostProcessEffects/ColorGrading)
- [NVIDIA SHIELD HDR developer guide](https://docs.nvidia.com/gameworks/content/devices/shield-hdr-dev-guide/hdr-dev-guide-nvidia-shield.htm)
- [Unity post-processing color grading (2018.1)](https://docs.unity3d.com/ru/2018.1/Manual/PostProcessing-ColorGrading.html)
- [Unity forum: texture workflow for tonemapping (anecdotal)](https://discussions.unity.com/t/texture-creation-workflow-for-tonemapping/896055)
- [Uncharted 2: HDR Lighting, J. Hable (slides)](https://www.slideshare.net/naughty_dog/lighting-shading-by-john-hable)
- [LYGIA uncharted2.glsl (parameter values)](https://git.gay/kaneraZh/lygia/src/branch/main/color/tonemap/uncharted2.glsl)
- [Day for night (Wikipedia)](https://en.wikipedia.org/wiki/Day_for_night)
- [Why colour grade moonlight blue (PremiumBeat)](https://www.premiumbeat.com/blog/why-color-grade-moonlight-blue/)
- [Day-for-night shooting notes (CML)](https://cinematography.net/edited-pages/DayForNight-AdditionalShootingInformation.htm)
- [Bloom & halation (Darkroom)](https://darkroom.co/help/edit/bloom-halation)
- [Cinematic colour grading for landscape photography (Lagerstedt)](https://mikkolagerstedt.com/blog/cinematic-color-grading-landscape-photography)
- [Oklab colour space (Wikipedia)](https://en.wikipedia.org/wiki/Oklab_color_space)
- [An interactive review of Oklab](https://notes.billmill.org/visualization/interactive_explainers/An_interactive_review_of_Oklab.html)
- [Learn to read video scopes (Larry Jordan)](https://larryjordan.com/articles/learn-how-to-read-video-scopes/)
- [How to read scopes in Resolve (Envato Tuts+)](https://photography.tutsplus.com/tutorials/how-to-read-scopes-in-resolve-waveform-parade-histogram-vectorscope--cms-41868)
- [Cyberpunk 2077 GDC 2022 lighting session (Game Developer)](https://www.gamedeveloper.com/marketing/find-out-how-cyberpunk-2077-lit-up-night-city-in-this-gdc-2022-session)
- [Creating lighting and environments for Cyberpunk 2077 (80.lv)](https://80.lv/articles/creating-lighting-and-environments-for-cyberpunk-2077/)
