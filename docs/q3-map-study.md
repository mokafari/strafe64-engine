# Q3/OpenArena map study — tricks worth stealing for strafegen

Reverse-engineered a curated set of acclaimed, **self-contained** (own-textures) Quake3 /
OpenArena maps to mine level-design techniques for `strafegen`. Pipeline:

```
download → deploy to assets/openarena/baseoa → run in engine (mode=client)
         → tools/strafegen/bsp_study.py → digest JSON in maps_study/digests/
```

Two levels of decompile:
- **`bsp_study.py`** (fast, zero-dep) pulls the two *instructive* BSP lumps (entities +
  shaders) directly — entity placement, light palette, jumppad arcs, shader vocabulary. This
  is the high-signal first pass for design study.
- **Full `tools/strafegen/q3map2 -convert -format map`** (the locally-built macOS q3map2,
  see `[[strafe64-q3map2-macos-build]]`) recovers editable brushwork — e.g. lun3dm5 →
  **12,852 brushes** with texture alignment. Worth it when you need to open geometry in a
  radiant-style editor, and because the decompiled worldspawn preserves the original
  **`_q3map2_cmdline`** — i.e. the author's exact compile recipe (see trick #7).

## The corpus

| Cohort | Map | Author | Why it's here |
|---|---|---|---|
| Aesthetic | **lun3dm5** *Shoot Your Eye Out* | Lunaran | Sun-dome faux-GI; pale concrete floating in fog |
| Aesthetic | **lun3dm2** *Black Sun* | Lunaran | 216 hand-placed blue lights; no sky, fog-as-void |
| Aesthetic | **lun3dm3** *Useless Loonies* | Lunaran | 296 lights, green palette, 15 misc_model props, phobos sky |
| Flow | **dm6ish** (q3dm6 remake) | Maik Merten | Teleporter-stitched vertical loop (Camping Grounds DNA) |
| Flow | **oa_dm5** *The Cistern* | OA team | misc_portal_surface security-cam mirrors + func_door movers |
| Flow | **oa_dm1** *Two Deaths* | OA team | 23 target_position nodes — dense item/route graph |
| Defrag | **acc_fuzzle** | DeFRaG | func_button start/stop timer; target_push trick-launches |
| Defrag | **de4th_run2** | DeFRaG | 104 misc_model world (props ARE the level), purple light grade |

Verified rendering in-engine: lun3dm5 loads with all textures resolved (screenshot in PR).
q3df.org / lvlworld are Cloudflare/JS-gated against scripted download — Lunaran's site and
GitHub mirrors (Yann39 defrag repo) are the scriptable sources. Flow cohort filled from
OA's own self-contained maps to avoid the gate.

## Tricks, ranked by payoff for our generator

### 1. Sun-dome lighting instead of point lights  ← biggest aesthetic win
**lun3dm5 has ZERO `light` entities.** Its entire look comes from a dome of many
`q3map_sun`/`q3map_sunExt` directives in the worldspawn shader + a bright sky, giving soft
faux-global-illumination shadows on bare concrete. lun3dm2/lun3dm3, by contrast, brute-force
**216 / 296 hand-placed coloured point lights** — gorgeous but unscriptable at our scale.
- **Take:** the sun-dome is the *generator-friendly* path — a handful of shader directives
  scales to any geometry, where hand-placed lights don't. `strafegen_gfx.py` already emits a
  `q3gl2_sun`; extend it to a **multi-sun dome** (3–5 suns: 1 strong key + dim fill from
  other compass directions) for the lun3dm5 soft-shadow look on our dev-concrete surfaces.

### 2. Coloured light *palette* discipline
Each Lunaran map commits to **one hue family**: lun3dm2 = blues (`0.61 0.80 1.00` dominant,
12×), lun3dm3 = greens (`0.0 1.0 0.0`), de4th_run2 = purple/magenta (`0.65 0.35 1.00`, 56×).
A single dominant `_color` with a couple of accents reads as deliberate art direction.
- **Take:** `strafegen_palettes.py` should pick **one light-hue family per seed** and drive
  both the sun-dome `_color` and the accent glow shaders from it — not per-light random.
  This is the cheapest "looks designed, not generated" lever we have.

### 3. Aim-a-point jumppads (`trigger_push` → `info_notnull`)
Q3 pads don't store a velocity — `trigger_push` targets an `info_notnull`/`target_position`
and the engine **solves the launch arc to land the player at that point** (gravity-correct).
lun3dm5 has 9 of these. This is strictly better than a fixed push vector: the arc auto-adapts
to gravity and the pad always delivers you to the intended ledge.
- **Take:** check whether `strafegen` pads emit fixed `target_push`/velocity or aim-a-point.
  If fixed, switch to the `trigger_push`→`info_notnull` form so pad arcs stay correct under
  our variable-gravity / slowmo time scaling. (Entity sample in `maps_study/`.)

### 4. Props ARE the level (`misc_model`)
de4th_run2 = **104 misc_model**, lun3dm3 = 15. Detail geometry is baked-in static models, not
brushwork — cheap silhouette variety, and the brush BSP stays simple/fast.
- **Take:** `strafegen_geom.py` is pure brushwork. A `misc_model` prop-scatter pass (rails,
  pipes, debris from a small md3 kit) would add the hand-built silhouette our maps lack,
  without complicating the brush/vis compile. Pairs with the existing trail-rail aesthetic.

### 5. Teleporter-stitched loops (flow)
dm6ish uses `trigger_teleport`→`target_teleporter` to **fold a vertical map into one
continuous fast loop** (the q3dm6 trick). oa_dm1 exposes a 23-node `target_position` route
graph — the layout is a *graph*, designed for sightline/route balance.
- **Take:** our killbox/arena momentum-portals already do dest-angle tricks
  (`[[strafegen-killbox-momentum-portals]]`). The lesson is **one deliberate stitch that
  closes the loop**, not scattered teleports — generate the return-stitch as a first-class
  flow element so courses loop instead of dead-ending.

### 6. Fog-as-void / sky-as-light
lun3dm2 has **no sky at all** — distance fog *is* the void, and it doubles as the light
source mood. lun3dm5's bright sky is effectively a giant area light feeding the sun-dome.
- **Take:** `strafegen_shaders.py` fog + our Bryce sky (`[[strafe64-bryce-sky]]`) already
  exist; the trick is **coupling fog colour to the sun/sky colour** so the horizon reads as
  one lit atmosphere, per-seed, instead of independent random picks.

### 7. Steal the compile recipe, not just the geometry
The full q3map2 decompile preserves the author's `_q3map2_cmdline` in worldspawn. lun3dm5's:
```
-meta -samplesize 8 ; -vis -saveprt ; -light -super 2 -bounce 3 -bouncescale 2 -fast -lomem
```
i.e. **3 bounces of radiosity at 2× bounce scale, 2× supersampled lightmaps, 8-unit samplesize**.
That airy soft-shadow concrete look is as much the *compile settings* as the geometry.
- **Take:** this is the recipe for our still-TODO q3map2 baked-lighting integration
  (`[[strafe64-q3map2-macos-build]]`). When we wire q3map2 into the strafegen pack pipeline,
  start from `-bounce 3 -bouncescale 2 -super 2` rather than guessing. Decompiling any map you
  admire gives you its exact light-compile flags for free.

## Artefacts
- `tools/strafegen/bsp_study.py` — the extractor (loose .bsp or .pk3; `--entities`, `--json`)
- `tools/strafegen/maps_study/pk3/` — the downloaded self-contained maps
- `tools/strafegen/maps_study/digests/*.json` — machine-readable digests for all 8 maps

## Implemented (this pass)
Tricks #1 + #2 landed together in the **vertex-light bake** (`strafegen_bsp.py`) — since
strafegen writes vertex-lit BSP directly (no q3map2 bake), the "sun dome" lives in the
per-face `LightModel.face_light`, not in `q3map_sun` shader lines:
- **`LightModel`** = hemispheric ambient + a **dome of suns** (1 dominant +x key + 3 low,
  sideways/backward fill suns). The fills light the vertical walls the single key missed, so
  faces turned away from the sun catch soft hue-tinted fill instead of going flat (trick #1).
  Fills are kept low-elevation on purpose — a straight-overhead fill just blows out floor
  readability at MACH, which the hemispheric sky ambient already handles.
- **`build_light_model(seed)`** picks one of 6 **hue families** (blue/green/amber/violet/
  dusk/teal) per seed and tints the sky ambient + fill suns from it (trick #2). Ordered so the
  canonical showcase seed 64 → "dusk" (identity stable) and arena seed 1337 → "teal". Gameplay
  accent colours (pads/danger/glow shaders in `strafegen_palettes.py`) are deliberately NOT
  recoloured — they must stay readable. The real-time `q3gl2_sun` is also left neutral-warm:
  the baked dome carries the hue, so tinting both would double-saturate.
- Verified: `--selftest` passes (28 maps), and in-engine A/B shows seed 1 = green floors vs
  seed 64 = warm dusk floors, accent pads yellow in both.
- **Tuning knobs:** `_HUE_FAMILIES` (saturation per family — green currently reads strong),
  `_DOME_FILL_DIRS` (fill bearings/elevation), per-sun rgb in `build_light_model`.

### q3map2 baked-lighting pipeline (trick #7) — PROVEN as a tool
`tools/strafegen/bake_map.sh <map> <zzz.pk3> <out.pk3>` takes the Radiant `.map` strafegen
emits (`--map`) and runs the locally-built q3map2 with lun3dm5's recipe
(`-bounce 3 -bouncescale 2 -super 2`, plus `-dirty` AO + `-scale` lift) → a pk3 with **12 real
lightmaps + a 2.35 MB volumetric lightgrid**, verified rendering in-engine. Key discoveries:
- Our identity shaders are `surfaceparm nolightmap` (vertex-lit), so q3map2 has nothing to bake.
  The script routes world brushes (`surf`/`wall`) to **lightmapped variants** (`surf_lm`/`wall_lm`,
  new names → zero collision with the runtime identity shaders) defined in a generated
  `zbaked.shader`.
- The sky's `q3map_sun`/`q3map_skylight` are injected compile-only (engine ignores `q3map_*`),
  mirroring the realtime `q3gl2_sun` — so the Bryce sky is unchanged but q3map2 reads the sun.
- **No `-vis` pass**: strafegen courses are open void (not sealed), so vis leaks (empty lump =
  "all visible" anyway) and `-vis` can Bus-error portalizing unsealed geometry.
- **LIMITATION (why it's a tool, not yet the default):** baking is from the `.map`, which does
  NOT carry the `_glow_tex` neon-accent routing (that happens at BSP-emit time). So baked maps
  trade the neon identity for soft radiosity mood. Resolving that (emit the glow accents into the
  `.map`, or a hybrid bake) is the remaining work before a clean `strafegen --bake` flag.

## Remaining next steps
3. Audit our jumppad emission vs aim-a-point form (trick #3).
4. `misc_model` prop-scatter pass (trick #4) for silhouette.
5. Promote `bake_map.sh` to a `strafegen --bake` flag once the glow-accent gap is closed
   (emit `_glow_tex` accents into the `.map` so baked maps keep the neon identity).
