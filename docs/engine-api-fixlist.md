# engine_api.py / engine_mcp.py — fix list

Compiled 2026-06-27 from (a) all 67 Claude session transcripts in
`~/.claude/projects/-Users-gustav-strafe64-engine/*.jsonl` and (b) a code read of
`tools/strafegen/engine_api.py` (2362 lines) + `engine_mcp.py` (1490 lines).

Each item: what's wrong → where → suggested fix. Grouped by priority.

---

## P0 — highest-frequency footguns (block the core rebuild→verify→capture loop)

### 1. `deploy_dylibs` all-or-nothing guard returns `[]` and looks like a silent no-op
`deploy_dylibs` ([engine_api.py:192](tools/strafegen/engine_api.py:192)) skips the **entire** deploy the moment **one** of the three dylibs on disk is newer than the build output, and returns `done = []` (line 216). `rebuild()` then reports `deployed: []`, the engine keeps loading the stale `cgame.dylib`, and a new cvar reads back `null` — the single most-cited pain across ~15 sessions (also surfaces as `Unknown event: 85` and `glow shaders missing!` from a mismatched trio).
- **Fix:** when the guard trips, raise/return a *loud* structured result (`{skipped, reason, deployed_mtimes, build_mtimes}`) instead of an empty list; and reconsider per-file skip vs. trio skip. Make `rebuild()` surface "deploy SKIPPED (concurrent build) — pass force=True" rather than an empty `deployed`.

### 2. `command()` throws raw `BrokenPipeError` on a dead engine (TOCTOU)
[engine_api.py:550](tools/strafegen/engine_api.py:550) checks `self.alive` then `os.write(self._pipe_fd, …)` (line 556). If the engine dies between the check and the write — common — the user gets `BrokenPipeError: [Errno 32] Broken pipe` from `t_command`/`t_give`/`t_console` (16 hits, 6 sessions) instead of a clean "engine has exited".
- **Fix:** wrap `os.write` in `try/except (BrokenPipeError, OSError)` → raise `EngineError("engine has exited (broken pipe)")`. Also handle partial writes (os.write may write < len).

### 3. Capture/measure freezes the player body
The sim only advances the local player while `engine_input` is actively sending usercmds; idle `measure`/`wait`/`screenshot` lets the body freeze or drift, making timed visual capture extremely awkward (~40 sessions of narration fighting this).
- **Fix:** a "hold pose / keepalive" mode that keeps feeding neutral usercmds during capture windows, or document + expose an explicit `keepalive=True` on `measure`/capture tools.

### 4. Capture/input tools silently force `g_timeBind 0` and never restore it
`play_mode` ([:1242](tools/strafegen/engine_api.py:1242)), `engine_input` path ([:1110](tools/strafegen/engine_api.py:1110)), and `capture_death` ([:1618](tools/strafegen/engine_api.py:1618)) all `set_cvar("g_timeBind", 0)` to keep scripted sequences deterministic — then leave it off. Every attempt to *test* the bullet-time feature disabled the very feature under test ("static slow-mo" phantom bug, chased repeatedly across many sessions).
- **Fix:** save the prior `g_timeBind` and restore on exit (or expose `time_bind=` param / context manager). At minimum log a one-line warning when auto-disabling.

---

## P1 — lifecycle & process hygiene (recurring, moderate)

### 5. "no engine running" / "already running" churn has no auto-recovery
`EngineError: no engine running for '<name>'` (41 hits, 22 sessions) and `a persistent engine named '<name>' (pid N) is already running` (19 hits, 18 sessions). Named persistent engines (esp. `mapstudio`, `default`) die between turns or get left running; tools error instead of recovering.
- **Fix:** option for `engine_open`/state-needing tools to auto-reopen a dead named engine; make "already running" a soft attach-or-warn rather than a hard error.

### 6. Stale-PID desync between the daemon, the MCP server, and the on-disk state file
The daemon/MCP holds a stale pid → `engine_attach` refuses, forcing manual raw-pipe writes (~20 sessions). State file `engine_api.session.json` isn't reconciled against `_pid_alive`.
- **Fix:** validate the stored pid with `_pid_alive` on every attach/list; prune dead entries automatically; have `engine_attach` re-adopt a live pipe even if the recorded pid is stale.

### 7. Orphan windows accumulate; a stale leftover engine starves `measure` (0 samples)
Leftover session windows pile up (often the old pre-auto-port `net_port 27970` default); a zombie can jam the `default` slot until the MCP server restarts; `measure` silently returns 0 samples against a stale instance.
- **Fix:** `measure`/tuning tools should detect 0-sample/stale and tell the user to run `engine_kill_orphans`; consider auto-reaping orphans on `engine_open`.

### 8. `apistate` reply unavailable → `engine_state`/`engine_look` fail on stale qagame or dedicated mode
`no apistate reply (needs the rebuilt qagame + client mode)` / `Unknown command apistate` (14 hits, 16 sessions) when the qagame is stale or running non-client.
- **Fix:** detect the missing-command case and emit an actionable error ("rebuild+deploy qagame, or run in client mode") instead of a generic timeout; surface it in `engine_doctor`.

### 9. Startup races: FIFO/console not created, or engine SIGKILLed during boot
`engine did not create com_pipefile within 40s` ([:450](tools/strafegen/engine_api.py:450)), `engine exited during startup (code -9)`, `command FIFO missing`, plus `Hunk_Alloc failed` collapsing multi-bot heats (~40 sessions touch startup).
- **Fix:** longer/configurable boot timeout for heavy maps+bots; on `proc.poll()!=None` during `_await_ready`, tail and include the last N console lines in the error; pre-flight `com_hunkMegs` for high bot counts.

### 10. `engine_spectate` suicides the local player and ends heats mid-capture
Dropping to spectator issues `MOD_SUICIDE`; in last-pilot/race/lattice modes that death ends the round mid-capture.
- **Fix:** spectate via a non-scoring path (or temporarily neutralize the death in those modes), or warn when called in a round-ending gametype.

---

## P2 — argument/parse robustness (MCP schema ↔ handler drift)

### 11. Camera tools reject string-form coordinate subjects
`_resolve_target` ([engine_api.py:937](tools/strafegen/engine_api.py:937)) only accepts a real `list/tuple` `[x,y,z]`; the MCP `subject` schema is untyped/string, so the model passes the literal string `"[0,0,300]"` → `no subject '[0,0,300]' — have ['UnnamedPlayer']` (14 hits, 10 sessions; the documented "can't frame an empty course" gap).
- **Fix:** in `_resolve_target`, parse string forms `"[x,y,z]"` / `"x,y,z"` (json.loads / split) into floats before the lookup.

### 12. MCP arg-schema drift → `unexpected keyword argument`
`TypeError: t_frame() got an unexpected keyword argument 'title'` (also `t_screenshot`, `t_orbit`, `t_console`, `t_input`, `t_map`, `t_launch`, `t_clip_video`, `t_spectate`); plus `t_console() missing 1 required positional argument`. Maps to "the mcp has been updated… still a bit off." (6+ sessions).
- **Fix:** audit every `t_*` signature against its declared JSON schema; add `**kwargs` tolerance or a thin validation wrapper that returns a clean "unknown arg X; accepted: …" message.

### 13. Type-coercion errors in handlers
`ValueError: invalid literal for int(): 'rocketlauncher'` (weapon *name* passed where a weapon *index* is expected, input/give path); `engine_set_cvar` on `sv_cheats` at runtime forces a server restart that drops the MCP session.
- **Fix:** accept weapon names → map to indices; for restart-forcing cvars, warn and/or reconnect the session automatically.

### 14. `no reply for command within 6.0s` — console readback occasionally never returns
Echo-sentinel log-scrape readback ([state](tools/strafegen/engine_api.py:732) / cvar print) times out intermittently (6 hits), esp. in free-cam.
- **Fix:** make the readback timeout configurable per call; retry once on miss; fall back to a second sentinel pass before erroring.

---

## P3 — missing capabilities / known limitations (block whole test categories)

### 15. `engine_input` has no crouch (`upmove<0`) or block/parry action
Blocks scripted testing of crouch-slide and katana projectile-deflect — forces console-bind workarounds (hit repeatedly).
- **Fix:** add `crouch`/`upmove` and a block/`+button` mapping to the input action set.

### 16. `engine_input` is blocking → can't sample velocity mid-motion
Synchronous input + friction zeroing velocity on key-release means speed can't be read mid-move.
- **Fix:** non-blocking hold-and-sample mode (start hold → return immediately → `measure` while held → release).

### 17. No reliable dense-footage / MP4 pipeline
Demos on a listen server are data-starved (`Wrote 4:0 frames` → ~0.16s renders) regardless of `sv_fps`/`g_synchronousClients`; smooth demo→MP4 via the engine `video` command was punted "to a future iteration." `clip_video` (fast screenshots) is the workaround.
- **Fix:** dedicated-server demo recording path, or wire the engine `video`/`stopvideo` AVI route in `render_demo` with AVI-growth end detection.

### 18. `engine_map_bounds` can't get true geometry bounds (radius heuristic only)
Auto-framing from real geometry needs a C-side change; current bounds are player-spread + radius heuristic ([_room_box](tools/strafegen/engine_api.py:958)). Informational only.
- **Fix (optional):** expose true BSP bounds via a small qagame `apistate` extension; until then, keep the radius path and label it.

### 19. `bspc` (AAS navmesh) path is flaky/non-deterministic and can't run standalone
Per-seed non-deterministic; lives inside the engine_api/MCP path so a navmesh can't be regenerated independently.
- **Fix:** a standalone `bspc` invocation with retry-on-failure + deterministic seed, decoupled from a running engine.

### 20. `engine_set_source_constant` drops trailing zeros (not byte-exact)
[set_source_constant](tools/strafegen/engine_api.py:2042) rewrites `1.10`→`1.1` — functionally identical, not byte-exact.
- **Fix:** preserve the original literal's formatting (regex-replace just the numeric token, keep its text) — or document as cosmetic.

### 21. Airborne-spawn quirk skews `capture_death` / audition framing
Spawned bodies can be non-grounded → death-cam/audition shows a floating body.
- **Fix:** ground-snap spawned audition bodies (trace down to floor) before framing.

### 22. `misc_model` runtime spawn renders nothing (won't-fix, document)
`engine_spawn("misc_model")` is compile-time/q3map-only; there's no runtime custom-prop audition. Already investigated → negative.
- **Fix:** document the limitation in `engine_help`/`ENGINE_API.md` so it isn't re-investigated.

---

## P4 — code quality / maintainability

### 23. Broad `except Exception:` swallows at 4 sites
[:487](tools/strafegen/engine_api.py:487), [:505](tools/strafegen/engine_api.py:505), [:528](tools/strafegen/engine_api.py:528), [:2298](tools/strafegen/engine_api.py:2298) silently swallow — fine for cleanup, but hides real failures during quit/cleanup.
- **Fix:** narrow to expected exception types; at minimum debug-log what was swallowed.

### 24. 63 `time.sleep(...)` fixed-delay timing points
Fixed sleeps in startup/spawn/play_mode/capture make the harness timing-fragile under load (the freeze/race family above).
- **Fix:** replace the load-bearing ones with `wait_for`/`wait_state` polling on an observable condition.

### 25. Thin unit-test coverage of pure helpers
`engine_api.py` is ~2362 lines with many pure-logic helpers (config parse, geometry, collage, aggregation, formatting) and only a small `test_engine_api.py`. Self-edits + concurrent dev on shared files regress silently.
- **Fix:** expand `test_engine_api.py` over the pure functions (`_resolve_target`, `_room_box`, `_clamp_to_room`, color-strip, cvar parse, deploy-guard mtime logic).

### 26. Recurring stale-binary / wrong-asset-path confusion is under-guarded
"silently tested stale binary", "OLD OA path", leftover `~/ioquake3` / `~/openarena-0.8.8` mounts undermine trust in results. `engine_doctor` exists but isn't run automatically.
- **Fix:** run a lightweight `engine_doctor`-style asset/dylib freshness assertion at `engine_open`/`rebuild` and warn loudly on mismatch.
