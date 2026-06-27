# MapForge — test run findings & what's missing

Full end-to-end test (CLIs + headless browser across every feature). **24/24
browser steps pass, no console errors.** Generate (all 18 kinds), edit, import/
decompile, learn, calibrate, trace-to-compose, auto-layout, branching (split),
freeform boxes, entity placement, save/load, and export (.bsp/.map/.pk3) all work.

## Fixed during the test run
- **pk3 export 500'd when `bspc` can't execute** (arm64 binary on x86) — the aas
  step now degrades to "no bots" instead of failing the export.

## Environment realities (not bugs)
- Bundled `bspc` is arm64 → no bot `.aas` on x86 hosts (works on the user's Mac).
- No `q3map2` here → `--bake` (real lightmaps) unavailable.
- three.js is vendored → runs fully offline.

## Missing — prioritised

### P1 — highest value
1. **Engine integration.** No "Play in engine" / "Bot playtest" button. The
   `strafe64-engine` MCP already has `engine_generate_map` / `engine_open` /
   `engine_playtest_report`; MapForge can't deploy an exported map into the live
   game to actually test it. For a map tool this is the #1 gap.
2. **Texture / material assignment.** Everything uses role palettes / dev
   textures; no per-brush real texture picker.
3. **Lighting authoring.** Maps are vertex-lit only — no light entities, no
   sun/ambient control, no `--bake` hook from the UI.

### P2 — editing depth
4. ~~**No undo/redo.**~~ **DONE (compose)** — auto-capture history, ↶/↷ buttons,
   Ctrl+Z / Ctrl+Shift+Z, and Delete-key removes the selection. (Generate-mode
   edits still have no undo.)
5. **Free boxes/entities**: now have **grid snap** (64u toggle) and **duplicate**
   (Ctrl+D). *Still:* axis-aligned only, XY drag (Z numeric-only), no rotation
   (would need non-axis brushes) or multi-select.
6. **Generate-mode geometry editing** is limited to axis boxes; ramps/prisms are
   read-only and you can't add ramps/cylinders/arbitrary prisms.
7. ~~**Entity keys**: only `origin` is editable.~~ **DONE** — placed entities now
   have a free-form key editor (angle, spawnflags, target/targetname, wait, …),
   keys export into the .bsp and are preserved when tracing a decompiled map.
   *Still missing:* brush-based trigger entities (trigger_push/teleport volumes)
   can't be authored in the UI — only point entities.
8. **Worldspawn/void control** in compose is fixed (void rate + sky); no UI for
   void rise/delay, gravity, fog, sky, music, or the map message.

### P3 — decompile / learn depth
9. **Patches** (curved surfaces) are approximated by their control grid
   (faceted), not Bézier-tessellated; non-axis brushes over-approximate to an
   AABB in the trace.
10. **Greedy box-merge** now fuses contiguous coplanar same-colour boxes on trace
    (footprint-hash + sweep, O(n log n)/axis) — biggest win on surface-thickened
    maps. *Still:* trace caps at 1500 brushes (large maps truncated).
11. **Learning feedback is shallow** — only platform/height feed `calibrate`.
    Item mix, jump-pad arcs, room/section types, and texture vocabulary aren't
    fed into generation or auto-layout.
12. No quantitative **diff/compare** between a generated map and the learned
    target.

### P4 — workflow / UX
13. Save/load is **local file only** — no server-side layout library or
    shareable links.
14. **Pre-export lint** now warns live in Compose (disconnected sections, no
    spawn, no finish, open connector count). *Still missing:* overlap detection
    and unreachable-gap checks (those need collision/physics analysis).
15. **Camera presets** (top / front / fit) added to the viewbar. *Still:* no
    frame-selection or measurement readout.
16. ~~No legend for entity marker colours.~~ **DONE** — a marker legend shows in
    both panels.
17. ~~Status label "N sections" confusing for freeform-only layouts.~~ **DONE** —
    status now reads "N sections · M boxes · K entities".
18. Fixed 3-column desktop layout; not responsive.
19. No in-app help/tutorial.
