# STRAFE 64 — Polish Punch-List

A running list of small-to-medium quality improvements, gathered one subsection
at a time. Each iteration of the polish loop fills the next subsection with ~10
concrete, actionable items (file:line + what/why). Nothing here is a bug-fix
mandate — it's a curated backlog of "this could be cleaner" cleanups.

## Subsection plan

- [x] 1. cgame — client effects & HUD (`engine/code/cgame/`)
- [x] 2. q3_ui — menus & front-end (`engine/code/q3_ui/`)
- [x] 3. game — server logic & bots (`engine/code/game/`)
- [x] 4. strafegen — map generator (`tools/strafegen/`)
- [x] 5. client/sound — audio & varispeed (`engine/code/client/snd_*`)
- [x] 6. qcommon — license, common, net (`engine/code/qcommon/`)
- [x] 7. bg_pmove / physics — movement model (`engine/code/game/bg_*`)
- [x] 8. renderer — GL2 post-stack & shaders (`engine/code/renderergl2/`)
- [x] 9. tooling/scripts — shells, engine_api, dojo (`scripts/`, `tools/`)
- [x] 10. docs & cfg — design specs, autoexec, presets (`docs/`, `*.cfg`)

> Legend: each item is `Title — file:line — what to polish & why`.

---

## 1. cgame — client effects & HUD  ✅ FIXED (a4b6c45: items 4,5,6,9)

1. **Repeated vertex-colour init in particles** — `engine/code/cgame/cg_particles.c:282-315` — `verts[].modulate` is set to (255,255,255,255) identically ~12 times; extract a small helper/macro to cut duplication.
2. **Magic `255` byte conversion scattered** — `engine/code/cgame/cg_players.c:903-914` — hardcoded 255 multiplier in colour conversions; define `COLOR_BYTE_MAX` near the colour helpers for clarity.
3. **Duplicated angle wraparound** — `engine/code/cgame/cg_weapons.c:1004-1017` — manual 360° clamp duplicated across both `cg_trueLightning` branches; route through a shared `CG_NormalizeAngle()`.
4. **Missing clientNum bounds check** — `engine/code/cgame/cg_players.c:883` — `CG_NewClientInfo()` indexes `cgs.clientinfo[clientNum]` without validating; mirror the guard in `CG_CustomSound()` (~line 57).
5. **Unnamed PVS distance constant** — `engine/code/cgame/cg_particles.c:270` — magic `1024` PVS range is unexplained; name it (`CG_PARTICLE_PVS_RANGE`) or expose as a LOD cvar.
6. **Stale commented-out yadj code** — `engine/code/cgame/cg_draw.c:150-151` — two commented `yadj` calcs reference dead field names (`Assets.textFont`); delete or document intent.
7. **Inconsistent colour-init whitespace** — `engine/code/cgame/cg_players.c:903-914` — mixed spacing around `newInfo.c?RGBA` assignments; reformat for consistency.
8. **Powerup sort loop off-by-one risk** — `engine/code/cgame/cg_draw.c:1214-1226` — insertion writes `sorted[k+1]` without an explicit cap as `active` nears `MAX_POWERUPS`; add a guard.
9. **Hardcoded lattice fade distances** — `engine/code/cgame/cg_lattice.c:530-531` — near/far self-fade `96.0f`/`150.0f` are baked in; promote to `cg_latticeNearFade`/`cg_latticeFarFade` cvars.
10. **Copy-paste gib launch blocks** — `engine/code/cgame/cg_effects.c:588-655` — `CG_GibPlayer()` repeats 8 near-identical VectorCopy+velocity+`CG_LaunchGib` blocks; collapse into a model-table loop.

---

## 2. q3_ui — menus & front-end  ✅ FIXED (items 3,6,7)

1. **Centralize NERV/MAGI colour palette** — `ui_qmenu.c:41-61` (+ `ui_menu.c:69`, `ui_arena.c:60`, `ui_dev.c:97`, `ui_generate.c:75`, `ui_credits.c:46`, `ui_confirm.c:39`, `ui_cdkey.c:46`) — the same amber/green/dim/grid colours are redefined in 7+ files; hoist to `ui_local.h` or a palette header.
2. **Name magic screen coordinates** — `ui_generate.c:56-59` (and throughout) — literals like `320` (center x), `70`, `180`, `280`, `24` should become `#define`s (`MENU_CENTER_X`, `RULE_WIDTH`, …) for consistency.
3. **Remove stale `APSFIXME` comment** — `ui_atoms.c:643` — `int forceColor = qfalse; //APSFIXME;` is a leftover placeholder; clarify or drop it.
4. **Standardize menu vertical spacing** — `ui_arena.c:388-405`, `ui_dev.c:417-425`, `ui_generate.c:577-651` — mixed `y += 22/24/30/36/40`; define `MENU_ITEM_SPACING`/`MENU_BUTTON_SPACING` so cadence is tunable in one place.
5. **Collapse `Menu_AddItem` boilerplate** — `ui_arena.c:430-440`, `ui_dev.c:545-552`, `ui_dev.c:898-910`, `ui_generate.c:697-708` — every menu hand-loops AddItem calls; a `Menu_AddItems(menu, items, count)` helper removes ~40 duplicated lines.
6. **Extract weapon-bind command strings** — `ui_arena.c:249-251`, `ui_dev.c:746,755` — `bind 1 "weapon 7"` / `"weapon 11"` appear inline twice; name them `BIND_VECTORGUN` / `BIND_SWORD`.
7. **Validate Forge seed field** — `ui_generate.c:199-201,216-221` — `ForgeMenu_CurrentSeed()` runs `atoi()` with no range check; clamp to `[1, INT_MAX)` to avoid silent wraparound.
8. **Factor menu-init template** — `ui_dev.c:407-439,517-555,620-653` — each subpanel repeats the memset→draw/fullscreen/wrapAround setup; pull into one init helper.
9. **Name divider-rule geometry** — `ui_arena.c:337-341`, `ui_dev.c:398-404`, `ui_generate.c:525-526` — section rules use magic coords (`180/134/150/286/250/404`) and widths (`280/330`); give them named constants.
10. **Unify panel-colour naming** — `ui_confirm.c:42` (`cf_panel`), `ui_cdkey.c:51-52` (`lic_panel`/`lic_back`), `ui_qmenu.c:155` (`s64_panel`), `ui_generate.c:79` (`forge_panel`) — inconsistent prefixes and alphas (0.55/0.92/0.94); standardize to `PANEL_*_RGBA` and document alpha tiers.

---

## 3. game — server logic & bots

1. **`bot_brake` missing `trap_Cvar_Update`** — `ai_main.c:2088` — registered but never updated in `BotAISetup()`, so it's frozen at its registration value (the known frozen-vmCvar footgun); add the update call.
2. **Unnamed sword-duel rhythm constants** — `ai_main.c:1122-1132` — orbit/dance timers use magic literals `777/2600/520/1500`; name them so the cadence is legible and rebalanceable.
3. **Unbounded `strcpy`/`strcat` on path buffers** — `g_bot.c:176-177,965-966` — filepath assembly into `filename[128]` has no overflow guard; switch to `Q_strncpyz`.
4. **Dead `#if 0` teleporter block** — `g_misc.c:187-197` — `SP_misc_model()` carries a disabled hardcoded-model index block; remove or finish it.
5. **Duplicated team-validity check** — `ai_main.c:406,419` (mirrored `g_bot.c:241,253`) — `atoi(Info_ValueForKey(...,"t")) == TEAM_RED/BLUE` is copy-pasted; factor a helper.
6. **`& 1024` used as a boolean** — `ai_main.c:1168` — peel-direction toggle ANDs against `1024`; use `& 1` (or `% 2048 < 1024`) so the intent is clear.
7. **Per-client static arrays indexed without guard** — `ai_main.c:951-963,1252` — `g_botSurfStrafe[]`, `g_botAirStrafe[]`, `g_botDashTime[]` etc. indexed by `bs->client` with no `MAX_CLIENTS` bound check.
8. **Scattered physics magic floats** — `ai_main.c:998,1006,1061,1114,1165,1282` — `0.7f` walkable, `0.05f` wall, `220.0f` blade reach, `700.0f` dash range, etc. should be named constants/cvars for live tuning.
9. **Empty `FIXME: parse scores?` handler** — `ai_main.c:1689` — `cp`/`cs`/`scores` server-command blocks are empty with a stale FIXME; implement score tracking or delete the dead branches.
10. **Unvalidated skill cvar conversion** — `g_bot.c:54-59` — `atof()` on the skill cvar has no range check, letting negative skill bypass the clamp in `G_AddBot()` (`g_bot.c:675-682`); clamp at the source.

---

## 4. strafegen — map generator (Python)

1. **`sun_keyword()` lacks docstring** — `strafegen_gfx.py:69` — public helper that builds `q3gl2_sun`+`q3map_sun` keywords has no docstring describing what it emits.
2. **Wildcard imports hide the API surface** — `strafegen.py:37-49` — `from strafegen_physics import *` (and siblings) obscure what's actually re-exported; enumerate names.
3. **`inject_sun()` lacks docstring** — `strafegen_gfx.py:88` — edits shader scripts in place but doesn't document idempotency or the anchor it searches for.
4. **Possibly-misscoped `import sys`** — `strafegen_pack.py:11` — only used once (stderr at ~line 127); scope it locally or confirm it's needed.
5. **Hardcoded killbox half-width** — `strafegen_killbox.py:243` — `W = 1408.0` with only an inline comment; promote to `KB_HALF_WIDTH` module constant.
6. **`Course.build()` is 87 lines** — `strafegen_course.py:625-711` — mixes dojo recipe, section scheduling, and void setup; split into helpers.
7. **`have_q3map2()` lacks docstring** — `strafegen_bake.py:34` — binary-probe helper with no return/behaviour doc.
8. **`augment()` lacks docstring** — `strafegen_gfx.py:215` — key shader-augmentation entry point undocumented.
9. **Repeated `trigger_push` boilerplate** — `strafegen_killbox.py:350,397` — `make_box(...)` + append `{"classname":"trigger_push",...}` duplicated 3+ times; extract a helper.
10. **Magic difficulty-scale tuple** — `strafegen_course.py:289` — `(0.88,0.94,0.99)[self.diff]` hall-width multiplier should be a named `HALL_REACH_SCALES` constant.

---

## 5. client/sound — audio & varispeed

1. **Magic `256` volume scale** — `snd_dma.c:1004,1006` — raw-sample volume calc uses bare `256`; name it (`VOICE_VOLUME_SCALE`) since the pattern repeats.
2. **Fixed `raw[30000]` decode buffer** — `snd_dma.c:1437` — music decode buffer is a hardcoded stack array with only a "mac stack frame" comment; no const/bounds guard if a chunk exceeds it.
3. **Dead commented `s_backgroundMusic`** — `snd_dma.c:42` — `//static char s_backgroundMusic[...]; //TTimo: unused`; remove now that the playlist replaced it.
4. **Playlist index not clamped after advance** — `snd_main.c:234-248` — `S_NextPlaylistTrack()` writes `out` after bumping `s_playlistIndex` without re-checking bounds; risks OOB read.
5. **Unseeded `rand()` shuffle** — `snd_main.c:111` — `S_ShufflePlaylist()` uses bare `rand()`; shuffles can be predictable per build — seed it or route through a controlled RNG.
6. **Time-pitch missing in DMA backend** — `snd_dma.c` (whole file) — OpenAL has full `s_timePitch*` varispeed (`snd_openal.c:1463`); DMA fallback has none, so audio feel diverges when OpenAL is unavailable.
7. **No clamps on time-pitch cvars** — `snd_openal.c:2648-2651` — `s_timePitchFloor/Ceil/Curve/Smooth` are unvalidated; `Floor<=0` breaks `S_AL_PitchFor()` (AL_PITCH must stay > 0, line ~1520).
8. **Duplicated volume→255 clamp across backends** — `snd_dma.c:922-927` vs `snd_openal.c` — both clamp spatialized volume separately; share a utility to prevent divergence.
9. **M3U parser has no line-length cap** — `snd_main.c:174-182` — `S_PlaylistAddM3U()` reads to `\n`/`\r` with no max length before passing into a `MAX_QPATH` buffer; a pathological line overflows.
10. **Cvar name case mismatch** — `snd_main.c:812` — registered as `"s_musicvolume"` but referenced as `s_musicVolume->value`; the lowercase registered name breaks search/discoverability.

---

## 6. qcommon — license, common, net

1. **Prefix read before length check** — `license.c:110` — `Lic_DecodeKey` reads `p[0..2]` before confirming the string is ≥3 chars; guard against OOB on short keys.
2. **Missing `const` on `licenseInfo_t` fields** — `license.c:40-43` — `tier`/`flags`/`issueDays` are write-once after verify; const-qualify to signal intent.
3. **Magic `122` base32 digit count** — `license.c:100` — inline literal documented only by comment; name it (`LICENSE_BASE32_DIGITS`) and cross-ref `LICENSE_BLOB_LEN`.
4. **Payload indices assume length** — `license.c:184-189` — `message[2..5]` accessed assuming `mlen == LICENSE_PAYLOAD_LEN` with no explicit check; validate first.
5. **Prefix advance lacks total-length guard** — `license.c:110-111` — `p += 3` after the S64/s64 check without confirming length ≥3; can run off a short string.
6. **Vague invalid-key error** — `license.c:221` — "License key is not valid" doesn't distinguish decode vs signature vs product mismatch; add a reason code for debugging.
7. **Undocumented Crockford substitutions** — `license.c:73-74` — O→0, I/L→1 look-alike mappings are correct but unmarked; add a one-line "Crockford alphabet" comment.
8. **Scattered `"S64"` prefix literal** — `license.c:20,110,248,251` — define `LICENSE_PREFIX` and use it everywhere instead of repeating the string.
9. **`license_pubkey.h` lacks generated-file marker** — `license_pubkey.h:1-2` — emitted by keygen but has no "auto-generated, do not edit" header; add one.
10. **tweetnacl lacks vendored marker** — `tweetnacl.c:1`, `tweetnacl.h:1-2` — no "unmodified upstream reference crypto — do not edit" header / upstream link; add it so the file isn't casually patched.

---

## 7. bg_pmove / physics — movement model

> ⚠️ Feel-critical code: every item here is **naming / comment / structure** polish — name the constant, don't change the value.

1. **Name the `550.0f` slide-slick speed** — `bg_pmove.c:278` — `float slick = 550.0f;` is the friction inflection point; promote to `pm_slideFrictionPeakSpeed` (same value), don't retune.
2. **Extract horizontal-velocity pattern** — `bg_pmove.c:885-887,949-951,1557-1559` — `dir[0]=v[0];dir[1]=v[1];dir[2]=0;` repeated 3×; add a `VectorHorizontal()` helper.
3. **`#define` the coyote sentinels** — `bg_pmove.c:1691-1696` — magic `-1/-9000/-8000` in the STAT_GROUND_MS sign-encoding; name them so the trick is self-documenting.
4. **Document the 16-bit ground-stat cap** — `bg_pmove.c:2674-2675` — `999` cap "to keep in 16-bit range" is terse; expand the comment so future tuners don't widen it blindly.
5. **Name the air-dash momentum threshold** — `bg_pmove.c:889` — `hsp < 50.0f` chooses momentum-follow vs held-input; name it `pm_airDashMinMomentum` (keep value).
6. **Helper for bhop-chain condition** — `bg_pmove.c:509,1015` — duplicated boost/streak checks; a `PM_IsBhopChained()` clarifies the shared condition.
7. **Name the wall-detect slope threshold** — `bg_pmove.c:977` — magic `-0.1f` next to `MIN_WALK_NORMAL` (`bg_local.h:25`) for vertical-wall detection; give it a named `#define`.
8. **Name the wall-run attach grace** — `bg_pmove.c:776` — `abs(stats[STAT_WALLRUN]) < 150` is the held-jump attach window; name it `pm_wallRunAttachGraceMs`.
9. **`COYOTE_SENTINEL` define** — `bg_pmove.c:1693,2695` — the `-9000` "coyote not armed" park value should be a `bg_local.h` define so it isn't mistaken for a real timing value.
10. **Helper for slide-entry boost** — `bg_pmove.c:1555-1570` — inline slide-entry speed/cap math mixes vectors and clamps; factor `PM_SlideEntryBoost()` for readability (behaviour unchanged).

---

## 8. renderer — GL2 post-stack & shaders

> ⚠️ Perf-sensitive: prefer naming / comment / DRY polish over behavioural change.

1. **Name the Gaussian blur weights/offsets** — `tr_postprocess.c:398-407` — `RB_BlurAxis` hardcodes `0.227.../0.316.../0.070...` weights and `1.384/3.230` offsets; `#define` them so the filter is legible.
2. **Name auto-exposure blend factors** — `tr_postprocess.c:74-77` — `0.03f` (float tex) vs `0.1f` (non-float) convergence rates; `EXPOSURE_BLEND_HQ/LQ` documents why they differ.
3. **Name SunRays blur passes/stretch** — `tr_postprocess.c:374-379` — magic loop count `2`, `2/3` stretch add, `1.125f` mult; name them for tuning.
4. **Name SunRays visibility cutoff & alpha** — `tr_postprocess.c:303,379` — `0.25f` sun dot cutoff and `1.125f` glow alpha should be independent named constants.
5. **Name BokehBlur scale multiplier** — `tr_postprocess.c:111` — `blur *= 10.0f` is unexplained; `BOKEH_BLUR_SCALE` documents the input-range remap.
6. **Name BokehBlur skip threshold** — `tr_postprocess.c:113` — silent-skip `0.004f` epsilon → `BOKEH_BLUR_THRESHOLD`.
7. **DRY the quarterBox Y-flip setup** — `tr_postprocess.c:123-126,355-358` — identical `quarterBox` Y-flip init in BokehBlur and SunRays; extract a helper.
8. **Strengthen the bloom Y-flip warning** — `tr_postprocess.c:515-522` — the FBO_Blit-vs-FastBlit parity note is great; add a one-line `// CRITICAL:` banner atop `RB_Bloom()` so a refactor can't silently regress the flip.
9. **Expand the MAX_POLYS comment** — `tr_local.h:2503` — note it's a compile-time cap and that over-budget scenes silently drop polys (`tr_scene.c:138`); flag "could be a cvar" so the 600→8192 trade-off is understood.
10. **Remove dead `#if 0` post-process block** — `tr_backend.c:1658-1689` — superseded GLSL-less quad fallback in `RB_PostProcessDepth`; delete or document.

---

## 9. tooling/scripts — shells, engine_api, dojo

1. **`showcase.sh` lacks `set -euo pipefail`** — `scripts/showcase.sh:1` — no strict mode; unquoted vars / unhandled failures pass silently in batch/CI.
2. **`play.sh` hardcoded `/Users/gustav` paths** — `tools/strafegen/play.sh:20` — `ENGINE`/`OA` absolute paths are non-portable; derive from repo root or env override like dojo does.
3. **`build-q3map2.sh` has `-e` but not `-u`** — `tools/strafegen/build-q3map2.sh:23` — undefined vars (e.g. misspelled `$LLVM`) fall through as empty strings, masking config errors.
4. **codesign subprocess has no timeout** — `tools/strafegen/engine_api.py:227` — a hung `codesign` blocks deployment indefinitely; add a timeout.
5. **Bare `except Exception` in `sh()`** — `tools/strafegen/dojo.py:118` — returns `"?"` for any error (incl. `KeyboardInterrupt`), making subprocess failures undebuggable; narrow it.
6. **`Popen.stdin.write` without timeout** — `tools/strafegen/dojo.py:166` — a clogged pipe deadlocks the whole batch forever; guard the write.
7. **`playtest.py` hardcoded `/Users/gustav` paths** — `tools/strafegen/playtest.py:20-24` — same portability break as `play.sh`; derive paths.
8. **ffmpeg encode calls without timeout** — `tools/strafegen/engine_api.py:1457,1536` — video concat/convert can stall for hours silently; add timeouts.
9. **Harden keygen private-key file perms** — `tools/keygen/keygen.py:76` — confirm the written private key is `chmod 0600` (not world-readable) and avoid echoing its full path to stdout where it can be logged/screenshotted.
10. **Bare `except Exception` in `list_processes()`** — `tools/strafegen/engine_api.py:2163` — swallows all `ps` errors and returns `[]`, hiding permission/platform failures; surface them.

---

## 10. docs & cfg — design specs, autoexec, presets

1. **Wiki-style `[[...]]` links in docs** — `docs/q3-map-study.md:16,80,87,99` — `[[strafe64-q3map2-macos-build]]`/`[[strafe64-bryce-sky]]` are memory-slug backrefs that render as dead links; convert to real markdown links or inline references.
2. **No docs index** — `docs/` — 13 specs (MOVEMENT, MAP_DESIGN, ROADMAP, VISUALS, forge spec, …) with no `INDEX.md`/TOC; add one so readers can orient.
3. **Duplicated cvar block in two `strafe64.cfg`** — `assets/openarena/baseoa/strafe64.cfg` vs `tools/strafegen/strafe64.cfg` (~lines 14-60) — ~22 shared cvars duplicated; share a `_base.cfg` or document why they must diverge (drift risk).
4. **Undocumented `wait 80` in arena cfgs** — `sword_arena.cfg:22`, `arena_vg.cfg:23`, `arena_squad.cfg:4` — bare frame-delay with no rationale; comment what it waits for and the safe range.
5. **Cryptic `g_timeBindMin` precedence note** — `assets/openarena/baseoa/autoexec.cfg:20` — comment about a config "clobbering the good value at load" is opaque; clarify the load order or refactor so 0.05 wins cleanly.
6. **`feelA.cfg` preset unlabeled** — `assets/openarena/baseoa/presets/feelA.cfg` — bare tuning preset (`pm_strafeAccelerate 111`, `pm_airControlAmount 222`) with no header explaining what "feelA" is or its status.
7. **Stale `[[strafe64-flow-combo]]` ref in ROADMAP** — `docs/ROADMAP.md` — wiki-slug reference breaks reader flow; replace with a real link/section or inline explanation.
8. **No "last updated" on volatile docs** — `docs/ROADMAP.md`, `docs/forge-ingame-strafegen-spec.md` — fast-moving design docs lack a freshness stamp; add `Last updated: YYYY-MM-DD`.
9. **Underscore presets need a clear marker** — `assets/openarena/baseoa/presets/_integ.cfg`, `_selftest.cfg` — leading `_` hints "internal" but is ambiguous; add a top "TESTING ONLY — do not exec" header.
10. **`arena.cfg` doesn't point to its variants** — `assets/openarena/baseoa/arena.cfg:1-18` — the "weapon-agnostic" note assumes you know to `exec sword_arena` / `arena_vg`; name the variant files explicitly.

---

## Status

All 10 planned subsections filled — **100 polish items** total. The list is a
backlog, not a work order: pick the high-value/low-risk ones first (dead code
removal, the overflow guards in §5, the frozen-cvar fix in §3, the "do-not-edit"
markers in §6) and treat the feel-critical (§7) and perf-sensitive (§8) sections
as naming/comment-only unless deliberately retuning.

_Loop complete — see the closing message to stop the 5-minute cron._

