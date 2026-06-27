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

## 4. Sources
- ET:XreaL — https://github.com/QuakeEngines/ET-XreaL
- Daemon engine (Unvanquished) — https://wiki.unvanquished.net/wiki/Engine ; https://www.phoronix.com/news/Unvanquished-0.56
- rend2 (SomaZ / JKA) — https://community.moviebattles.org/threads/a-modern-renderer-rend-2-graphics-mod.3799/ ; https://jkhub.org/tutorials/rend2/rend2-cvar-list-r212/
- DarkPlaces features — https://hemebond.gitlab.io/darkplaces-www/engine/features/ ; rtlights https://quakewiki.org/wiki/RTlights_in_FTEQW
- FTEQW — https://www.quakeworld.nu/wiki/FTE_QW
- Quake II RTX / Q2VKPT — https://github.com/NVIDIA/Q2RTX ; https://brechpunkt.de/q2vkpt/
