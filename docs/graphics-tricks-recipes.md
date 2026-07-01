# STRAFE 64 — Quake-Engine Light & Graphics Tricks (Recipe Book)

A field guide to the cool light/graphics tricks the idTech3 family (Quake 3 / ioquake3
/ OpenArena, which is what STRAFE 64 is built on) can pull off — and *how* to do each
one. Split into three layers:

1. **Shader-script tricks** — `.shader` text files. No rebuild, no recompile. Hot-swap with `vid_restart`.
2. **GL2 / rend2 renderer tricks** — modern PBR-ish features already compiled into our renderer (HDR, normal/specular, sun shadows, SSAO, cubemaps). Driven by cvars + shader keywords.
3. **q3map2 compile-time light tricks** — baked lightmaps, radiosity, sky/sun light. Relevant to `tools/strafegen` map output.

Everything here is keyed to what *our* build already supports. Cross-refs to memory notes
in `[[brackets]]`. Most of this is pure data — perfect for `strafegen` to emit
programmatically.

---

## 0. Mental model: how an idTech3 shader draws

A shader is a named surface material made of **stages**, each stage a texture pass blended
onto the last. The engine draws stage 1, then blends stage 2 over it, etc. The art of
idTech3 eye-candy is *layering cheap passes*:

```
textures/strafe64/energy_panel
{
    qer_editorimage textures/strafe64/panel.tga   // editor preview only
    surfaceparm nomarks
    {
        map textures/strafe64/panel.tga           // base diffuse
        rgbGen identity
    }
    {
        map textures/strafe64/scanline.tga         // glowing overlay
        blendFunc add                              // additive = light, never darkens
        tcMod scroll 0 -0.5                         // crawls upward
        rgbGen wave sin 0.6 0.4 0 0.3               // pulses brightness
    }
}
```

Key blend modes:

| `blendFunc` shorthand | Math | Use for |
|---|---|---|
| `add` (`GL_ONE GL_ONE`) | dst + src | glows, energy, fire, light — **never darkens**, ignores black |
| `filter` (`GL_DST_COLOR GL_ZERO`) | dst × src | grime, shadow, multiply detail, lightmaps |
| `blend` (`GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA`) | alpha lerp | decals, dirt, soft edges |

> Gotcha (from `[[strafe64-cgame-effect-gotchas]]`): additive (`add`) stages are
> brightness-only — fade them by driving **RGB toward black**, not by lowering alpha.

---

## Layer 1 — Shader-script tricks (no rebuild)

### Recipe 1.1 — Scrolling energy / forcefield / waterfall
The workhorse. `tcMod scroll <s> <t>` slides UVs per second; stack two at different rates
for parallax depth.

```
{
    map textures/strafe64/grid.tga
    blendFunc add
    tcMod scroll 0 -1.4          // fast inner layer
}
{
    map textures/strafe64/grid.tga
    blendFunc add
    tcMod scroll 0 -0.4          // slow outer layer → fake depth
    rgbGen wave sin 0.5 0.5 0 0.7
}
```

### Recipe 1.2 — Pulsing / breathing glow
`rgbGen wave <func> <base> <amp> <phase> <freq>` modulates brightness over time.
`func` ∈ `sin | triangle | square | sawtooth | inversesawtooth | noise`.

```
rgbGen wave sin 0.6 0.4 0 0.25     // gentle breathe
rgbGen wave square 0.2 0.8 0 6     // hard strobe
rgbGen wave noise 0.7 0.3 0 4      // flicker (torch/neon-on-the-fritz)
```

### Recipe 1.3 — Swirling plasma / heat-haze (turbulence)
`tcMod turb <base> <amp> <phase> <freq>` churns the UVs — the classic Q3 lava/plasma look.

```
{
    map textures/strafe64/plasma.tga
    blendFunc add
    tcMod turb 0 0.15 0 0.4
    tcMod scroll 0.1 0
}
```

### Recipe 1.4 — Rotating halo / scanning radar / vortex
`tcMod rotate <deg/sec>`. Great on round emblems, portal rings, charge-up FX.

```
tcMod rotate 30          // CW
tcMod rotate -90         // fast CCW
```

### Recipe 1.5 — Environment-map chrome / shiny metal (cheap, no cubemap)
`tcGen environment` projects a reflection from view angle. Pair with a blurry "chrome"
texture for a fake reflective surface — works on the vanilla renderer too.

```
{
    map textures/env/chrome.tga
    tcGen environment
    blendFunc add
    rgbGen identity
}
```

### Recipe 1.6 — Smooth sprite animation (animMap + interpolation)
`animMap <fps> <f1> <f2> ...` flips frames. The pro trick: run **two** animMap stages
offset by one frame and cross-fade with complementary sawtooth waves to interpolate —
turns an 8-frame loop into buttery 16fps. (Source: PlayMorePromode shader tricks.)

```
{
    animMap 16 fx/01.tga fx/02.tga ... fx/08.tga
    rgbGen wave inversesawtooth 0 1 0 16
    blendFunc add
}
{
    animMap 16 fx/02.tga fx/03.tga ... fx/01.tga
    rgbGen wave sawtooth 0 1 0 16
    blendFunc add
}
```

> **Desync trick:** give stacked animations **prime-number** periods so they never
> visibly re-sync (e.g. freq `0.183…` instead of a round `0.2`). Two loops of period
> d1,d2 only realign after d1·d2 seconds.

### Recipe 1.7 — Deforming geometry (`deformVertexes`)
Moves the actual vertices on the GPU. Exact syntax:

```
deformVertexes wave   <div> <func> <base> <amp> <phase> <freq>   // flag/water ripple
deformVertexes normal <div> <func> <base> <amp> <freq>           // wobble normals (heat, jelly)
deformVertexes bulge  <bulgeWidth> <bulgeHeight> <bulgeSpeed>     // pulsing tube/pillar
deformVertexes move   <x> <y> <z> <func> <base> <amp> <phase> <freq>  // whole surface oscillates
deformVertexes autosprite     // quad always faces camera (particles, orbs, coronas)
deformVertexes autosprite2    // faces camera but keeps one axis (laser beams, plasma bolts)
```

`autosprite2` is gold for STRAFE 64 trail/beam FX — a flat strip that always presents its
face to the camera. `deformVertexes wave` on a floor panel = rolling energy floor.

### Recipe 1.8 — Liquid / glass refraction overlay
Two-layer: a `tcGen environment` reflection over a turbulent diffuse, with `tcMod turb`
faking refraction wobble. Add `sort` if it should draw after opaque geometry.

### Recipe 1.9 — Audio-reactive surfaces (STRAFE 64 special)
We already pipe music band envelopes into shader genfuncs via the `au_bass / au_mid /
au_high / au_level` cvars (see `[[strafe64-audio-reactive-shaders]]`). Any `wave`/`turb`
parameter can be driven so the world pulses on the beat. Treat it as a custom waveform
source feeding `rgbGen`/`deformVertexes`. This is our signature trick — lean into it.

### Recipe 1.10 — Sky & cloud layers
`skyparms <farbox> <cloudheight> <nearbox>`. Cloud layers are just scrolling/rotating
shader stages on the sky dome; `cloudheight` sets dome curvature. Our identity sky is a
Bryce-style dusk built this way (`[[strafe64-bryce-sky]]`).

```
skyparms - 512 -                 // no boxes, procedural cloud height 512
{ map sky/clouds.tga
  tcMod scroll 0.02 0.01
  blendFunc add }
```

### Recipe 1.11 — Distance / volume fog
- **Global volumetric fog:** a brush with `surfaceparm fog` + `fogparms <r> <g> <b> <distanceToOpaque>`. Everything inside fades to the fog color.
- **Atmospheric distance fog:** set in the worldspawn / `r_` cvars for whole-level haze.

```
fogparms 0.1 0.15 0.3 600      // cool blue murk, opaque at 600 units
```

### Recipe 1.12 — Portals & mirrors
Make a surface a portal/mirror: shader gets `sort portal` and the map needs a
`misc_portal_surface` entity within 64 units, plus a `misc_portal_camera` for non-mirror
portals. Renders the scene from another viewpoint into the surface — security cameras,
infinite-mirror rooms, "window into another arena."

### Recipe 1.13 — Coronas & light flares
Billboarded glow that's identical from every angle (`deformVertexes autosprite` + additive
glow texture). Place over every light source for that bloom-y "the bulb is bright" pop.
On GL2 the real bloom pass (below) amplifies these for free.

---

## Layer 2 — GL2 / rend2 renderer tricks (already in our renderer)

Our renderer is the modern OpenGL2 path (`[[strafe64-showcase-launcher]]`,
`[[strafe64-sky-and-bloom-render-fixes]]`). It exposes rend2-class features. Cvar defaults
from the iortcw rend2 readme.

### Recipe 2.1 — Normal + specular mapping (PBR-lite materials)
Add height/normal/spec stages to any material. Suffix convention: `_n` normal, `_s`
specular. Exact stage syntax:

```
textures/strafe64/hull_metal
{
    q3map_normalImage textures/strafe64/hull_metal_n.tga
    {
        stage diffuseMap
        map textures/strafe64/hull_metal.tga
    }
    {
        stage normalMap
        map textures/strafe64/hull_metal_n.tga
        normalScale 1.4 1.4              // crank relief
    }
    {
        stage specularMap
        map textures/strafe64/hull_metal_s.tga
        specularExponent 256            // shininess (default 256)
        specularReflectance 0.04        // metalness (default 0.04; raise for metal)
    }
}
```

Cvars to toggle/tune live: `r_normalMapping 1`, `r_specularMapping 1`,
`r_baseSpecular 0.04`, `r_baseGloss 0.3`.

### Recipe 2.2 — Parallax / relief mapping (fake 3D depth in flat walls)
`r_parallaxMapping` — `0` off, `1` parallax-occlusion, `2` relief (best, costliest). Use
`stage normalParallaxMap` and set `parallaxDepth <v>` (default `0.05`). Turns a flat
panel into deep recessed greebles. Perfect for our dev/Source-look hull textures
(`[[strafe64-source-dev-textures]]`).

### Recipe 2.3 — Real sun + cascaded shadow maps
The `q3gl2_sun` shader keyword on a sky shader gives a directional sun that casts dynamic
cascaded shadows:

```
q3gl2_sun <r> <g> <b> <intensity> <degrees> <elevation> <ambientScale>
// e.g.
q3gl2_sun 1.0 0.95 0.8 80 215 35 0.4
```

Cvars: `r_sunShadows 1`, `r_shadowMapSize 1024` (bump to 2048/4096 for crisp edges),
`r_shadowFilter 1` (soft PCF edges). This is the single biggest "wow" upgrade for an
outdoor STRAFE 64 arena — moving geometry and players throw real shadows.

### Recipe 2.4 — HDR + tone mapping + auto-exposure
`r_hdr 1`, `r_toneMap 1`, `r_autoExposure 1`. The world renders in HDR and tone-maps to
screen; auto-exposure makes the eye "adapt" when you exit a dark tunnel into a bright
arena. Tune the curve per-shader with `q3gl2_tonemap <min> <avg> <max> <autoMin>
<autoMax>`.

> **Gotcha (`[[strafe64-showcase-launcher]]`):** on flat untextured dev surfaces, keep
> exposure LOW (~0.85) or everything blooms to white.

### Recipe 2.5 — Bloom / glow
Bright (HDR > 1.0) pixels bleed light. We already have a custom RB_Bloom pass
(`[[strafe64-sky-and-bloom-render-fixes]]` — watch the Y-flip parity bug). Drive it from
additive shader stages and `q3gl2_sun` intensity; the brighter the source, the bigger the
halo. This is what makes our trails and neon read as *emissive*.

### Recipe 2.6 — SSAO (contact shadows / ambient occlusion)
`r_ssao 1` (default off). Darkens crevices and corners where surfaces meet — cheap depth
and groundedness, makes greebled hulls and pillar bases sit in the world instead of
floating.

### Recipe 2.7 — Cubemap reflections (real, not fake-env)
`r_cubeMapping 1`. Place cubemap probes; reflective materials sample the baked cube for
true local reflections (chrome floors, glossy water). Heavier than Recipe 1.5 but
correct. Combine with high `specularReflectance` for mirror metal.

### Recipe 2.8 — MSAA + texture upsampling
`r_ext_framebuffer_multisample 4` (or 8/16) for smooth edges without the perf hit of
supersampling. Texture upsampling sharpens low-res idTech3 art.

### Recipe 2.9 — Full-screen color grade / post FX
The post-process chain (`r_postProcess 1`) is where to hang custom grades — desaturate +
blue-shift for bullet-time, vignette, chromatic aberration on hit. Ties directly into our
time-bind/bullet-time system (`[[strafe64-sword-slowmo-direction]]`): slowmo could push a
cold high-contrast grade, swings could flash warm.

---

## Layer 3 — q3map2 compile-time light tricks (baked, in strafegen output)

These bake into the BSP lightmap at compile. Relevant to `tools/strafegen` map emission.

### Recipe 3.1 — Radiosity bounce (soft indirect GI)
Compile with `-light -bounce 8 -bouncescale 1.0`. Light bounces N times off surfaces,
baking soft color-bled indirect lighting into lightmaps. `-bouncescale` multiplies the
indirect intensity. Even `-bounce 2` transforms a flat-lit room.

### Recipe 3.2 — Sky / sun light from the shader
On the sky shader:
```
q3map_sun <r> <g> <b> <intensity> <degrees> <elevation>   // baked directional sun
q3map_skylight <brightness> <iterations>                  // soft hemispheric sky fill (50–200 good)
q3map_surfacelight <value>                                 // make any surface emit light
```
`q3map_skylight` replaces the slow `q3map_surfacelight`+`q3map_lightSubdivide` combo on
skies — faster, more uniform ambient.

### Recipe 3.3 — Emissive surface lights
Any shader with `q3map_surfacelight 2000` becomes a light source at compile — glowing
strips, lava, screens that actually illuminate the room. Pair with the additive *visual*
stage so it looks lit AND lights neighbors.

### Recipe 3.4 — Deluxemapping (per-pixel direction for baked light)
Compile with `-deluxe`. Stores a per-texel light *direction* alongside the lightmap, so
baked lighting still responds to normal maps (Recipe 2.1) at runtime — your normal-mapped
walls get correct highlight direction from the baked sun. Bridges Layer 3 (baked) and
Layer 2 (dynamic materials).

### Recipe 3.5 — `_minlight` / ambient floor
`_minlight <n>` in worldspawn guarantees no surface is fully black. Crude but kills ugly
pure-black shadows. Better: a low `q3map_skylight` for directional ambient instead.

### Recipe 3.6 — Lightstyles (animated baked light)
Switchable/animated light styles (flicker, pulse, "candle") baked as multiple lightmaps
the engine blends at runtime — classic flickering-fluorescent horror lighting with zero
runtime light cost. Great for our hot-floor / hazard sections.

### Recipe 3.7 — Dynamic lights (`dlight`)
Runtime point lights from rockets, muzzle flashes, the sword trail, pickups. Cheap but
capped — **any one surface takes at most ~4–8 dlights**; beyond that they're dropped.
Budget them. Our weapon/trail FX should attach dlights for that travelling-glow read.

---

## STRAFE 64 quick-win checklist

Ranked by wow-per-effort for our build:

1. **`r_sunShadows 1` + `q3gl2_sun`** on outdoor arenas — real moving shadows. (Recipe 2.3)
2. **Bloom on every additive trail/neon** — already wired; make sources HDR-bright. (Recipe 2.5)
3. **Audio-reactive `deformVertexes`/`rgbGen`** — our signature; warp the arena on the beat. (Recipe 1.9)
4. **Parallax hull textures** (`r_parallaxMapping 2`) on the dev-look walls. (Recipe 2.2)
5. **Bullet-time color grade** in post — cold/desaturated during slowmo. (Recipe 2.9)
6. **`autosprite2` beams** for laser/plasma/trail strips that always face camera. (Recipe 1.7)
7. **`-bounce 8 -deluxe`** in strafegen's compile — softer, normal-aware baked light. (Recipes 3.1, 3.4)
8. **SSAO** for grounded greebles. (Recipe 2.6)

---

## Implemented in strafegen

These recipes are shipped by [`tools/strafegen/strafegen_gfx.py`](../tools/strafegen/strafegen_gfx.py),
a graphics module strafegen applies to every packed map (toggle with `--no-gfx`):

- **q3gl2_sun** (Recipe 2.3) — injected into the sky shader; direction matches the painted dusk sun. Cascaded shadows are armed (`r_sunShadows` defaults on).
- **`textures/strafe64/hull`** — PBR-lite material (Recipes 2.1/2.2: diffuse + normal + specular + `normalParallaxMap`), with procedurally-generated `hull{,_n,_s}.tga`.
- **`textures/strafe64/chrome`** — `tcGen environment` fake reflection (Recipe 1.5).
- **`textures/strafe64/plasma`** — turbulent audio-reactive energy panel (Recipes 1.3/1.9).
- **`strafe64/beam`** — `deformVertexes autosprite2` camera-facing strip (Recipe 1.7).
- Each `--pk3` also drops a `<map>_gfx.cfg` enabling the off-by-default toggles (`r_parallaxMapping 2`, `r_ssao 1`) plus a fixed `r_cameraExposure 0.85` (auto-exposure blows out flat dev textures).

The sun is active on every map. The materials are **routed into generators**:

- **Arena** (vectorgun/sword bowl): center tower tiers + cover pillars → `hull`; tower crown / crater pedestal → `chrome`; crater walls → `hull` (red danger floor kept as a gameplay cue).
- **Killbox**: spire pyramid steps + wall-jump/cover column shafts → `hull`, keeping the neon rim caps as the wall-jump cue.

Routing is a per-brush `tex=` swap (hull/chrome are opaque & solid → collision unchanged).
`plasma`/`beam` are `nonsolid` additive decals and remain unplaced components (they need a
non-solid decorative brush, which the direct-BSP writer doesn't emit yet).

> **nolightmap note:** on the vertex-lit world the hull's PBR relief reads subtly — faces
> away from the low sun get only blue dusk ambient. Mitigated with a brighter steel albedo +
> `r_forceSunAmbientScale 0.7` in the cfg; real relief pop would need a lightmap bake.

> Layer-3 (baked q3map2 lighting) recipes are **not** implemented: strafegen writes
> vertex-lit BSP directly with no compile pass, so there's no lightmap to bake into.

## Sources

- [Quake III Arena Shader Manual (icculus / GtkRadiant)](https://icculus.org/gtkradiant/documentation/Q3AShader_Manual/) — general & stage keywords, `deformVertexes`, `skyparms`, `fogparms`, `sort`, `cull`.
- [Quake III Arena Shader Manual PDF (Stanford mirror)](https://graphics.stanford.edu/courses/cs448-00-spring/q3ashader_manual.pdf)
- [PlayMorePromode — Quake III Arena shader tricks](https://myt.playmorepromode.com/blog/q3_shader_tricks/) — interpolated animMap, prime-number desync, multi-stage blend math.
- [iortcw rend2 readme](https://github.com/iortcw/iortcw/blob/master/rend2-readme.md) — `normalMap`/`specularMap`/`normalParallaxMap`, `q3gl2_sun`, `q3gl2_tonemap`, full cvar list & defaults.
- [Rend2 cvar list (JKHub)](https://jkhub.org/tutorials/rend2/rend2-cvar-list-r212/) and [rend2 shaders tutorial](https://jkhub.org/tutorials/rend2/shaders-r97/)
- [ModDB — ioquake3's new OpenGL2 renderer](https://www.moddb.com/engines/ioquake3/news/were-updating-ioquake3s-graphics-renderer) — HDR, CSM, SSAO, parallax, cubemaps feature overview.
- [Q3Map2/Light (Wikibooks)](https://en.wikibooks.org/wiki/Q3Map2/Light) and [OpenArena Mapping manual/Lighting](https://openarena.fandom.com/wiki/Mapping_manual/Lighting) — `-bounce`, `-bouncescale`, `q3map_skylight`, `q3map_surfacelight`, deluxemaps.
- [Q3Map2 Shader Manual — Lightstyles](http://q3map2.robotrenegade.com/docs/shader_manual/lightstyles.html)
- [OpenArena — Fake indirect lighting](https://openarena.fandom.com/wiki/Fake_indirect_lighting) — `_minlight`, radiosity faking.
