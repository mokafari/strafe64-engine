# Arena FPS Map Design — Research
> Research for STRAFE 64 procedural arena tuning. Sources cited at bottom.

Distilled from competitive arena-FPS design (Quake 3 / CPMA / QuakeLive, Diabotical,
Reflex Arena, Unreal Tournament) and the level-design canon. Numbers are in **Quake
units (u)**; our player is ~**56u tall, 15u radius**, step height **18u**, and our
movement constants are run **320 ups**, jump **300 / apex ~46u**, double-jump ledge
**~92u**, gravity **1000**. Killbox shell today: **~2816u wide, ~1600u tall**, catwalk
at **~400u**.

## TL;DR
- **Connectivity beats geometry.** A great arena is a *graph* first: every key node
  (each item, each spawn cluster) has **2–3 routes** in/out and the whole map forms
  **circulation loops** — no dead ends, no single-file corridors that trap a runner.
- **Design the "rotation."** Top items (mega, red armor, powerup) are spaced so a
  player *touring* them traces a loop whose lap time ≈ the item clock. Control = being
  the one who completes the rotation; that is the whole metagame.
- **Item timing is the clock of the match.** Q3 canon: **mega health respawns 35 s**,
  **red/yellow armor 25 s**, **powerups (quad) 120 s**, weapons 5 s, ammo 40 s. Placement
  + timing, not kills, create map control.
- **Scale to the verbs.** Combat rooms **512–1024u** across; doorways **96–128u** wide /
  **128u+** tall; corridors **128–192u**. A space must be wide enough to dodge at 320+ ups
  and tall enough (**≥128u**, ideally **256u+**) for jumps/air fights.
- **Verticality is the skill ceiling.** Multi-level rooms with pads/teleporters/ramps put
  the z-axis in play; the best duel maps cycle floor ↔ balcony constantly.
- **Mix sightline lengths.** Pair long "power" lanes with short broken ones; never allow a
  single sightline that covers the whole map. Cover cadence every **~256–512u**.
- **Jump pads are commitment.** They give a *predictable, readable arc* and a moment of
  vulnerability; place them as connective tissue between tiers, never as the only route.
- **Teleporters trade position for the unknown.** Useful to close loops and escape, but
  exits must be safe (no telefrag, not staring into a power lane) and signposted.
- **Symmetry for fairness, asymmetry for character.** Competitive 1v1/2v2 favors mirror or
  rotational symmetry; FFA tolerates asymmetry. Either way, **spawns** must be plural,
  spread, away from items and out of immediate sightlines.
- **Small for duels, big for FFA.** 1v1 maps (Aerowalk, Blood Run, Hub) are compact, tall,
  loop-heavy; FFA wants more rooms and item nodes so the crowd disperses.

## Topology & Connectivity
The single most repeated principle in arena design: **think in graphs, not rooms.**
Nodes = items / spawns / arena foci; edges = traversal routes (corridors, pads, jumps,
teleporters). Good arenas satisfy:
- **No dead ends.** Every node has ≥2 exits. A dead end is a death trap for a fast player
  and a camp spot for a slow one.
- **Multiple routes (2–3) between any two key nodes**, with different *character* (a fast
  open lane, a tight flank, a vertical shortcut). This is what enables "outplaying" — you
  can disengage, re-angle, and re-approach.
- **Circulation loops.** The map's main traffic should form one or more closed loops (a
  figure-8 around two power items is the classic). Loops keep momentum alive and make the
  rotation possible.
- **Hub-and-spoke vs loop:** small duel maps lean loop; larger maps use a central atrium
  (hub) the spokes feed into, giving a contested "stage" and quieter edges.
- **Chokes are seasoning, not structure.** A few narrow pinch points create decisions, but
  the spine of the map must stay flowing.

Named exemplars: **Aerowalk** (tight vertical loop, every point reachable many ways),
**Blood Run** (z-heavy figure-8 around RA/MH), **The Longest Yard / Q3DM17** (open
floating islands connected by pads — pads *are* the topology).

## Scale & Dimensions
Scale is relative to player size and **speed**. At 320+ ups a player crosses 512u in
~1.6 s, so rooms must be sized to give dodge room without becoming empty.
- **Player:** ~56u tall, 15u radius. **Step-up** 18u (auto-climb curbs).
- **Doorways / openings:** **96–128u** wide, **128u+** tall (avoid <96u — a strafing
  player clips it).
- **Corridors:** **128–192u** wide; never single-file (≥128u so two can pass / dodge).
- **Combat rooms:** **512–768u** for a duel pocket, **768–1024u+** for a main atrium.
- **Ceilings:** **≥256u** in fight rooms so jumps, pads and air duels have headroom; low
  (128u) only in connective corridors to vary rhythm.
- **Jumpable gaps / ledges:** climbable ledge ≤ **64u** (single jump), ≤ **92u** (double);
  horizontal gap at run speed ≈ **200u** flat, more with built speed (see doc 02).
- **1v1 vs FFA:** duel maps are *compact and tall* (you should never walk >~2 s without a
  fight or an item decision); FFA scales up room/node count so 4–8 players disperse.

## Verticality
The z-axis is where arena skill lives. Principles:
- **Layer the map** into 2–4 connected heights; make the *vertical* connections diverse
  (ramp = momentum-preserving, pad = committed arc, teleporter = instant, ledge/wall-jump
  = skill).
- **Height = advantage but exposure.** High ground sees more and rains down, but is
  reachable by multiple routes so it can't be hard-camped.
- **Pits and balconies** around a central volume produce the best duels: combatants cycle
  down into the pit and back up to the rim.
- For STRAFE 64 the killbox already embodies this (deck → catwalk → columns → spire); the
  lesson is to keep **every tier reachable ≥2 ways** and to make the vertical play
  *continuous*, not a one-way climb.

## Sightlines & Cover
- **Vary sightline length deliberately.** A map needs a couple of long "power" lanes (for
  projectile/zoning duels) and many short, broken ones (for movement duels). Avoid any
  single line that surveils the whole arena — it becomes a camp throne.
- **Cover cadence ~256–512u.** Pillars, steps, half-walls placed so a moving player always
  has a break-LOS option within a second of travel.
- **Cover doubles as movement geometry** in a movement shooter: a pillar is both a
  sightline break *and* a juke post / wall-jump face.
- **Atrium rule:** the central contested space should be readable at a glance (you can see
  the threats) but full of *micro-cover* so neither player owns it for free.

## Item Economy (the match clock)
Items, not kills, are the economy of an arena FPS. Canonical Q3/QL respawn timing:

| Item | Respawn | Role |
|---|---|---|
| Mega Health (100) | **35 s** | The heartbeat; controlling MH = controlling the map |
| Red Armor (100/200) | **25 s** | Primary power item; pairs with MH on the rotation |
| Yellow/Combat Armor (50) | **25 s** | Secondary, off the main lane |
| Powerup (Quad/Regen) | **120 s** | Round-defining; spawns force a timed contest |
| Weapons | 5 s | Fast — never the contested resource |
| Ammo | 40 s | Logistics |
| Health/Armor shards | 25–35 s | Trail/baiting pickups along routes |

Placement rules:
- **Power items on the through-route, not in a nook** — grabbing one should cost
  *position/exposure*, never be free.
- **Space MH and RA so a tour between them ≈ their timer** — that spacing *is* the
  rotation and the skill of timing items.
- **Never put a top item adjacent to a spawn** (free pickups) or in a single-exit room
  (camp magnet).
- **Powerup in a high-risk, central or exposed spot** so its 120 s spawn is a fought event.
- For STRAFE 64 (melee/no guns): the economy is **health / armor / mega / quad** only;
  put the **quad at the end of the most committed (fastest, most exposed) line** so greed =
  speed = damage (see doc 03).

## Jump Pads & Teleporters
- **Jump pads** give a *predictable arc* and a *commitment window* (you can't change course
  mid-air much) — readable risk. Use them to link tiers and to inject vertical fights;
  pair each pad with at least one non-pad route so the pad isn't a forced funnel. Land
  zones should be safe-ish and lead somewhere (not into a wall).
- **Teleporters** collapse distance and close loops; great for escape and surprise. Rules:
  exit must **not telefrag** (telekill or offset spawns), must **not face a power lane**,
  and should be **signposted** (the player should learn where it goes). Velocity behavior
  matters — STRAFE 64's **momentum portals preserve velocity** (place the destination a few
  hundred units *inward* of the far wall so it doesn't instantly re-trigger; use discrete
  gates, not full-wall fields, or bots pinball forever).

## Balance, Symmetry & Spawns
- **Symmetry:** mirror (1v1, dueling) or rotational (team) symmetry makes positions fair;
  asymmetric maps need careful item/teleporter counterweights and are riskier to auto-gen.
- **Spawns:** many (≥8 for FFA), **spread around the perimeter**, each facing *into* play,
  **away from items**, and **out of immediate sightlines** of other spawns. Spawn near a
  weapon, not near the mega.
- **Anti-spawnkill / anti-telefrag:** offset/multiple spawn points, spawn-protection or
  telefrag rules, and crucially for procedural gen, **spawn-clearance** — never place a
  spawn inside or hard against a column/pillar (our `avoid_footprints` fix).
- **Readability ("N64 clarity"):** strong silhouettes, distinct accent colors for
  spawn/item/danger, consistent grid — a fast player must parse geometry at speed.

## Implications for STRAFE 64
- The killbox is a single big atrium — give it **loop circulation**: catwalk ring + ≥2
  vertical links (pads + columns + portals) per side so a runner can lap the volume without
  backtracking or dead-ending.
- Keep **every tier reachable ≥2 ways** (pad *and* wall-jump column, not just one).
- **Item ring on the rotation:** distribute health/armor around the catwalk and deck so
  touring them traces the loop; **quad** on the most exposed/committed line.
- **Spawn ring** offset from items and columns (already nudged by `avoid_footprints`),
  facing inward, ≥8 points; keep the inner ring on diagonals so it misses axis structures.
- **Sightline discipline:** the centerpiece archetype *is* the cover — make sure it breaks
  the cross-box sightline (a hollow ring/forest/cross does; a thin spire may not), so the
  box isn't one giant power lane.
- **Headroom:** 1600u tall is plenty; ensure vertical play actually *uses* 256–900u, not
  just the deck.

## TUNING KNOBS → strafegen
| Finding | Generator knob | Suggested value / target |
|---|---|---|
| 2–3 routes between tiers; loop circulation | Vertical links per side | ≥2 distinct (pad + wall-jump column / portal) per wall; catwalk forms a full loop |
| No dead ends | Reachability check | every platform/tier has ≥2 exits; reject one-way pockets |
| Item economy = the clock | Item set + respawn | health/armor/mega/quad only (no guns); MH-analog 35 s, armor 25 s, quad 120 s |
| Power item on the rotation, costs exposure | Quad/mega placement | quad on most-exposed/committed line; mega on the loop, not in a nook |
| Items spaced ≈ timer apart | Item ring spacing | distribute around catwalk+deck so a lap ≈ item cadence |
| Doorways/openings sized for 320+ ups | Min opening width/height | ≥96u wide, ≥128u tall; corridors ≥128u |
| Fight headroom | Ceiling over fight zones | ≥256u; killbox 1600u OK — ensure play uses 256–900u |
| Sightline break | Centerpiece must break cross-box LOS | favor ring/forest/cross/twin over thin spire for the no-camp-lane goal |
| Cover cadence | Micro-cover spacing on deck | a LOS break every ~256–512u of floor |
| Spawns: plural, spread, safe | Spawn count/placement | ≥8, perimeter, facing in, off items & columns; inner ring on diagonals |
| Anti-telefrag / anti-stuck | Spawn clearance | `avoid_footprints` vs all column/spire/pad footprints (done); keep clearance ≥ player radius + margin |
| Pad has a non-pad alternate | Route redundancy | every jump pad paired with a wall-jump/ramp route to the same tier |
| Portal safety | Momentum-portal dest placement | discrete gates at catwalk height; dest a few hundred u inward of far wall; never full-wall fields |
| Readability | Accent palette discipline | vivid accents only for spawn/item/quad/danger; orange floor / grey structure / measure grid |

## Sources
- [Quake III Arena item placement & map theory — Quake3World forums](https://www.quake3world.com/forum/) — item respawn timing, reward-costs-exposure placement, duel HP budgets.
- [The Level Design Book — Layout / Flow / Metrics](https://book.leveldesignbook.com/) — circulation, loops, desire lines, flow = speed + direction + wayfinding, human-scale metrics.
- [Ten Principles for Good Level Design — Dan Taylor (GDC) summary](https://www.gamedeveloper.com/design/10-principles-of-good-level-design) — fun first, no dead ends, multiple routes, readable, paced.
- [id Tech / Quake mapping scale & brush conventions — Quake Wiki](https://quakewiki.org/wiki/) — unit scale, doorway/corridor/room dimensions, step height.
- [Quake Live / CPMA competitive map design discussion — esreality](https://www.esreality.com/) — the "rotation," item timing as control, duel-map compactness.
- [Designing for movement: Diabotical / Reflex Arena design notes — developer blogs/interviews](https://www.reflexarena.com/) — loop topology, verticality, pad/teleporter use in modern AFPS.
- [Boss Keys / level-topology-as-graph — Mark Brown (GMTK)](https://www.youtube.com/c/MarkBrownGMTK) — thinking of levels as connectivity graphs (applies directly to arena loops).
