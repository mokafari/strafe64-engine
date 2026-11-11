# STRAFE 64 studio workflow

This adapts the task scoping, discipline review, and evidence-driven delivery
ideas from [Claude Code Game Studios](https://github.com/Donchitos/Claude-Code-Game-Studios)
to this existing native C/ioquake3 game. The entry point is [AGENTS.md](../AGENTS.md),
also imported by [CLAUDE.md](../CLAUDE.md).

## Adaptation boundary

Reviewed upstream `CLAUDE.md`, `.claude/docs/coordination-rules.md`, and
`.claude/docs/coding-standards.md` on 2026-11-11; the repository tree returned
revision `abc0de79d6ceb0b5d7aa693945849b3937afbe6a` during review.
This is independently worded project guidance inspired by that framework.
No upstream scripts, agents, hooks, skills, or configuration are installed.

| Upstream concept | STRAFE 64 implementation |
| --- | --- |
| Studio disciplines | Review perspectives selected for the affected systems |
| Engine setup and onboarding | Existing C/ioquake3 tree, Windows scripts, native DLL deployment |
| Design briefs and acceptance criteria | Small feature briefs and updates to existing design docs |
| Verification before completion | Production-code regressions, staged assets, live observations |
| Visual evidence | Captures inspected at relevant resolutions and renderer settings |
| Tiered delegation | Single-agent work unless delegation is explicitly requested |
| Approval for each write | Continue authorized work; ask only for material decisions or required authorization |
| New project/production directory hierarchy | Existing `docs/`, `tests/`, `scripts/`, and `tools/strafegen/` |

These are agent instructions, not automated enforcement. Existing scripts remain
the executable checks; the upstream slash commands are not available through this adaptation.

## Feature loop

1. **Inspect.** Check the working tree, affected code, active presets, relevant
   design sections, and existing tests. Resolve stale documentation against
   current implementation while preserving the user's requested direction.
2. **Define.** Describe the player action and expected result, what must remain
   stable, and how to observe success. Identify affected clocks, shared state,
   assets, and renderer paths. Ask about substantial creative ambiguity early.
3. **Implement.** Make one coherent change using established code boundaries.
   Add a behavioral regression for a bug or substantial new logic when practical.
   Update existing design/tuning documentation where behavior changes.
4. **Verify.** Run focused checks, build affected native components, stage matching
   modules/assets, and exercise the actual player scenario. Broaden to the full
   regression suite for shared timing, movement, combat, or protocol changes.
5. **Review and deliver.** Inspect evidence against acceptance criteria, fix
   regressions, and name anything still blocked. Preserve useful logs/captures.
   Commit/push when requested, with precise local/remote status.

For a larger feature, use this brief in the relevant design document or task:

```text
Player outcome:
Current behavior -> intended behavior:
Scope and behaviors to preserve:
Systems, clocks, and asset dependencies:
Tuning values (units, bounds, defaults, preset overrides):
Acceptance scenarios (including failure/edge cases):
Verification commands and evidence paths:
Observed results and remaining limitations:
```

Avoid a new planning document for a trivial fix. Use [the docs index](README.md)
to find the existing home for a mechanic, visual rule, or map decision.

## Review perspectives

Use the relevant rows as a checklist. They are responsibilities, not a request
to spawn a team or invent a chain of approvals.

| Perspective | Questions to resolve |
| --- | --- |
| Game design | Does the change support movement/combat decisions? Does it preserve the requested mode and difficulty? |
| Engine/gameplay | Are authority, prediction, time units, state transitions, and network layouts consistent? |
| Level and bot design | Are routes reachable with real collision and movement? Are spawn/landing spaces safe? Do bots select routes naturally? |
| Art and UX | Are threats and routes readable at speed? Do HUD, world, and results preserve their distinct visual roles? |
| Audio | Do cues communicate hits, parries, danger, and state changes? Are fallback formats and slow-motion behavior correct? |
| Performance | What changed in frame time under the same map, bots, camera, resolution, and renderer? |
| QA and delivery | Did the intended build and complete asset set actually run? Which claims have direct evidence? |

When delegation is explicitly requested, specify file ownership, dependencies,
acceptance checks, and expected output for each worker. Coordinate changes to
shared headers centrally. Keep live game sessions serial even when code review
or independent edits run in parallel.

## Evidence appropriate to the change

| Change | Verification |
| --- | --- |
| Movement, combat, timebind | Focused native fixtures plus affected live scenario; check normal and slow time where relevant |
| Bots and traversal | Controlled route fixtures plus a separate natural-play sample; retain counters and failures |
| HUD/menu/VFX | Build and live screenshots; inspect readability, overlap, and relevant resolutions |
| Renderer/shaders | Exercise affected GL2/GL1 paths; inspect images and shader/texture errors |
| Generator/material/packaging | Focused Python checks, regeneration, staged pack inspection, map load and visual review |
| Balance/config | Inspect effective preset overrides and play the affected mode; report observations separately from balance judgments |
| Networking | Relevant multi-client/server scenario; local solo smoke cannot establish multiplayer correctness |
| Documentation/instructions | Check references, command/flag existence, consistency, and diff whitespace |

Follow [tests/README.md](../tests/README.md) for commands and prerequisites,
[the coverage audit](TEST_COVERAGE_AUDIT.md) for known gaps, and
[human playtesting](HUMAN_PLAYTEST.md) for subjective evaluation.
Do not embed historical passing test totals as a permanent completion threshold.

Missing external materials are a dependency failure, even when map geometry loads.
Check which Nocturne variant and packs are installed instead of assuming that
classic, reservoir, and Flow variants share the same requirements.

## Handoff

Report the implemented behavior, verification actually performed, and unresolved
items. Include map/seed, preset, renderer, resolution, bot count, and evidence
paths when they are needed to reproduce a finding. Distinguish automated runtime
observations from human judgments of feel, fun, readability, and balance.

If interrupted, leave a concise checkpoint in the relevant task document with
changed files, completed checks, remaining work, and any owned running processes.
Never publish local smoke profiles, identity keys, or machine-specific settings
as evidence. Preserve existing source and third-party license notices.
