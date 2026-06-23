# Arena Recipes — Blendable Building Blocks
> Distilled from docs 01–05 into composable *ingredients* you blend into an arena, plus
> named *preset* blends. Pair with `tools/strafegen/mapview.py` to preview a layout as a
> picture before building it in-engine. Machine-readable form: `tools/strafegen/recipes.json`.

## How blending works
An arena = **one ingredient per LAYER**, stacked. Layers are independent, so any shell +
any centerpiece + any spice set + any combat/economy/feel blend is legal. The seed varies
the *detail* within a chosen blend; the recipe pins the *structure*.

```
ARENA  =  SHELL  +  CENTERPIECE  +  SPICE[…]  +  COMBAT  +  ECONOMY  +  FEEL
```

| Layer | Picks | Status |
|---|---|---|
| **SHELL** — the container | sealed-box · (planned: tall-chimney · wide-pit · velodrome-ring · twin-linked) | sealed-box live; others planned |
| **CENTERPIECE** — the silhouette | spire · spiral · forest · ring · cross · twin | **live** (`--arch`) |
| **SPICE** — movement features (stack several) | corner-chimney · aerial-slabs · bounce-chevrons · slide-pads · momentum-portals · surf-ramps | portals/columns live; others partial/planned |
| **COMBAT** — the encounter | bot-duel · apex-pulse (slice-gates) · quad-bait | bot-duel live; gate-pulse planned |
| **ECONOMY** — items | melee-only · rotation-ring · quad-on-committed-line | melee-only live; layout planned |
| **FEEL** — tone | bullet-time-duel · audio-trails · source-dev-look | live (cfg layer) |

"live" = strafegen produces it today; "planned" = a knob the research says to add (see doc 05 §G).

---

## Ingredients

### SHELL (the container)
- **sealed-box** *(live)* — the killbox: ~2816u square, ~1600u tall, perimeter catwalk at
  ~400u, neon corners. Vertical, no escape, fall-to-deck. Knobs: `W`, `H`, `CW`.
- **tall-chimney** *(planned)* — narrow footprint, very tall (H↑, W↓); the fight climbs.
  Needs archetype radii scaled to W (doc 05 §G #7).
- **wide-pit** *(planned)* — large footprint, low ceiling; sprawling FFA.
- **velodrome-ring** *(live as `kind=arena`)* — banked deep-blue ring around an item floor;
  open, fast, less vertical. (Many seeds fail to build `.aas` — verify before bot-testing.)
- **twin-linked** *(planned)* — two shells joined by momentum portals.

### CENTERPIECE (the silhouette) — `--arch`
- **spire** — solid stepped pyramid + 4 corner + 4 cover columns; quad on crown. Cover-dense,
  classic. *(thin core — verify it breaks cross-box LOS; doc 05 §G #1)*
- **spiral** — helix of ascending slabs to the quad; no core. Highest top-speed; aerial climb.
- **forest** — scatter of varied-height wall-jump columns; quad on tallest. **Most
  combat-dense** (constant close-quarters); medium speed.
- **ring** — hollow colonnade around a central void, quad floating in the heart; dive through
  gaps. Strong all-rounder.
- **cross** — two crossing wall slabs (+) splitting the deck into quadrants, tower+quad at the
  intersection. Sightline-breaker, quadrant duels.
- **twin** — two offset towers linked by a high bridge with the quad. Two-peak duel.

### SPICE (movement features — stack several)
- **corner-chimney** *(live)* — 4 corner wall-jump columns; kick corner-to-corner to the
  catwalk. Spacing in the 150–400u kick band (doc 02).
- **aerial-slabs** *(live in spiral; planned elsewhere)* — floating slabs, gaps 150–350u,
  height deltas ≤92u, weaving in/out AND up/down. Bhop/air-strafe chains.
- **bounce-chevrons** *(planned)* — 45° corner chevron walls to revector off. **Never** a
  tilted floor-bank (reverted).
- **slide-pads** *(planned)* — flat/downhill runways feeding gates; slide-jump ×1.08.
- **momentum-portals** *(live)* — discrete velocity-preserving gates at catwalk height; dest
  inset a few hundred u. Recycle fast lines; never full-wall fields (bots pinball).
- **surf-ramps** *(planned)* — 30–50° banked faces for surf speed gain.

### COMBAT (the encounter)
- **bot-duel** *(live)* — Assassin bots, sword-only (`g_botSwordOnly 1`). Grade by frags /
  midair% / stuck (doc 05 §C).
- **apex-pulse** *(planned)* — slice-gates at apex/landing beats, wave shape **1→1→2→3→5**,
  ≥120–200u apart, never reachable only at <320 ups (doc 03).
- **quad-bait** *(partial)* — quad on the most-exposed/committed line so greed = speed.

### ECONOMY (items)
- **melee-only** *(live)* — health/armor/mega/quad, NO guns/ammo.
- **rotation-ring** *(planned)* — distribute mega/armor around catwalk+deck so a lap ≈ item
  cadence (mega ~35s / armor ~25s / quad ~120s).
- **quad-on-committed-line** *(planned)* — see quad-bait.

### FEEL (tone — cfg layer, no rebuild)
- **bullet-time-duel** — `g_timeBind 1`; still ≈ frozen, swing surges time.
- **audio-trails** — `cg_arenaTrails 1` + lattice cvars; datamosh velocity trails.
- **source-dev-look** — orange measure-grid floor, grey structure, vivid accents only for
  spawn/item/quad/danger (default).

---

## Preset blends (name → blend → preview)
Each preset is a named stack. **Preview** column is a `mapview.py` command (run from
`tools/strafegen/`) that renders the layout as an SVG you can eyeball and iterate.

| Preset | Blend | Generator today | Preview |
|---|---|---|---|
| **Colonnade Duel** | sealed-box + ring + corner-chimney + momentum-portals + bot-duel + melee-only + bullet-time | `kind=killbox --arch ring` | `python3 mapview.py killbox 8224 --arch ring` |
| **Ascension** | sealed-box + spiral + aerial-slabs + momentum-portals + bot-duel + quad-bait | `killbox --arch spiral` | `python3 mapview.py killbox 7001 --arch spiral` |
| **Thicket** | sealed-box + forest + corner-chimney + bot-duel + melee-only | `killbox --arch forest` | `python3 mapview.py killbox 9000 --arch forest` |
| **Crossfire** | sealed-box + cross + momentum-portals + bot-duel (quadrant duels) | `killbox --arch cross` | `python3 mapview.py killbox 9999 --arch cross` |
| **Twin Peaks** | sealed-box + twin + aerial-slabs (bridge) + bot-duel | `killbox --arch twin` | `python3 mapview.py killbox 8888 --arch twin` |
| **Citadel** | sealed-box + spire + corner-chimney + cover + bot-duel | `killbox --arch spire` | `python3 mapview.py killbox 7001 --arch spire` |
| **Velodrome** | velodrome-ring + center-tower + aerial-slabs + bot-duel *(verify .aas)* | `kind=arena` | `python3 mapview.py arena 7140` |
| **Bladestorm** *(planned combat)* | sealed-box + forest + apex-pulse(1→1→2→3→5) + quad-bait | `killbox --arch forest` + gates | (gates = planned knob) |

### Iterate-from-text workflow
1. Pick a blend (or tweak a preset's layers in words).
2. Map it to today's knobs: `kind` + `--arch` (+ planned: spice/combat/economy params).
3. **Preview the picture:** `python3 mapview.py <kind> <seed> --arch <archetype>` → open the
   SVG (top-down plan + side elevation + legend). Or `--ascii` for an inline text grid.
4. Read the layout, adjust the blend/seed, re-preview. When it looks right, build + bot-test
   via `engine_generate_map` + `engine_playtest_report` and grade against doc 05 §C bands.

---

## Compatibility notes
- **Centerpiece × sightline:** spire is the only thin core — pair it with cover columns or
  it won't break the cross-box power lane (doc 01/05). Ring/forest/cross/twin self-break LOS.
- **Spiral × combat:** spiral's height introduces fall deaths (MOD_FALLING) — fine as stakes;
  cap fall damage if it annoys.
- **momentum-portals × bots:** bots ignore portals (like race triggers) — portals are a human
  fast line; keep discrete (no full-wall fields) or bots pinball (~98% air, 0 frags).
- **velodrome-ring × bots:** many seeds build no `.aas`; favor killbox shells for bot-graded
  iteration, use the ring for human/visual work.
- **Every mandatory gap** must be clearable with single-jump@run-speed (~190u); reserve
  wall-jump/DJ/pad lines for the optimal route (doc 02).

See `recipes.json` for the machine-readable ingredient/preset list (consumable by the
generator's recipe hook: `Killbox(seed, diff, archetype=...)`).
