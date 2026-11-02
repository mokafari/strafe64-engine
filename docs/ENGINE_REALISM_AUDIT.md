# Engine audit: what blocks the hyper-real look (2026-11-02)

Question: do we need engine changes to load higher-quality textures and reach the look in the concept art (`docs/COLOUR_SCIENCE.md` section 0)?

**Short answer.** Texture *loading* is not the bottleneck: the engine already loads 2048 px albedo and normal-height maps, and we already ship them. The gaps are (1) a handful of defaults and format choices that quietly cost quality, (2) the lighting and reflection model (no reflections beyond cubemaps, fixed 128 px lightmaps, no volumetrics), and (3) content: the maps are flat, neutral-less and warm-light-poor. Most of the look is fixable without engine work; four small engine changes and two medium features would close most of the rest.

Everything below is from reading `engine/code/renderergl2/` and running the engine (Apple M3 Pro, GL 4.1 over Metal, GLSL 4.10), except where marked *unverified*.

## 1. Textures

| Question | Finding | Where |
|---|---|---|
| Formats loaded | PNG, TGA, JPG, PCX, BMP, PVR, plus DDS (BC1-BC7 incl. BC5, BC6H, DX10 header) | `tr_image.c:2380`, `tr_image_dds.c` |
| Max size | `GL_MAX_TEXTURE_SIZE` (no engine cap; larger images are halved only above that) | `tr_image.c:1626` |
| What we ship | 2048 albedo `.jpg` + 2048 `_nh.png` (normal RGB + height alpha) from `build_materials.py` | `tools/strafegen/build_materials.py` |
| Sidecar maps | `<name>_nh` / `_n` (normal), `_s` (specular) are picked up automatically when present | `tr_shader.c:2269-2345` |
| Parallax | needs `_nh` and `r_parallaxMapping 1` (default **0**; earlier notes: reads lumpy on noisy height) | `tr_init.c:1345` |
| Compression | `r_ext_compressed_textures` default **0**. DDS is only tried when it is **1**, and then ordinary 8-bit images are also **recompressed by the GL driver** (DXT/BPTC) at upload | `tr_image.c:2432`, `tr_image.c:1743-1775` |
| Anisotropic filtering | `r_ext_texture_filter_anisotropic` default **0** (off), max 16 | `tr_init.c:1289` |
| Colour space | no sRGB texture or framebuffer path; gamma is approximated in shaders (`r_pbr` squares/sqrts) | grep: no `r_srgb` |
| Lightmaps | fixed **128 x 128** per page (`DEFAULT_LIGHTMAP_SIZE`); BSP lump cap 8 MB (`MAX_MAP_LIGHTING`); external HDR lightmaps (`maps/<name>/lm_NNNN.hdr`) must also be 128 x 128 float | `tr_bsp.c:201,347`, `qfiles.h:332` |

Consequences:
- **Higher-res textures need no engine change.** 4096 would load too; the limit is memory: a 2048 RGBA8 texture is 16 MB (21 MB with mips), and both pk3 size and load time grow with it.
- **Anisotropic filtering being off is a real, cheap quality loss.** I tested it in-engine: with 16x enabled, floor veining and tile seams stay crisp to the horizon at grazing eye height, where they were smeared before. (Not a controlled A/B: the camera pose differed slightly between the two shots.) The showcase launcher turns it on; the default build does not.
- **BC7/BC5 DDS would cut VRAM about 4x and load faster, but the current switch can't be used as is,** because enabling it also lossy-recompresses everything else. It needs a small engine change (section 4).
- **Lightmap resolution is the hard ceiling for baked detail.** One 128 px page covers a limited area (our bake uses 8 unit sample size), so large plazas get soft shadow edges and no fine contact shadows. The 8 MB lump cap bounds map size and bake quality together.

## 2. Material and lighting features in the renderer

Present: lightall shader with normal, specular, parallax, deluxemapping (we bake `-deluxe`), cubemap IBL with parallax-corrected box, PBR path (`r_pbr`), HDR + auto-exposure + filmic tonemap, bloom, SSAO, cascaded sun shadows, sun rays, DoF, our grade pass.

Absent or weak (checked by grep through `renderergl2/` and `glsl/`):
- **No screen-space reflections, no planar reflection pass.** The only mirror is the original Q3 portal/mirror surface mechanism (`tr_main.c` `R_MirrorViewBySurface`), which re-renders the scene through the mirror plane, i.e. roughly doubles the visible geometry cost. Forum sources agree on that cost (secondhand, 2001-2011).
- **Cubemaps** (`r_cubeMapping`, default 0): probes come from `maps/<map>/env.json`, `misc_cubemap` entities, or, as a fallback, our `info_player_deathmatch` spawn points (`tr_bsp.c:2975-2995`). Our maps place none, so enabling it silently used spawn points as probes. This is a likely contributor to the blown-out floor when I tried `r_pbr 1` + `r_cubeMapping 1` earlier (*unverified*; the PBR path also assumes sRGB-ish inputs that our lightmaps/vertex lighting do not provide).
- **No shadows from point lights.** `r_dlightMode >= 2` only gives per-pixel dynamic lights for entities; baked warm strips are lightmap-only, which is fine.
- **No volumetric fog or light shafts** beyond screen-space sun rays and Q3 fog volumes.
- **No GI beyond the q3map2 bounce bake.** No light probes.
- **Tonemap + grade order**: tonemap first (Hable curve with auto-exposure), grade after on clamped LDR (see `COLOUR_SCIENCE.md`). No LUT, no ACES/AgX-style tonemapper choice.
- **No temporal AA**; FXAA in our grade pass plus 4x MSAA.

Platform: macOS gives OpenGL 4.1 (GLSL 4.10), which has no compute shaders, no SSBOs, and no GL 4.3+ features. That rules out compute-based techniques (froxel volumetrics, tiled lighting) in the current renderer. Our earlier measured costs on this machine (`strafe64-perf-measured-costs`): shadows about 3 ms, bloom about 3 ms, the shared post-stack about 5-7 ms. Any new pass needs to fit that budget.

## 3. Content gaps (not engine work)

Measured against the concept art (`COLOUR_SCIENCE.md` section 0), most of the distance is in what the maps contain:
- Floor and wall albedo is blue-tinted and bright; the art uses neutral warm-grey stone (chroma 0.02 vs our 0.055).
- Lighting is a hard sun plus a few large point-light pools; the art has dozens of small recessed amber strips along steps, floors and column bases, plus a few cyan seams.
- Geometry is flat slabs. The art has joints, chisel marks, cracked and uneven stone, steps with lit nosings, large monolithic forms and haze at the horizon.
- No water or wet stone; reflections carry a lot of the "hyper-real" read in all four images.

## 4. Recommendations

Ordered by value for effort. "Engine" = needs C/GLSL change in `renderergl2`.

**A. Do now, no engine work**
1. Turn on anisotropic filtering by default (`r_ext_texture_filter_anisotropic 1`, `r_ext_max_anisotropy 16`) in the shipped config and the launchers.
2. Retint materials to neutral warm-grey and re-light (see `COLOUR_SCIENCE.md` gap list 1-4); bake many small warm strip lights instead of a few big pools.
3. Use `_nh` normals on selected surfaces only (grating, carved relief) and keep `r_parallaxMapping` off elsewhere.
4. Add `misc_cubemap` probes (a few per map) and test `r_cubeMapping 1` with a neutral floor, before touching `r_pbr`.

**B. Small engine changes (hours each)**
1. Default anisotropic filtering on (code default, not just config).
2. Split DDS loading from GL recompression: load DDS when present, never recompress other images. Then add a build step to emit BC7 (albedo), BC5 (normals), BC4 (height/roughness) DDS next to the sources. Gains VRAM (about 4x), load time, and room for 4K hero textures.
3. A tonemap choice (keep Hable, add ACES-style or a neutral curve) and move split-toning/lift/shoulder before the tonemap so highlights are recoverable (the main limitation we hit in rounds 2-6).
4. A color LUT stage in the grade pass, so looks can be authored in an external grading tool and compared with the metrics tool.
5. Make lightmap size configurable (or a second, larger lightmap set for the hero maps) and test an HDR bake path (q3map2 HDR output + `lm_NNNN.hdr`): removes 8-bit banding in shadow gradients and lets deep shade keep detail. *Unverified*: how our netradiant-custom q3map2 build writes HDR lightmaps and at what page size; first step is to check that.

**C. Medium features (days each)**
1. **Planar reflection for still water / wet stone.** Two routes: try the existing Q3 mirror surface first (zero engine code, doubles geometry for the visible area), then, if too slow, a dedicated half-resolution reflection pass for horizontal planes with fresnel and blur. This is the single biggest visual gain toward the concept images (reflected moon, obelisks, colossi).
2. **Screen-space reflections** for glossy floors that are not planes. A forward renderer can do it from a depth prepass (we already allocate `hdrDepthImage` for SSAO/DoF); cheap implementations blur by roughness and fade at screen edges; half-res tracing is standard to save cost (sources: engine docs and one open-source implementation, no performance numbers for our hardware). Without a normal buffer it needs normals reconstructed from depth, which is noisy on veined marble.
3. **Light shafts / volumetric moon light** using the existing cascaded sun shadow map (ray-march in a half-res post pass), replacing the screen-space sun rays. GL 4.1 is enough.
4. **Shadowed point lights** for a handful of hero lights (katana, bolts) following XreaL's approach.

**D. Large / strategic**
- Port the relevant parts of the OpenJK "Rend2" line (SomaZ and others): HDR lightmaps, refraction, dynamic light shadows (the sources I found mention these but not ORM packing, cubemaps or GI, so each would need a code read first).
- Moving off macOS OpenGL 4.1 (Vulkan via MoltenVK, or ANGLE-on-Metal) would unlock compute-based volumetrics, tiled lighting and GI, but is a rewrite-scale project. Not recommended until A-C are done.

## 5. Suggested order

1. A1 + A2 + A3 now (config and content) and re-measure with `tools/colour/metrics.py` against the concept-art targets.
2. B1, B2, B3 (small, mostly invisible to players, unlock quality and headroom).
3. C1 (planar reflection prototype with the Q3 mirror shader on the nocturne pools) to find out how far reflections alone close the gap, before building anything custom.
4. Revisit C2/C3 and the lightmap/HDR work (B5) based on what is still missing.

Caveats: the concept art is AI-generated, so it shows intent, not what real time can reach; the aniso check was visual and not controlled; performance numbers are from an M3 Pro and earlier sessions; items marked *unverified* need a code or experiment pass before committing to them.

## 6. Prototype result: planar mirror pool (2026-11-02)

Built a 1120 x 2000 shallow reflecting pool down the nocturne's central axis using the stock Q3 mirror mechanism, no engine changes (`tools/strafegen/strafegen_nocturne.py`, shot: `docs/screenshots/reflection/mirror-pool-v1.jpg`).

**It works** and reflects the portal frame, its cyan lintel, the pillars and the stars. Recipe, learned the hard way:
1. A `misc_portal_surface` entity with no target, **in open air** (not inside a brush) and **within 64 u of the plane**. Its position also supplies the mirror view's PVS leaf.
2. A shader with the `portal` keyword **and `depthWrite` on its blend stage.** Without `depthWrite` the pool is a "hole": the portal surface leaves the depth buffer cleared, so the main view's own sky box (the cloud sea) is drawn over the mirror afterwards (`docs/screenshots/reflection/mirror-pool-hole-bug.jpg`). My first attempt had this bug, and a bad guess about PVS and clip planes cost an hour. The renderer's mirror maths (plane, clip, mirrored camera) was correct all along; I verified it with debug prints (removed).
3. The tint stage is `blendFunc GL_ONE_MINUS_SRC_ALPHA GL_SRC_ALPHA` with a constant alpha (0.78 here), so result = mirror * 0.78 + dark tint * 0.22.
4. `strafegen_bake.py` now accepts `course.extra_shader` for map-specific shader text.

**Cost: not measured.** The mirror pass generates 147 draw surfaces here (plus the main view's), so it roughly doubles the geometry for the visible area, as sources suggested. Needs a GPU-timer run (`r_gpuTimers`, `tools/gpubench`) before shipping it.

**Limits to know:** one mirror plane per view (no recursion), mirrors at one height per entity, no roughness blur or fresnel yet (flat blend), no ripples. Fine for still water; wet-stone or rough surfaces need SSR or a blur pass.

## 7. Reservoir textures + water v1 (2026-11-02)

**Textures.** The Nocturne-Reservoir pack (`/Volumes/LaCie/STRAFE64/Nocturne-Reservoir/`, 12 x 2048 PBR sets) is integrated into the nocturne via map-specific `nrx_*` shaders (`strafegen_nocturne.py`): pavers floor, ashlar terraces/stairs, limestone columns, obelisk marble portals, basalt inlays, pool coping, amber lamp strips (emissive + baked `q3map_surfacelight`). Shaders are lightmapped with the pack's `_nh` normal maps; tile sizes are set in the generator (`tcMod scale = 64 / tile`, names under `textures/strafe64/m/` get 64u-per-repeat UVs). The bake mounts the pack through `course.extra_pk3`; the map ships no textures. Runtime: `assets/openarena/baseoa/nocturne_reservoir.pk3` is a **symlink** to the LaCie copy (the internal disk is nearly full); if the drive is unmounted the map falls back to missing textures.
First-pass numbers against the concept-art targets: shadow/highlight hue gap 172-178 deg (target >= 100), ground/sky luma ratio 1.5-3.4 (target 0.9-2.6), warm pixels 2-4 % (target >= 2.5). Chroma was still about 2x: lowering `r_gradeSaturation` to about 0.6 (+ split 0.5, lift 0.045, exposure 0.2) gives the neutral, restrained look in `docs/screenshots/reservoir/graded-low-saturation.jpg`.

**Water v1 (no engine change).**
- Body: the mirror stage now uses `alphaGen portal 1500`, so the pool is dark deep water up close and a full mirror at grazing/long range (cheap fresnel-like behaviour); `depthWrite` stays (see section 6).
- Motion: two counter-scrolling, differently scaled/rotated additive layers of a moon-glint texture (`nrx_water_glint.png`, generated at bake time from random-phase tileable waves; the pack's own water height map is periodic and showed as a dot grid). Verified moving: mean frame-to-frame difference 8.6 over the pool vs about 1.3 over static sky/floor.
- Limits: the glints move but the *reflection itself does not distort*, there is no fresnel by angle (distance proxy only), no refraction/depth tint, no shoreline foam, no interaction (player or projectile ripples).

**Water v2 (needs engine work, scoped, not started).** In the mirror pass the reflected scene is already in the colour buffer when the water surface is drawn. A small new stage type for portal surfaces would: copy the colour buffer to a scratch texture, then draw the water with a dedicated GLSL program that (1) offsets the lookup by an animated normal map (distorted reflection), (2) blends by true fresnel from view angle, (3) adds a moon specular lobe from the same normals, (4) tints by fake depth. About 100 lines C (copy + program + shader keyword) and 60 lines GLSL, plus `r_water*` cvars. Gotchas to expect: copying the HDR FBO each frame costs bandwidth on this GPU; MSAA resolve; the pass must run after the mirror view and before sky. Ripples from gameplay (footsteps, bolts) would need cgame to feed impact positions to the shader as uniforms.

**Process notes.** The internal disk had about 300 MB free; bakes with the 88 MB pack run with `TMPDIR=/Volumes/LaCie/STRAFE64/tmp`. `/var/folders/.../T` holds 273 git-style `tmp.*` stores (15 GB) that I did not create and did not delete. The very first screenshot returned by `engine_camera` after a teleport often shows a stale/odd viewpoint; use the follow-up `engine_screenshot`.

## Sources
- [Rend 2, a more modern renderer (ModDB dev diary)](https://www.moddb.com/news/dev-diary-rend2)
- [OpenJK rend2 PR #1179](https://github.com/JACoders/OpenJK/pull/1179/files)
- [Rend2 cvar list (JKHub)](https://jkhub.org/tutorials/rend2/rend2-cvar-list-r212/)
- [Q3Map2 light reference (Wikibooks)](https://en.wikibooks.org/wiki/Q3Map2/Light)
- [q3map2 `-lightmapsize` discussion (GtkRadiant list)](https://icculus.org/pipermail/gtkradiant/2011-February/011617.html)
- [Mirrors and portals (Khronos forum)](https://community.khronos.org/t/mirrors-and-portals/42936)
- [Texture compression formats, BC4/BC5/BC7](https://docs.bestway.com.ua/textures-and-materials/physically-based-rendering/texture-compression-formats-in-gem-rts)
- [Compressonator codecs](https://compressonator.readthedocs.io/en/latest/developer_sdk/codecs/)
- [Real-time local reflections (gamedev.net)](https://gamedev.net/blogs/1323/entry-2254101-real-time-local-reflections/)
- [Flax screen-space reflections](https://docs.flaxengine.com/manual/graphics/post-effects/screen-space-reflections.html)
