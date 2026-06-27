# STRAFE 64 — Art Direction & Visual Guideline

> The creative bible. **What** the game should look and feel like, and **why**.
> For *where each effect lives in code*, see [VISUALS.md](VISUALS.md).

> **2026 pivot — REALISM.** The house style is now **surrealist ultra-real
> brutalist concrete** under a hard **Bryce-3D** sky: monolithic greebled
> structures, a low raking sun, long shadows, a filmic GL2 render path. The
> earlier **PSX/N64 low-fi** look is *retired* as the default — kept only as a
> selectable nostalgia preset (`run.sh -p` / `--theme default`). What survives
> the pivot is everything that was *gameplay* identity, not render-fi: the flow
> system, the NERV/MAGI HUD, the rising void, section-colour-as-verb, neon
> weapon light, tracker music. Those sections below are unchanged; the *world
> layer* is what changed.

---

## 1. One-line pitch

**An impossible brutalist megastructure, raced at lethal speed, watched from a
MAGI defense terminal.**

You are a pilot threading a vast concrete structure that should not be able to
stand — slabs the size of city blocks, a sun frozen low on the horizon, shadows
that go on forever — and you are *also* the operator watching that run on an
amber-on-black ops screen. The world is rendered *ultra-real*; its **scale and
arrangement are the dream**. Speed is the only thing holding the run together.

---

## 1b. The four registers

The look is built from four visual "registers." Each owns a job, a palette, and
a fiction. Mixing them *within one element* is the cardinal sin; layering them
on one screen is the whole point.

| Register | Owns | Palette | Mood reference |
|---|---|---|---|
| **STRUCTURE** | the world / track | grey brutalist concrete + section accents | brutalism, Bryce 3D, surreal megastructure |
| **CELESTIAL MACHINE** | mechs, menus, hero art, wordmark | white / silver / pale-blue | Eva, Armored Core white, winged mecha-angel |
| **MAGI OPS** | realtime HUD, alerts | amber → red | NERV defense terminal |
| **AC DATA** | loadout / results / spec screens | blue + cyan, technical | Armored Core stat readout |

STRUCTURE is where you *are* — ultra-real concrete mass, sun-shadowed and
weathered, arranged at impossible scale. CELESTIAL MACHINE is what you and your
weapons *are made of* — clean, white, angelic hardware against the grey. MAGI
OPS is the operator watching the run. AC DATA is the engineer reading the spec.

> **STRUCTURE replaces the old VOID register.** VOID was a near-black space
> backdrop with vertex-lit pastels; the world is now a daylit concrete
> megastructure. The section accent hues (§4.3) survive — they're now the only
> saturated colour against the grey, which makes them read *harder*, not softer.
> The rising **void plane** (the red kill-floor, §4.2) keeps its name and job.

---

## 2. The three pillars

Every visual decision must serve at least one. If it serves none, cut it.

1. **READ AT SPEED.** You are moving at 320–960 u/s. Anything that matters —
   the next ledge, the mechanic of a section, the rising void, your velocity —
   must be legible in a quarter-second glance. **Realism serves readability,
   never fights it:** bold brutalist masses, hard sun-shadow that models the
   form, section accents that pop against grey concrete. The moment weathering
   or detail hides the next ledge, it's wrong.
2. **SPEED = LIFE, STILLNESS = DEATH.** The screen rewards momentum and
   visibly *decays* when you stop. Color, saturation, FOV, HUD presence and
   audio all read off one `CG_Flow` [0,1] value. The world literally dies if
   you stand still.
3. **TWO LENSES, ONE FRAME.** The **world** is a surreal *ultra-real* brutalist
   simulation (diegetic, inside the fiction). The **HUD** is a NERV/MAGI
   operator overlay (non-diegetic, watching from outside). They are
   *intentionally* different media layered on the same screen — never blend
   them. A photoreal concrete megastructure under a clinical amber terminal is
   the *strongest* version of this contrast, not the weakest.

---

## 3. Resolving the core tension

There has been a standing question of whether the **realist brutalist world**
and the **NERV/MAGI amber terminal HUD** clash. **They do not — they are two
different things and must stay that way.** This is the art direction:

| | World layer | HUD layer |
|---|---|---|
| Fiction | The simulation you pilot | The terminal monitoring it |
| Cue | Brutalist concrete + Bryce-3D surrealism | Evangelion MAGI ops (amber CRT) |
| Color | Grey concrete + section-accent pops | Amber → red ops palette |
| Geometry | Greebled mass, hard sun-shadow, real depth | Crisp vector text, thin frames |
| Behaviour | Decays as flow drops | Escalates as danger rises |

The player is simultaneously **inside** (pilot) and **above** (operator) the
run. Keep the membrane between them sharp. The HUD never adopts photoreal
texture; the world never adopts amber terminal type. Their only shared language
is **alert-red under threat** (the void plane and the klaxon HUD share it).

---

## 4. Color

### 4.1 The MAGI ops palette (HUD)

Single amber→red ops ramp ties every readout together. These are the
authoritative values (from `cg_draw.c`):

| Token | RGB (0–1) | Hex | Meaning |
|---|---|---|---|
| `nerv_amber` | 1.00, 0.60, 0.06 | `#FF990F` | nominal / default readout |
| `nerv_orange` | 1.00, 0.42, 0.05 | `#FF6B0D` | elevated / climbing |
| `nerv_red` | 1.00, 0.12, 0.16 | `#FF1F29` | alert / klaxon (flashes) |
| `nerv_green` | 0.40, 1.00, 0.50 | `#66FF80` | nominal-GO / ghost target |
| `nerv_dim` | 0.55, 0.42, 0.20 | `#8C6B33` | idle / inactive |
| `nerv_fill` | 0.02, 0.03, 0.04 @ .55α | `#050708` | panel backing plate |

**Rule:** amber is rest, orange is rising, red is danger, green is *go / your
best*. Color *is* the state — never use red for something that isn't a threat,
never use green for anything but success and the ghost target.

### 4.1b The AC-data palette (loadout / results / spec screens)

The **blue** register, borrowed from Armored Core stat readouts. Used for
non-realtime screens — loadout, run results, weapon spec — and never for live
threat. Blue means *data / performance / cold hardware*, the inverse of amber's
*operational alert*.

| Token | RGB (0–1) | Hex | Meaning |
|---|---|---|---|
| `ac_blue`  | 0.45, 0.70, 1.00 | `#73B3FF` | primary data / labels |
| `ac_steel` | 0.62, 0.72, 0.85 | `#9EB8D9` | secondary / inactive |
| `ac_cyan`  | 0.55, 0.95, 1.00 | `#8CF2FF` | highlight / best stat |
| `ac_white` | 0.92, 0.96, 1.00 | `#EBF5FF` | celestial-machine white |

**Rule:** amber and blue never appear as *peers* in the same readout. A
realtime HUD is amber (ops). A data screen is blue (engineering). The
transition between them (entering results) is a deliberate scene change, not a
blend.

### 4.1c The celestial-machine white

The mechs, weapons, and hero art are **white/silver/pale-blue** — clean
angelic hardware. This is the bright counterweight to the grey STRUCTURE: the
world is matte concrete, but the *machine you pilot* is luminous and smooth.
Eva-white plating, Armored Core gloss, winged-mecha-angel silhouettes with
halos. Where the world is rough and weathered, the machine reads as polished,
reflective, and *important*. Keep player/weapon assets in this register so they
never get lost against the concrete or a section's accent.

### 4.2 The world background

- **Sky:** a hard **Bryce-3D daylight** — a real photographed sky (baked cube,
  see [the photo-bake recipe](../tools/strafegen/skytex_src/README.md)) or, as
  the always-present procedural fallback, a wide gradient with a low raking sun
  and a mountain/horizon ridgeline. The sun sits **low** on purpose: long
  shadows model the brutalist mass, and the frozen low sun reads as *surreal*,
  not noon-bland. The GL2 directional sun (`q3gl2_sun`) is aimed to agree with
  wherever the sky's sun is, so cast shadows and the painted sun never disagree.
- **Void plane (the kill plane):** `1.00, 0.08, 0.28` (`#FF1447`) — the same
  alert-red family as `nerv_red`. The threat reads the same whether you see it
  in the world or on the HUD. This is the one deliberate bridge between layers.

### 4.3 Section palettes (world geometry)

The world is **vertex-coloured geometry over one concrete material** — the
*mechanic of a section is encoded in its colour* so you read what to do before
you arrive. The concrete theme repaints bulk floors/walls grey but **leaves the
accents vivid**, so against the grey these hues pop *harder* than they did on
the old pastel world. Authoritative (from `strafegen.py`, 0–255):

| Section | RGB | Reads as |
|---|---|---|
| START | 150, 255, 150 | green — go |
| GAPS (jump) | 255, 215, 140 | warm amber — air |
| BHOP | 140, 225, 255 | cyan — keep hopping |
| SLIDE | 255, 150, 150 | red-pink — get low |
| WALLS | 225, 150, 255 | magenta — walljump |
| TOWER | 255, 255, 150 | yellow — vertical |
| FINISH | 150, 255, 220 | mint — end |
| PLAIN | 205, 205, 205 | neutral grey |
| DANGER | 255, 95, 95 | hot red — hurt |

**Rule:** a section's color is a *verb*. Same mechanic = same hue across every
generated map, forever. Players learn the vocabulary once. Do not reuse a hue
for an unrelated mechanic.

### 4.4 Weapon color (neon signatures)

Each weapon owns a saturated neon dlight/muzzle color so the projectile reads
against the grey concrete world: Slipstream cyan-white, Arc electric-blue,
Tracer pale-yellow, Choke orange, Lance amber-red, Skipper green, Wake magenta,
Vectorgun void-blue, Overdrive green-white. **Rule:** weapon light is *neon and
self-illuminated* — it is the brightest thing on screen at the moment of fire,
and against matte concrete + hard shadow it reads even hotter.

---

## 5. Typography

- **HUD type:** Share Tech Mono (OFL), loaded via the engine TrueType path
  (`baseoa/fonts/strafe64.ttf`). Monospace, technical, terminal. Always drawn
  with a 1px drop shadow so frameless text stays legible over any world color.
- **Wordmark:** `STRAFE 64` with a chromatic split — amber base with cyan/red
  offset copies. The chromatic fringe is a deliberate HUD-layer flourish (a
  lens/CRT artefact on the operator's terminal), never bled into the realist
  world; it is the logo, the seam made into identity.
- **Numbers are the hero.** Velocity, timer, void distance. Big, monospace,
  amber. Everything else is a label and stays small/dim.
- **Voice:** terse ops shorthand — `VEL`, `SYNC`, `HOP`, `- ELAPSED -`,
  `TARGET`, `VOID COLLAPSE 240m`, `SYS 6.66 // NERV // PROJECT No.666`.
  Uppercase, bracketed, clinical. Never conversational.

---

## 6. The render path (Bryce-3D realism)

The world is rendered *ultra-real on purpose* — the surrealism is in the **scale
and arrangement**, not the fidelity. This is the GL2 (rend2) path, shipped by
default via [`realism.cfg`](../tools/strafegen/realism.cfg) (mirrored per-map by
`strafegen_gfx.render_cfg()`). The look:

- **Directional sun + cascaded shadow maps** (`q3gl2_sun`, `r_sunShadows`,
  `r_shadowMapSize 4096`). A **low** sun throwing **long, hard shadows** that
  carve the brutalist mass — the single biggest contributor to the look.
- **Filmic tonemap, crushed blacks** (`r_forceToneMap`, fixed `r_cameraExposure`
  with auto-exposure off). Dark concrete vs blown sky/neon — punch, not a flat
  grey wash.
- **PBR-lite surfaces** (`r_normalMapping` / `r_specularMapping` /
  `r_parallaxMapping 2` relief). Greebled panels and the `hull` material gain
  real depth and a moving highlight, not a painted-on fake.
- **Crisp textures, HDR** (trilinear + `r_ext_max_anisotropy 16`, full-res
  picmip, 32-bit, MSAA, `r_hdr 1`). The inverse of the old point-sampled crunch.
- **Photoreal concrete + sky** when baked (`skytex/` via `bake_assets.sh`);
  otherwise the procedural Bryce sky + generated concrete tile render the same
  shape at lower fidelity. Either way the map is whole.

Brutalist **greeble** (`strafegen_geom.greeble_course`) erodes the big masses
into stacked-cube clusters so the silhouette reads as eroded concrete, not a
smooth slab — it's the geometry half of the look, the sun-shadow is the other.

> The legacy **PSX/N64 crunch** (`psx.cfg`: `GL_NEAREST`, 16-bit banding, affine
> `r_psx`, vertex-only) is retired as the default but kept intact as a
> selectable preset — `run.sh -p`. It is no longer the house style.

### 6.1 Surrealist Bryce — the target (next iteration)

The realism path + greeble + low sun already deliver the brutalist Bryce base.
The *surrealist* register deepens it; these want the live render loop to tune,
so they're the documented next steps, not yet shipped:

- **Impossible scale.** Slabs and monoliths far larger than the play space,
  read against the horizon — the eye can't reconcile the size. (A decorative,
  non-colliding monolith-skyline pass, analogous to `greeble_course`.)
- **Reflective infinite plane.** A Bryce signature — a vast still reflective
  floor/water plane at the world base (env-mapped `chrome`-style shader), used
  for backdrops only so gameplay readability is untouched.
- **The frozen low sun + dramatic sky palette.** Push `_build_synthsky`'s sun
  lower/larger and the gradient toward surreal dusk-violet → cyan for the
  dreamlike Bryce horizon.

---

## 7. Feedback & juice (the flow system)

All "speed = everything" channels read off one client value, `CG_Flow` [0,1]
(0 at run speed 320, 1 at the 960 ceiling, nudged by bhop streak):

- **Flow wash** — a cold fullscreen grade drains the world toward grey as flow
  drops. *The world dies if you stop.* (Tuned subtle: ~0.28 max.)
- **Stillness contrast** (`CG_DrawStillness`) — the dead state must not read as
  a flat grey film. As momentum bleeds, a **cold void-blue vignette** creeps in
  from the edges (depth, not a uniform veil), and near a true standstill a
  **warm amber→red alert rim breathes at ~1 Hz** — the one warm, moving thing
  in a cold static frame. Warm-vs-cold + motion-vs-static is the contrast; it
  reads as a NERV low-power klaxon: *MOVE.* The vignette deepens with lost flow;
  the warm rim only ignites in the bottom ~45% and reddens the closer you are
  to stopped.
- **FOV** — widens with flow; speed feels like speed.
- **Speedlines** — appear above a flow threshold (~0.35), low density/alpha.
- **HUD presence** — the speedometer fades *in* with flow and is hidden at
  rest. The number you don't need when standing still isn't there.
- **Combo / FLOW** — style multiplier escalates amber→orange→red.
- **Strafe meter** (`CG_DrawStrafeMeter`) — the one piece of *teaching* HUD.
  The whole skill ceiling is the air wishspeed cap (`pm_wishSpeedClamp = 30`):
  while A/D-strafing you only gain speed when your velocity↔wishdir angle
  exceeds `acos(30/|v|)`, optimal at exactly 90°. A strip under the crosshair
  shows velocity heading (center), the fixed ±90° targets, a **red dead-zone
  that visibly widens as you speed up** (the ceiling made literal), and a
  needle that goes green when you're gaining. Only appears airborne mid-strafe
  — the moment the cap *is* the game. Toggle `cg_strafeHelper`.

**Glitch layer** — `CG_Corruption` [0,1] = max(momentum loss, void proximity,
damage spike) drives datamosh macroblocks + scanline tears + chromatic flash.
This is *distress*, not decoration — it fires when you are losing the run.

---

## 8. The HUD philosophy — Mirror's Edge minimalism

The HUD has converged on **clean and flow-focused**, not a cluttered ops
dashboard. Frames (`CG_NervPanel`) are gone; text floats with a drop shadow.

- Speedometer = just the velocity number, fades in with flow.
- Vitals = small health number + tinted cross, bottom-left only. Armor only if
  >0. Ammo only for finite weapons.
- No frag scores, no 3D attacker head, no status-bar clutter.

**Rule:** the default screen is *almost empty*. The HUD earns its pixels by
escalation — it gets louder and redder only as danger (void, low health,
alert) rises. A calm run is a quiet screen.

---

## 8b. Data screens — the Armored Core register

Non-realtime screens (loadout, run results, weapon select, leaderboard) speak a
different dialect from the in-run HUD: **blue, technical, spec-sheet.** This is
where the game shows you *numbers about yourself*, the way Armored Core shows
an AC's performance.

- **Spider / radar graph.** Run results plot performance axes — TOP SPEED,
  AVG FLOW, AIRTIME, STYLE, BHOP CHAIN, CLEANLINESS — as an Armored-Core-style
  polygon. The shape of your run, at a glance. Cyan fill, blue grid.
- **Stat blocks.** Monospace label/value columns (`TOP VEL  961`,
  `SYNC  1.78`, `CHAIN  ×24`) in `ac_blue`, best-ever values flagged
  `ac_cyan`. Japanese-style technical sub-labels are welcome as flavor.
- **Spec-sheet annotation.** Weapon/loadout screens use the model-kit callout
  language from Gundam/AC manuals — the asset centered, thin leader lines to
  labeled parts, a dense technical caption block. Clinical, catalogued,
  over-specified on purpose.
- **The wordmark and menus** live here too: white celestial machine over near-
  black, blue/amber chromatic accents, the `SYS 6.66 // NERV` ops footer.

**Rule:** data screens are *calm and exact*. No glitch, no flow wash, no
chromatic tear — those belong to the live run. A results screen is the autopsy:
still, blue, precise.

---

## 9. Restraint clauses (the "don'ts")

Realism + greeble + glitch is a powerful toolkit and easy to overdo. Hard limits:

- **Realism serves readability.** Weathering, pitting, parallax depth and
  reflections are welcome *until* they hide the next ledge or muddy a section
  accent. If detail competes with the run line, the run line wins — cut it.
- **Accents stay vivid.** The concrete theme greys the bulk but **never** the
  section/start/finish/hazard accents (§4.3). They are the only saturated colour
  and the whole readability system rides on them.
- **Surreal scale, not surreal noise.** The dream is *bigger-than-possible* mass
  and a frozen low sun — clean, monumental, legible. It is not random clutter,
  melting textures, or a busy skybox. One impossible idea per vista.
- **Glitch = signal, never ambience.** Datamosh/tears only fire on real
  distress (corruption term). A clean fast run is *clean*.
- **No HUD frames at rest.** Brackets and plates only where escalation
  justifies them.
- **Don't blend the two lenses.** No amber terminal type baked into world
  geometry; no photoreal texture or chromatic fringe on HUD text. The wordmark
  is the only sanctioned crossover.
- **One meaning per color.** Red is always threat. Green is always go/best.
  Section hues are fixed verbs.

---

## 10. Audio identity (for completeness)

Native tracker-module music (`.it/.xm/.s3m/.mod/.mptm` via libopenmpt). The
genre lane is specific — fast, textured, breakbeat-driven:

- **Atmospheric Jungle** — the core. Chopped amens over deep pads.
- **Breakcore** — peak intensity / high-flow / danger moments.
- **Liquid DnB** — smooth, flowing sections; the "in the zone" feel.
- **Intelligent DnB** — menus, loadout, the cerebral data-screen mood.
- **Ambient DnB** — practice / low-stakes / between-run calm.

The music is part of the flow loop: it is the pulse you ride, and its energy
should track the register on screen (Breakcore for the void closing, Ambient
for a calm menu). See [VISUALS.md §Tracker-module music].

---

## 11. Reference mood board (in words)

- *Evangelion* MAGI / NERV defense screens — amber+green radial defense
  terminal, "TIME REMAINING TO COLLAPSE", "PROTECT No.666", klaxon-red under
  pressure, clinical ops typography. **(MAGI OPS register)**
- *Armored Core* — AC PERFORMANCE stat screens, blue spider/radar graphs,
  white gloss mechs, dense spec-sheet readouts. **(AC DATA + CELESTIAL
  MACHINE registers)**
- White / silver **mecha-angel** art — winged machines, halos, Eva-white
  plating, fragmenting feminine faces in starfields. Luminous hardware against
  black. **(CELESTIAL MACHINE register)**
- *Metal Gear Solid* — octocamo stealth greens, technical HUD restraint,
  cold-war ops mood.
- **Brutalist architecture** — Boston City Hall, the Barbican, Tadao Ando
  poured concrete, board-formed slabs and panel seams. Monolithic, heavy,
  honest material. **(STRUCTURE register)**
- **Bryce 3D / surreal CGI landscapes** — the frozen low sun, the infinite
  reflective plane, fractal terrain on the horizon, that unmistakable
  "rendered" stillness. Plus de Chirico's empty plazas and long shadows,
  Beksiński's monolith dread, *Inception*'s folding city. **(STRUCTURE
  register — the surreal half)**
- *Mirror's Edge* — near-empty HUD, color-coded readability, momentum as the
  whole game.
- *REZ / Vib-Ribbon / Thumper* — abstract neon, rhythm-coupled motion, the
  hot self-lit projectile against a quiet world.
- **Breakcore / jungle label art** (Breakstation et al.) — icy blue fanged
  feminine faces, high-contrast cold imagery; the music's visual cousin.

If a new asset or effect doesn't sit naturally on that board, it's off-brand.

---

*Maintainer note: this file is the intent. When you change a palette value,
font, or threshold in code, update the authoritative tables here so this stays
the single source of truth for the look.*
