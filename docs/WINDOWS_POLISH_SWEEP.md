# Windows polish sweep — 2026-10-27

Continues [the texture sweep](WINDOWS_TEXTURE_SWEEP.md) after integrating
origin/master. The timed polish pass ran from 10:46 to 11:46 UTC. All changes are local;
no commits or pushes were made for these fixes.

## Confirmed problems and changes

### Stability and transitions

- **Killcam disconnect:** reproduced `CL_GetServerCommand: a reliable command
  was cycled out` within seconds of a death. Per-render-frame timescale changes
  flooded the reliable system-info stream. The server now coalesces system-info
  updates to simulation ticks, keeping pending changes until the next tick.
- **Settings overwritten by the killcam:** temporary grading and time settings
  previously reverted to hardcoded defaults. The killcam now snapshots the
  actual settings and restores them through an allowlisted recovery journal.
  A pending journal is recovered on cgame initialization. A synthetic interrupted
  journal test is distinct from an actual crash-recovery test.
- **Spectator slow-motion lock:** leaving play could strand the clock at 0.05.
  The clock owner now releases time and restores the prior movement mode on
  spectator/intermission/disconnect transitions. Death-to-spectator also releases
  the killcam's saved movement-derived clock value. Other spectators cannot
  release an active pilot's clock.

### Text, materials, and presentation

- **Font cache exhaustion:** generated fonts lacked their cache identity, and
  capacity rejection happened before reuse lookup. Both are corrected.
- **Block glyphs after saved-font reload:** descriptive atlas paths exceeded the
  32-byte glyph shader name. Short versioned hashed names now fit and distinguish
  full font paths and point sizes. Available custom TTFs take precedence over
  size-only legacy caches; legacy-only packs remain supported.
- **Font resource handling:** release FreeType faces and file buffers on both
  success and failure; reject glyph sizes that cannot fit the atlas.
- **Readability:** reduced the fresh-profile slow-motion DoF maximum from 14 to
  6 pixels after a same-view comparison. Saved overrides remain intact. The
  respawn prompt has a dark backing, idle combo indicators fade, and arena BACK
  no longer crowds its footer. Subtitle separators are consistent.
- **Scoreboard:** larger text and brighter secondary labels improve 800×600
  readability. Narrow team panels show names/scores without overlapping extra
  columns, and requesting the scoreboard suppresses the overlaid death report.
- **Identity:** fresh profiles use PILOT; scripted opponents have distinct
  callsigns. The default startup no longer queues bundled base-game cinematics.

### Music and launch behavior

- Unsupported codec extensions now fall back to the same basename, rather than
  looking for names such as `track.mp3.ogg`.
- The engine publishes compiled codec support; playlists filter accordingly.
  Trailing directory separators are normalized, and single-track playlist
  arguments work. Curated music selection checks installed files; missing optional
  playlists/tracks fall back to the installed soundtrack. This Windows build
  queues seven bundled OGG tracks without missing-open warnings.
- Arena setup lists only installed maps with navigation data and disables FIGHT
  if none exist. Its bullet-time setting now wins over `exec arena`. Opponent
  count is stored independently from automatic bot population, with enough
  client slots and an exact named roster.
- Arena presets clear inherited Lattice/void modes; course launches clear
  inherited automatic bots and team game types. Lattice explicitly seats its
  full starting roster, excludes observers from bracket seeding, waits for queued
  bots, and measures connection grace in real time. Delayed bot joins likewise
  use real time so slow motion does not stretch a short join delay into a minute.

### Content tools

- Shared packs are written to a temporary file and atomically replaced. An
  injected write failure preserves the previous complete pack.
- Generator help/log output no longer aborts under Windows legacy encodings.
- Windows staging includes Lattice, surf, and other map prefixes.
- Decorative platform undersides could embed a Lattice player spawn in solid
  geometry. Both decoration passes now discard new blocks overlapping protected
  spawn volumes. Base geometry and decoration RNG sequencing remain unchanged.
- Generated and staged the missing Practice and Lattice maps, plus marble and
  granite material maps, with AAS navigation.

## Verification

Focused checks and reproduction commands are in [tests/README.md](../tests/README.md).

- MSVC x64 Release engine, both renderers, and native game/UI DLLs build.
- Native font, codec, arena-menu, and time-bind regressions pass.
- Packaging/freshness/failed-write checks pass. Spawn-decoration validation passes
  16 theme/seed/density combinations, preserving all 24 Lattice spawns.
- Existing standalone engine API checks: 40 passed, 0 failed.
- Generator self-test: 28 maps generated and validated. All 12 generated packs
  pass ZIP CRC checks.
- Ten-minute four-bot arena soak completed without an engine error.
- Ten scripted death/respawn cycles completed without reliable-command overflow;
  custom saturation 1.31, bloom 0.12, and disabled time-bind were retained.
- Real menu-generated launch commands retained bullet-time OFF and all seven
  named bots at both status checks, rather than having automatic population
  change the roster.
- Synthetic interrupted-killcam journal restored bloom 0.11 and time-bind OFF,
  then cleared the journal. Direct spectator and death-to-spectator checks restored
  timescale 1.
- Main and arena menu captures were visually inspected at 800×600. Marble GL2
  at 1280×720 and granite GL1 at 800×600 load without missing-image/shader warnings.
- Renderer restart with saved font data reloads the game without an engine error.
- Six-minute final Lattice run seeded five pilots, completed a bracket, and
  exited normally. A separate observer run seeded four bots without the observer.
- Final FFA/death and two-team scoreboard captures were visually checked at
  800×600. Final staging succeeded; all three deployed native game DLL hashes
  match the Release build, and all 12 deployed packs match their generated
  sources, including all eight generated maps.

Selected local evidence (ignored output, relative to `engine/build`):

| Check | Directory |
|---|---|
| Arena soak | `polish-arena-stable-1793099057824711100` |
| Death/respawn cycles | `polish-killcam-cycles-1793099724472410200` |
| Seven-bot menu launch | `polish-arena-menu-launch-1793100285059924300` |
| Main menu | `polish-main-menu-1793100075914973700` |
| Arena spacing | `polish-arena-menu-final-1793100380531162900` |
| Marble GL2 | `polish-marble-gl2-1793100155062743300` |
| Granite GL1 | `polish-granite-gl1-1793100162864055800` |
| DoF comparison | `polish-dof-compare-1793100234414595600` |
| Saved-font renderer restart | `polish-renderer-restart-1793100683272433900` |
| Death-to-spectator recovery | `polish-death-spectator-fixed-1793100755357272600` |
| Final Lattice soak | `polish-lattice-final-1793100954559298100` |
| Observer exclusion | `polish-lattice-observer-1793100964949906600` |
| Readable death scoreboard | `polish-scoreboard-final-1793101411102002600` |
| Readable team scoreboard | `polish-scoreboard-team-final-1793101480210768500` |

Each smoke directory contains `commands.log`, `home/baseoa/qconsole.log`, and
`home/baseoa/screenshots`. Build/staging logs are `windows-polish-build.log` and
`windows-polish-stage.log` at the repository root.

## Limits

Validation is local on Windows with Intel Iris Xe and unbaked generated maps.
This is not an exhaustive gameplay, multiplayer, other-GPU, or baked-lightmap
audit. Audio file selection/decoding was checked through engine logs, not a
listening evaluation. MP3/tracker codecs remain disabled in this Windows build;
the fix selects supported installed content. Existing compiler warnings remain.
Normal player settings were preserved; aesthetic default changes apply to fresh
settings unless the player adopts them explicitly.
