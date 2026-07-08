# Quake-engine ports — high-quality lighting & graphics survey

Goal: find lighting/graphics tech worth integrating for a better-looking STRAFE 64.
Our renderer is **rend2** (`renderergl2`), the XreaL-derived ioquake3 GL2 renderer. The
big realization from this survey: **rend2 already ships most of the "modern Quake" feature
set** — so the win is less "adopt a new engine" and more "lift specific features from forks
of our own renderer, then close 3–4 named gaps."

---

## 0. Where we already stand (rend2 feature inventory)

Confirmed present in our `renderergl2` (cvars + GLSL shipped):

`r_pbr`, `r_normalMapping`, `r_specularMapping`, `r_specularScale`, `r_parallaxMapping`,
`r_deluxeMapping`, `r_cubeMapping` (image-based lighting), `r_hdr`, `r_toneMap`,
`r_bloom`, `r_ssao`, `r_sunShadows` + `r_sunlightMode` (cascaded/parallel-split sun
shadows), `r_shadowFilter`, `r_shadowMapSize`, `r_dlightMode`, point-light shadows
(`pshadow` GLSL). Plus our own additions: **DoF** (`r_dof`) and audio-reactive shaders.

So we have: PBR, normal/spec/parallax materials, IBL cubemaps, HDR+tonemap, bloom, SSAO,
cascaded sun shadows, DoF. That's already a strong, modern stack.

**The real gaps** (what high-end forks have that we don't):
1. **Many *shadowed* dynamic lights.** Our dlights are cheap/unshadowed; only the sun casts
   real shadows. Sword glow, projectiles, muzzle flashes don't shadow the world.
2. **Soft / higher-quality shadows** (EVSM, better PCF, omni shadow maps for point lights).
3. **Color grading / LUT + a linear-light pipeline + better AA** (we have tonemap, not grading; no FXAA/SMAA).
4. **Screen-space reflections & parallax-corrected cubemaps** (our IBL is static, non-corrected).
5. **Runtime global illumination / bounce probes** (we bake bounce statically via q3map2 `--bake`, but nothing dynamic).

---

## 1. The survey (ranked by integrability into rend2)

### Tier 1 — same codebase, directly cherry-pickable
**SomaZ / Jedi Academy "Rend2 — A Modern Renderer" (OpenJK / EternalJK / MBII)**
The single best target: it's *our rend2*, actively extended years past stock ioq3's.
Adds (completed or in-progress upstream): **global illumination** (SomaZ), image-based
lighting improvements, real-time dynamic lighting + real-time sun shadows refinements,
**RMO/ORM PBR materials**, refraction, multisampled AA, weather/rain. Because the
`tr_*` structures and GLSL match ours, features port as patches, not rewrites.
→ *Highest ROI. Mine GI, IBL, and material improvements straight from this fork.*

### Tier 2 — same renderer *family* (idTech3/XreaL), port the technique
**XreaL / ET:XreaL** — rend2's ancestor. Has what rend2 dropped: **EVSM + omni-directional
soft shadow maps**, parallel-split sun shadows, true 64-bit HDR with adaptive tonemapping,
optional **deferred shading**, **relief mapping**. The shadow-quality and point-light-shadow
code is the most reusable. C/GLSL, idTech3 lineage → portable with effort.
→ *Source for soft shadows + omni point-light shadows (gap #2, part of #1).*

### Tier 3 — different engine, architecture/feature *reference* (not liftable)
- **Daemon engine (Unvanquished)** — modern OpenGL3 C++ renderer. Standout: a **tiled
  dynamic-light renderer** that scales to *many* lights cheaply (exactly our gap #1), plus
  **PBR (physicalMap/ORM)**, **color grading**, **FXAA**, **linear-light blending** (0.56),
  motion blur, heat haze, rim lighting. Engine is a hard C++ fork — not cherry-pickable, but
  the **tiled-forward+ approach is the blueprint** for adding many shadowed lights to rend2.
- **DarkPlaces** (Q1, but loads Q3 BSP) — the **RTLights** system: realtime per-light
  shadows (stencil volumes *and* shadow maps), 256 dynamic lights, in-game light editor,
  `.rtlights` files. Reference for a dynamic-light workflow + the "bake static realtime
  lights from BSP light ents" trick.
- **FTEQW** (loads Q3 BSP/shaders) — RTLights, PBR, reflection/refraction. A reference, or
  a fallback "what does the content look like elsewhere" check.

### Tier 4 — aspirational north star (renderer replacement, not integration)
- **Quake II RTX / Q2VKPT** — full **path-traced GI** (unified shadows/reflections/refraction)
  with an **SVGF denoiser**, Vulkan RT. Gorgeous, but it's idTech2 + a Vulkan RT renderer —
  adopting it = replacing our renderer entirely and abandoning rend2. Useful only as a
  visual target / proof of what bullet-time cinematics *could* look like someday.

---

## 2. Recommended integration plan (prioritized)

Ordered by value-per-effort for our stylized, bullet-time aesthetic:

| Pri | Feature | Closes gap | Source | Effort |
|-----|---------|-----------|--------|--------|
| 1 | **Color grading LUT + FXAA/SMAA + linear-light pass** | #3 | Daemon as reference; cheap rend2 post-passes (we already have the FBO post chain from DoF) | Low |
| 2 | **Soft shadows** (better PCF / EVSM) on the sun | #2 | XreaL EVSM | Low-Med |
| 3 | **A few shadowed dynamic lights** (sword, projectiles, muzzle) via per-light shadow maps | #1 | XreaL omni shadows; cap count tightly | Med |
| 4 | **GI / IBL improvements** (bounce probes, parallax-corrected cubemaps) | #4,#5 | SomaZ rend2 (same codebase) | Med |
| 5 | **Tiled light renderer** for *many* dynamic lights | #1 (scale) | Daemon blueprint; bigger rework | High |
| 6 | Path-traced mode | — | Q2RTX north star | Out of scope |

**Why this order:** #1 (grading + AA + linear light) is the cheapest, most visible quality
jump and reuses our existing DoF post-pass plumbing. #2/#3 make our *existing* shadows look
high-end and finally let the sword/bolts light the world — which pairs naturally with the
bullet-time + DoF cinematics we already have. #4–#5 are the "true GI" tier and bigger bets.

## 3. Art-direction caveat
STRAFE 64 is a stylized N64/dev-texture, motion-clarity-first game. "High-quality lighting"
should serve readability and cinematic bullet-time, not photoreal clutter. The cheap wins
(color grading, soft sun shadows, a couple of expressive shadowed dynamic lights for the
katana/bolts) will read as a bigger upgrade than chasing path-traced GI — and won't fight
the framerate budget our DoF/bloom/baked stack already spends (~5–7ms).

## 3b. Phase 1 — IMPLEMENTED & benchmarked (branch `feature/photoreal-grade`)

Built the priority-1 item: a single full-screen **photoreal-finish pass** at the end of
the rend2 post chain (`RB_ColorGrade` in `tr_postprocess.c`, GLSL in
`glsl/colorgrade_*.glsl`), doing **FXAA + cinematic colour grade + vignette + film grain**.
Reuses the existing DoF FBO/scratch plumbing; one new GLSL program; all knobs live (not
latched — `r_grade 0` skips the whole pass). This is the "photoreal finish over a lofi
base" look: clean the low-poly jaggies, warm/shape the colour like graded film, then a
soft vignette + a whisper of animated grain for the nostalgic tell.

**Cvars** (all `CVAR_ARCHIVE`, live):

| cvar | default | meaning |
|------|---------|---------|
| `r_grade` | 1 | master toggle for the whole pass |
| `r_fxaa` | 1 | edge antialias inside the pass |
| `r_gradeContrast` | 1.06 | S-curve contrast (1 = neutral) |
| `r_gradeSaturation` | 1.08 | saturation (1 = neutral) |
| `r_gradeTemp` | 0.04 | white balance (+ warmer / − cooler) |
| `r_vignette` | 0.18 | edge darkening |
| `r_filmGrain` | 0.03 | animated grain strength (crank for VHS) |

**Benchmark** — `timedemo` (120 deterministic frames), 2560×1440, uncapped, full post
stack live (HDR/tonemap/DoF/bloom/sun-shadows). GPU: **Apple M3 Pro via OpenGL 4.1
(Metal-88)** — i.e. the rend2 GL2 path running through Apple's deprecated GL→Metal
translation layer.

| Config | fps | avg ms | min ms (GPU floor) |
|--------|-----|--------|--------------------|
| grade OFF | 65.6 | 15.2 | 8.0 |
| grade ON, FXAA off (grade+vignette+grain) | 64.1 | 15.6 | 9.0 |
| grade ON, full (incl. FXAA) | 63.6 | 15.7 | 9.0 |

**Cost of the full pass: ~0.5 ms/frame average, ~1.0 ms at the GPU-bound floor — ~3% at
65 fps.** FXAA adds only ~0.1 ms on top of the grade math here (the base pass — FBO bind +
fullscreen quad + grain hash + blit-back — dominates). Scales ~linearly with pixel count,
so ~0.3 ms at 1080p.

Two caveats worth remembering:
- The absolute **65 fps at 1440p on an M3 Pro is the macOS GL-over-Metal penalty**, not the
  GPU and not this pass — it reinforces the survey's core point that *the renderer path is
  the real perf bottleneck on Mac*. On a native GL/Vulkan renderer the headroom is far
  larger and this pass is even cheaper in relative terms.
- Run-to-run noise is ~±0.2–0.3 ms, so the grade-math-vs-FXAA split sits near the noise
  floor; treat it as "whole pass ≈ 0.5–1 ms, FXAA is the cheap part."

Verdict: cheap enough to ship on by default; it does not meaningfully dent the frame budget
that DoF/bloom/HDR already spend. Next candidates from §2: soft sun shadows (XreaL EVSM),
then a few shadowed dynamic lights for the katana/bolts.

## 3c. Phase 2 — soft sun shadows (branch `feature/photoreal-grade`)

Priority-2 from §2. rend2's sun-shadow PCF used a fixed ~2-texel kernel, so edges read
stair-stepped. Change (`shadowmask_fp.glsl` + `tr_glsl.c` + `tr_init.c`): **decouple
penumbra width from tap count.**

- `r_shadowSoftness` (new, default **2.0**, latched) scales the PCF kernel *radius*. This
  is **free** — it spreads the *same* taps wider, no extra samples — so the default now
  gives a softer, filmic edge for ~nothing.
- `r_shadowFilter` (default left at **1** = 3-tap) selects *tap count*. 2 = 9-tap = a
  smoother penumbra but ~3× the shadow samples. Kept **opt-in**, not defaulted.

**Why filter stays opt-in — and a benchmarking caveat worth recording.** I tried to
timedemo hard-vs-soft but the numbers were **contaminated** and I'm not reporting them as
clean: (a) back-to-back uncapped 1440p timedemos **thermally throttle** the M3 Pro (the
first cold run read ~49 fps, every subsequent run ~35 regardless of settings), and (b) the
**autonomous daemon attaches to the shared engine instance** and changed resolution
(snuck in a 640×360 run) and drove gameplay mid-measurement. The one thing that *did*
replicate: cost tracks **tap count** (3→9), not kernel width — consistent with the theory
that widening offsets is free and extra `shadow2D` samples are not. So the safe call is
softness-by-default (free), 9-tap opt-in (unquantified but real).

**Lesson for future perf work:** use a *dedicated* instance the daemon isn't driving,
interleave A/B/A/B to cancel thermal drift, and let the GPU idle between runs — or measure
on a desktop GPU without GL-over-Metal throttling. The grade-pass numbers in §3b were taken
early (cold, fewer back-to-back runs) and are the more trustworthy of the two.

## 4. Sources
- ET:XreaL — https://github.com/QuakeEngines/ET-XreaL
- Daemon engine (Unvanquished) — https://wiki.unvanquished.net/wiki/Engine ; https://www.phoronix.com/news/Unvanquished-0.56
- rend2 (SomaZ / JKA) — https://community.moviebattles.org/threads/a-modern-renderer-rend-2-graphics-mod.3799/ ; https://jkhub.org/tutorials/rend2/rend2-cvar-list-r212/
- DarkPlaces features — https://hemebond.gitlab.io/darkplaces-www/engine/features/ ; rtlights https://quakewiki.org/wiki/RTlights_in_FTEQW
- FTEQW — https://www.quakeworld.nu/wiki/FTE_QW
- Quake II RTX / Q2VKPT — https://github.com/NVIDIA/Q2RTX ; https://brechpunkt.de/q2vkpt/
