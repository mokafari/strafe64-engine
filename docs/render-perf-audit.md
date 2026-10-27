# Render + post-process audit (GPU-timed)

Measured 2026-10-25 on the M3 Pro, arm64 Release, 2560x1440 windowed,
`strafe64_1337` at spawn (god + noclip, no bots), showcase look from
`tools/gpubench/bench.cfg` (HDR + forced tonemap + autoexposure, bloom 0.18,
DoF, PBR + cubemaps, 2048 sun shadows, grade + FXAA). One fresh boot per row,
median of ~65 `r_gpuTimers 10` reports (≈650 frames). Run-to-run noise on the
total is about ±0.2 ms.

## Why the old numbers were wrong

`com_speeds` cannot see the renderer on this Mac. Windowed GL-on-Metal is paced
by WindowServer, so the main thread sits in
`beginRenderPass -> commandBufferWithUnretainedReferences -> semaphore_wait`
(found with `sample`) and every config reads ~15 ms, even at 720p or with
`r_postProcess 0`. Two harness bugs also made earlier matrices meaningless:

- **ioq3 keeps only 32 `+` command-line commands** (`MAX_CONSOLE_LINES`). A
  launcher with a full showcase `+set` block silently drops anything after the
  32nd, which included every A/B override. The bench now execs `bench.cfg` and
  sends overrides over the pipe, then runs `vid_restart`.
- The bench pose has to survive. The floor kills, the killcam fires, and
  `CG_KillcamRestoreCvars` resets `r_bloom`, `g_timeBind`, the DoF range and
  others to their defaults mid-run.

So the new `r_gpuTimers N` cvar uses `GL_TIME_ELAPSED` segments (Apple's GL
returns 0 for `GL_TIMESTAMP`) to print the average GPU time per pass every N
frames:

    gpu: scene 4.853 tonemap 0.907 bloom 1.474 dof 1.069 hud 2.705 present 0.343 total 11.362 ms

| segment | covers |
|---|---|
| scene   | sun-shadow cascades + depth prepass + world/entities |
| tonemap | MSAA resolve + SSAO apply + autoexposure + tonemap (+ copy-back) |
| bloom   | bloom + sun rays |
| dof     | depth of field |
| hud     | bokeh + 2D/HUD (also picks up the tail of the previous pass on a TBDR GPU) |
| present | grade/FXAA/bodycam present blit |

Attribution between neighbouring segments blurs because of tile deferral, so
read **the total delta** of an A/B pair as the cost of that feature.

## Per-feature cost (delta in total GPU ms vs the showcase baseline)

| toggle | before (master) | after (this branch) | notes |
|---|---:|---:|---|
| baseline total | **13.41** | **11.37** | −2.0 ms / −15 % |
| r_dof 1 -> 0 | −3.86 | −2.14 | adaptive gather taps |
| r_bloom 0.18 -> 0 | −2.31 | −2.08 | 25 blits -> 4 shader passes; much bigger win at 720p (1.15 -> 0.25 ms in-segment) |
| cg_draw2D 1 -> 0 | −2.96 | −2.82 | **the HUD is the third most expensive pass** (follow-up) |
| r_sunShadows 1 -> 0 | −1.51 | −1.29 | |
| r_shadowMapSize 2048 -> 4096 | **+2.98** | +3.43 | never ship 4096 |
| r_shadowMapSize 2048 -> 1024 | −0.50 | −0.37 | blockier, little gain |
| r_pbr + r_cubeMapping -> 0 | −1.53 | −1.24 | scene shading |
| r_ssao 0 -> 1 | +0.60 | +1.41 | |
| r_grade 1 -> 0 (FXAA+grade) | −0.73 | −0.33 | present pass is ~0.34 ms |
| r_fxaa 1 -> 0 | ~0 | ~0 | free inside the grade pass |
| r_autoExposure 1 -> 0 | −0.34 | −0.18 | |
| r_bodycam 0 -> 1 | +0.11 | +0.25 | |
| 2560x1440 -> 1280x720 | −7.06 | −6.38 | scene stays ~3.3 ms: shadows + geometry don't scale with res |

## What changed (branch `perf/render-post-audit`)

1. **`r_gpuTimers`**: per-pass GPU timing that works on macOS (`tr_gputimer.c`), plus
   the `tools/gpubench/` harness (`run.sh`, `matrix.sh`, `parse.py`, `bench.cfg`).
2. **Bloom as 4 shader passes** (`glsl/bloom_*.glsl`, `RB_Bloom`): the old
   chain was 1 single-tap downsample + 4 multiplicative self-blits (c^4 knee) +
   2×(H+V) blurs of 5 additive blits each + a composite, about 25 FBO passes. It is
   now a 16-texel bright-pass downsample (same c^4 knee, less shimmer than one
   bilinear tap), one H and one V 9-texel gaussian (taps ×√2 to match the old
   double-pass width) and a composite. Every pass uses the same non-flipping NDC
   quad, so the old Y-flip parity trap is gone.
3. **Adaptive DoF gather** (`depthoffield_fp.glsl`): taps now scale with the blur
   disc's area (`0.6·r²`, 8..43). The default 6 px max blur uses ~22 taps
   instead of a fixed 43, and only big cinematic racks pay for all 43.

Before/after screenshots (`gpubench` B2_base vs N2_base) match: the bloom stays
upright with no sky ghosting, and the DoF falloff and grade are unchanged.

## Recommended defaults

Keep the showcase look; the expensive knobs, in order:

    seta r_shadowMapSize 2048   // 4096 = +3 to 3.4 ms, the worst single setting
    seta r_ssao 0               // +1.4 ms for a subtle effect on flat dev textures
    seta r_dofAmount 6          // DoF cost scales with blur radius squared now
    seta r_hdr 1                // keep (HDR-off measured slower before)
    seta r_postProcess 1

Perf preset for weak GPUs: add `r_sunShadows 0` (−1.3 ms) and
`r_pbr 0; r_cubeMapping 0` (−1.2 ms).

## Follow-ups worth doing

- **HUD costs ~2.8 ms of GPU.** That is more than bloom, and it's likely many small
  2D draws/fullscreen overlays, each breaking a render pass on the GL-on-Metal
  layer. Profile `cg_draw2D` sub-elements.
- **DoF copy-back**: `RB_DepthOfField` and the tonemap each do a full-res
  scratch -> renderFbo `FBO_FastBlit`. Ping-ponging the render target would drop
  two full-res copies.
- **Killcam stomps user cvars**: `CG_KillcamRestoreCvars` writes hard-coded
  defaults (`r_bloom 0.25`, `g_timeBind 1`, ...) instead of restoring the user's
  values.
