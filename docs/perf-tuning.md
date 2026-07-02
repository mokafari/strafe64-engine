# Performance tuning — measured post-stack costs

Measured 2026-07-02 on the M3 Pro at 2560x1080 (windowed harness, strafe64dm_1337,
GL2 showcase beauty set, com_speeds medians over ~100 frames per config, fresh
boot per config; `scratch/bench/run_bench.sh` methodology). Frames were
present-bound in windowed mode (`cl` carries the wait; `rf`/`bk` near zero), so
treat the **deltas** as the per-pass GPU cost and confirm absolutes fullscreen.

## Measured deltas vs the showcase baseline (HDR + tonemap + bloom + 2048 sun shadows)

| change                        | frame time | takeaway |
|-------------------------------|-----------:|----------|
| r_shadowMapSize 2048 -> 4096  | **+6 ms**  | the MAXQ shadow size is the single most expensive knob at high res |
| r_sunShadows 1 -> 0           | **-3 ms**  | sun-shadow cascades cost ~3 ms |
| r_bloom on -> 0               | **-3 ms**  | bloom chain ~3 ms even at quarter-res FBOs (capture + composite are full-screen blits) |
| r_ext_multisample 0 -> 4      | +0 ms      | masked by the present-bound state — re-measure fullscreen before trusting |
| r_hdr 1 -> 0 (+postProcess 0) | **+4 ms — SLOWER** | the non-FBO fallback present path is worse; do NOT "optimize" by turning HDR off |
| 2560x1080 -> 1280x800         | -1 ms      | resolution itself is not the wall while present-bound |

## What to turn off first when high-res feels slow

1. `r_shadowMapSize 2048` — never 4096 at ultrawide/4K (MAXQ=1 sets 4096; costs ~6 ms).
2. `r_sunShadows 0` — biggest clean win (~3 ms) if the look survives; baked (`--bake`)
   maps don't need runtime sun shadows at all (`r_sunShadows 0` is already the
   recommendation for them — see strafe64-baked-lighting-perf).
3. `r_bloom 0` — ~3 ms; the look flattens, so prefer keeping it and dropping shadows.
4. Keep `r_hdr 1` + `r_postProcess 1` always — the fallback path measured slower.
5. Windowed mode is present-bound on macOS: prefer fullscreen (`FULLSCREEN=1` /
   `scripts/fullscreen.sh`) and close heavy apps (Ableton, extra engine instances)
   before blaming the renderer — see strafe64-fps-system-contention.

## Quick perf settings

In the console (or autoexec.cfg), keep the look and drop the two measured
heavyweights — HDR/tonemap/bloom stay on (cheap, and HDR-off is slower):

    seta r_sunShadows 0
    seta r_shadowMapSize 2048   // cap in case something re-enables them
    seta r_ext_multisample 0
    vid_restart

(A `PERF=1` launcher knob belongs next to the showcase MAXQ block that sets
4096 — add it when the showcase/fullscreen launchers land on master.)

## Re-measuring

The harness lives in the polish-loop scratchpad (`bench/run_bench.sh` +
`parse_bench.py`): one fresh engine boot per config, 6 s of `com_speeds 1`,
condump, medians with the first 20% trimmed. Re-run after renderer changes and
diff the deltas, not the absolutes.

## CPU side

5 bots + full game sim at 2560x1080: **+0 ms at the median** — G_RunFrame and
botlib are not the wall; the frame budget is all present/GPU.

## Bigger engineering levers (scoped, not yet built)

- **r_renderScale** — this rend2 does not have one; adding it (render FBOs at
  0.75-0.85x, stretch at present, HUD native) is the cleanest big win for
  GPU-bound native-res fullscreen. Touches FBO_Init sizing + present blit.
- **Bloom blit slimming** — the ~3 ms is mostly the full-screen capture +
  composite FBO_Blits around the quarter-res chain (kept as FBO_Blit for the
  Y-flip parity — see the RB_Bloom comment before touching this).
