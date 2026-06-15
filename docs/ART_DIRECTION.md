# STRAFE 64 — Art Direction & Visual Guideline

> The creative bible. **What** the game should look and feel like, and **why**.
> For *where each effect lives in code*, see [VISUALS.md](VISUALS.md).

---

## 1. One-line pitch

**A PlayStation-era racing sim, rendered on a dying MAGI defense terminal.**

You are a pilot inside a low-poly world that is collapsing in real time, and
you are *also* the operator watching that collapse on an amber-on-black ops
screen. Speed is the only thing holding the simulation together.

---

## 1b. The four registers

The look is built from four visual "registers." Each owns a job, a palette, and
a fiction. Mixing them *within one element* is the cardinal sin; layering them
on one screen is the whole point.

| Register | Owns | Palette | Mood reference |
|---|---|---|---|
| **VOID** | the world / track | near-black + section pastels | space, REZ, the abyss |
| **CELESTIAL MACHINE** | mechs, menus, hero art, wordmark | white / silver / pale-blue | Eva, Armored Core white, winged mecha-angel |
| **MAGI OPS** | realtime HUD, alerts | amber → red | NERV defense terminal |
| **AC DATA** | loadout / results / spec screens | blue + cyan, technical | Armored Core stat readout |

VOID is where you *are*. CELESTIAL MACHINE is what you and your weapons *are
made of* — clean, white, angelic hardware against the black. MAGI OPS is the
operator watching the run. AC DATA is the engineer reading the spec.

---

## 2. The three pillars

Every visual decision must serve at least one. If it serves none, cut it.

1. **READ AT SPEED.** You are moving at 320–960 u/s. Anything that matters —
   the next ledge, the mechanic of a section, the rising void, your velocity —
   must be legible in a quarter-second glance. N64 clarity, not realism.
2. **SPEED = LIFE, STILLNESS = DEATH.** The screen rewards momentum and
   visibly *decays* when you stop. Color, saturation, FOV, HUD presence and
   audio all read off one `CG_Flow` [0,1] value. The world literally dies if
   you stand still.
3. **TWO LENSES, ONE FRAME.** The **world** is a PSX/N64 simulation (diegetic,
   inside the fiction). The **HUD** is a NERV/MAGI operator overlay
   (non-diegetic, watching from outside). They are *intentionally* different
   media layered on the same screen — never blend them.

---

## 3. Resolving the core tension

There has been a standing question of whether the chunky **PSX/N64 world** and
the **NERV/MAGI amber terminal HUD** clash. **They do not — they are two
different things and must stay that way.** This is the art direction:

| | World layer | HUD layer |
|---|---|---|
| Fiction | The simulation you pilot | The terminal monitoring it |
| Era cue | PlayStation 1 / N64 (1996–98) | Evangelion MAGI ops (amber CRT) |
| Color | Saturated vertex-lit pastels | Amber → red ops palette |
| Geometry | Flat-shaded, affine-warped polys | Crisp vector text, thin frames |
| Behaviour | Decays as flow drops | Escalates as danger rises |

The player is simultaneously **inside** (pilot) and **above** (operator) the
run. Keep the membrane between them sharp. The HUD never adopts texture warp;
the world never adopts amber terminal type. Their only shared language is the
**near-black background** and **alert-red under threat**.

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
angelic hardware, lit against the black. This is the bright counterweight to
the VOID: the world is dark, but the *machine you pilot* is luminous. Eva-white
plating, Armored Core gloss, winged-mecha-angel silhouettes with halos. Where
the world is matte and flat, the machine reads as smooth, reflective, and
*important*. Keep player/weapon assets in this register so they never get lost
against a section's vertex color.

### 4.2 The world background

- **Sky:** near-black `0.015, 0.022, 0.028` (`#040607`) with procedural
  scrolling starfield + faint nebula. The void is space; the track is the only
  lit thing.
- **Void plane (the kill plane):** `1.00, 0.08, 0.28` (`#FF1447`) — the same
  alert-red family as `nerv_red`. The threat reads the same whether you see it
  in the world or on the HUD. This is the one deliberate bridge between layers.

### 4.3 Section palettes (world geometry)

The world is **vertex-colored flat geometry** — the *mechanic of a section is
encoded in its color* so you read what to do before you arrive. Authoritative
(from `strafegen.py`, 0–255):

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

### 4.4 Weapon color (neon-PSX signatures)

Each weapon owns a saturated neon dlight/muzzle color so the projectile reads
against the dark world: Slipstream cyan-white, Arc electric-blue, Tracer
pale-yellow, Choke orange, Lance amber-red, Skipper green, Wake magenta,
Vectorgun void-blue, Overdrive green-white. **Rule:** weapon light is *neon and
self-illuminated* — it is the brightest thing on screen at the moment of fire.

---

## 5. Typography

- **HUD type:** Share Tech Mono (OFL), loaded via the engine TrueType path
  (`baseoa/fonts/strafe64.ttf`). Monospace, technical, terminal. Always drawn
  with a 1px drop shadow so frameless text stays legible over any world color.
- **Wordmark:** `STRAFE 64` with a chromatic split — amber base with cyan/red
  offset copies. This is the *only* place the world's PSX color-fringing and
  the HUD's amber meet by design; it is the logo, the seam made into identity.
- **Numbers are the hero.** Velocity, timer, void distance. Big, monospace,
  amber. Everything else is a label and stays small/dim.
- **Voice:** terse ops shorthand — `VEL`, `SYNC`, `HOP`, `- ELAPSED -`,
  `TARGET`, `VOID COLLAPSE 240m`, `SYS 6.66 // NERV // PROJECT No.666`.
  Uppercase, bracketed, clinical. Never conversational.

---

## 6. The render path (PSX/N64 crunch)

The world is rendered low-fi *on purpose*. The look:

- **Point-sampled textures** (`GL_NEAREST`) — chunky pixels, no smoothing.
- **16-bit color banding**, no anti-aliasing, vertex lighting only.
- **Affine texture mapping** (`r_psx 1`) — non-perspective-correct, the
  signature PSX texture *swim/warp* on near surfaces. (Default subtle/off —
  see §9.)
- **Flat geometry, hard edges.** No smooth normals, no rounded detail. Forms
  are blocky and readable, lit by their vertex color.

Aspired (GL2 FBO work, not yet shipped): screen-space vertex jitter and a
low-res framebuffer upscale for true chunky-pixel output.

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
affine warp — those belong to the live run. A results screen is the autopsy:
still, blue, precise.

---

## 9. Restraint clauses (the "don'ts")

The lo-fi/CRT/glitch toolkit is powerful and easy to overdo. Hard limits:

- **Affine warp stays subtle.** The full PSX texture-swim was judged too harsh
  and is defaulted off. Keep the PSX vibe present but not nauseating.
- **Glitch = signal, never ambience.** Datamosh/tears only fire on real
  distress (corruption term). A clean fast run is *clean*.
- **No HUD frames at rest.** Brackets and plates only where escalation
  justifies them.
- **Don't blend the two lenses.** No amber terminal type baked into world
  geometry; no affine warp on HUD text. The wordmark is the only sanctioned
  crossover.
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
- PlayStation 1 / Nintendo 64 (1996–98) — flat-shaded polys, affine texture
  swim, point-sampled chunk, vertex-lit pastels against black. **(VOID
  register)**
- *Mirror's Edge* — near-empty HUD, color-coded readability, momentum as the
  whole game.
- *REZ / Vib-Ribbon / Thumper* — abstract neon-on-void, rhythm-coupled motion,
  the track as the only lit object in space.
- **Breakcore / jungle label art** (Breakstation et al.) — icy blue fanged
  feminine faces, high-contrast cold imagery; the music's visual cousin.

If a new asset or effect doesn't sit naturally on that board, it's off-brand.

---

*Maintainer note: this file is the intent. When you change a palette value,
font, or threshold in code, update the authoritative tables here so this stays
the single source of truth for the look.*
