# Sword / Katana Combat — Feel & Depth Design

**Status:** ✅ **all tiers implemented** on branch `feature/sword-combat-depth` (flow-assist +
Tiers 1–4), built green and smoke-tested. Goal: fix the "ram forward and bump into each
other" problem and add a mastery ceiling — a Tekken-like string system on two buttons —
**without** breaking the movement pillar. The tier descriptions below are the design intent;
the "what shipped" note at the head of each tier records what actually landed.

### Shipped cvar cheat-sheet (all live/archive)
| cvar | default | what |
|---|---|---|
| `pm_swordRecovery` / `pm_swordRecoveryMin` | 300 / 180 | swing recovery ms at standstill / flow speed |
| `g_swordWhiffScale` | 1.0 | recovery a connecting hit refunds (miss = exposed); 0 = off |
| `g_swordMinRange` | 28 | cut whiffs closer than this (anti-ram); 0 = off |
| `g_swordGuardRaise` | 90 | ms the guard must be up before it parries |
| `g_swordRiposte` | 250 | clean-parry counter-buff window (defender +50%) |
| `g_swordCounterHit` | 1 | hit a mid-swing enemy → +50% + pop |
| `g_swordJuggle` | 1 | up-cut launches / down-cut spikes caught bodies |
| `g_swordGuardBreak` | 1 | slash after dropping guard breaks a blocker (P~S) |
| `cg_swordReticle` | 1 | hot-tint crosshair when an enemy is in kill-range |
| _(flow-assist)_ `pm_swordMagnet`/`Range`, `g_swordAimSnap`, `g_swordChainRedirect` | 1.0/300, 12, 1 | lunge magnetism, aim-snap, kill-redirect |

> **Note on Tier 4:** the counter-hit / directional-ender / guard-break moves shipped by
> reusing the existing quadrant + combo + guard state (prediction-safe, no new networked
> fields). A full data-driven S/P string *routing table* (arbitrary buffered move-strings) was
> left as a future extension — the moves below deliver the Tekken feel through systems already
> in place. Feint is covered by the guard-cancel of a buffered swing (flow-assist commit).

---

## ✅ SHIPPED — flow-assist pass (the "fast superhuman ninja" feel)

Four features, all cvar-gated, built + smoke-tested. These are the *power-fantasy* axis
(assist, not friction); the Tier 1-3 friction below is for **duels** and still gates onto
"opponent also has a sword".

| Feature | What | Where | Cvar (default) |
|---|---|---|---|
| **Lunge magnetism** | Swing near an enemy steers the forward lunge onto them + boosts it to close the gap. Predicted (trace-fan in pmove). | `PM_SwordMagnetLunge` in `bg_pmove.c` | `pm_swordMagnet` (1.0, 0=off), `pm_swordMagnetRange` (300) |
| **Aim-snap** | Bends the *cut axis* (not the camera) onto a near-miss enemy within N degrees so slightly-off aim connects. | `G_SwordFindTarget(byAngle)` in `Weapon_Sword`, `g_weapon.c` | `g_swordAimSnap` (12 deg, 0=off) |
| **Kill-to-kill redirect** | On a clean kill, the forward kick aims at the *next* nearest body → clearing a room is one flowing line. | kills-block in `Weapon_Sword`, `g_weapon.c` | `g_swordChainRedirect` (1) |
| **Input buffering** | A slash pressed during the 230ms recovery is queued (`PMF_SWORD_BUFFER`) and fires the instant recovery ends — no dropped taps. Guarding cancels the queued swing. | `PM_Weapon` in `bg_pmove.c` | (always on) |

**Prediction-sync gotcha:** the magnet runs in *shared* pmove, so the two magnet cvars are
registered under `pm_`-prefixed names and mirrored in **both** `g_active.c` (server) and
`cg_predict.c` (client) — same pattern as the air-strafe tunables. Miss either bridge and a
live cvar change mispredicts.

---

## The problem

Right now the optimal melee play is: walk into the opponent's face and mash slash. It feels
like *bumping*, not *fencing*. There is no **neutral game** — no reason to respect spacing.

## Root cause (confirmed in code)

You already have the *juice* (55ms hitstop, view-punch, quiver, quadrant parry,
cleave-launch). What's missing is **spacing friction**. Three code facts make ramming
strictly optimal:

- **Unconditional lunge** — `+120 u/s forward on *every* swing`
  (`engine/code/game/bg_pmove.c:2526`), gated only at 1000 u/s. Swinging *is* a free
  forward step, so faceplanting is rewarded.
- **No windup, no whiff cost** — traces fire instantly; next swing in a flat `230ms`
  whether you hit or miss (`bg_pmove.c:2536`). Missing costs nothing, so range is
  irrelevant.
- **Zero-commitment guard** — raise MOUSE2 instantly, even mid-recovery, for free
  (`bg_pmove.c:2375`). Turtling has no cost, blocking is never a *read*.

## The unifying principle: speed-inverse spacing friction

Damage already scales with speed (45 → 120 across 0 → 960 u/s). So make **spacing friction
speed-*inverse*** to match that philosophy instead of fighting it:

- **High speed** → you fly *through* clusters and kill. That's flow. **Leave it untouched.**
- **Low-speed standoff** → the ramming problem. This is where commitment, recovery, and the
  neutral game should bite.

So every friction mechanic below is scaled to **fade out as speed rises**. Fast = flow
(current behavior preserved). Slow = duel (new neutral game). This is the same speed-scaling
logic the weapon already uses for damage and reach.

---

## Tier 1 — Create the neutral game (do first)

### 1. Speed-inverse whiff recovery — *the single biggest fix*
- **Where:** `bg_pmove.c:2536` (`addTime = 230`) + server hit result in `Weapon_Sword`
  (`g_weapon.c:160`).
- **What:** Base recovery `230 → 300ms`. A *connecting* hit refunds it back down (server
  sets `weaponTime` ≈ 180 via the existing combo path). A **whiff** eats the full 300 — a
  punishable window. Scale the penalty by speed: near-full at standstill, ≈0 above ~600 u/s.
- **Why:** Turns "miss = free retry" into "miss = you're exposed." This is what makes
  players respect range instead of ramming.
- **Cvars:** `g_swordRecovery` (300), `g_swordWhiffScale`.
- **Note:** `weaponTime` is server-authoritative + predicted. A hit-based refund briefly
  mispredicts then self-corrects; or gate on the predictable combo-window state.

### 2. Distance-gate the lunge + minimum range
- **Where:** `bg_pmove.c:2510-2528` (the `+120` lunge); `SWORD_RANGE` at `g_weapon.c:146`.
- **What:** Kill the lunge at point-blank (don't reward faceplant-swinging); keep it only
  when already moving with a target ahead. Add `SWORD_RANGE_MIN` (~40u) so being *too close*
  whiffs the arc — you must *step to distance*.
- **Why:** Removes the mechanical incentive to occupy the same voxel. Forces push/pull.
- **Cvars:** `g_swordLungeMinRange`, `g_swordMinRange`.

### 3. Guard commitment
- **Where:** guard toggle `bg_pmove.c:2375`; resolution `g_combat.c:999`.
- **What:** Short **raise-time** (~90ms before `EF_BLOCKING` protects) so turtling on
  reaction isn't free — you must read the swing *early* (Sekiro blocks early, not late).
  Optional light guard-stamina that chip-drains on absorbed hits.
- **Why:** Makes the parry a read, not a panic button — prerequisite for feints to matter.
- **Cvar:** `g_swordGuardRaise` (90).

---

## Tier 2 — Deepen the duel (leverages bullet-time)

### 4. Parry riposte window
You already stagger the attacker with `SWORD_PARRY_KICK` (280) on a clean quadrant parry
(`g_combat.c:27`). Add a brief **attacker `weaponTime` lockout** + a **defender buff window**
(~250ms of faster/harder next swing). This is the Sekiro posture-break payoff — reading the
quadrant and parrying *earns a kill*, not just a shove. In bullet-time the window is readable
and cinematic.

### 5. Feint / swing-cancel
Once Tier 1 adds a windup, allow cancelling that windup into a guard (costs the recovery).
This is Mordhau's whole mind-game — bait the early block, then feint. Cheap to add on top of
#1, huge depth ceiling.

### 6. Range / threat reticle cue (client-only, cheap, high value)
When an enemy enters kill-range, flash the reticle / screen edge (Halo's red-reticle). In
first person players *cannot* judge melee spacing by eye — this single cue turns "guess the
distance" into "dance at the edge." Lives in `cg_draw.c` / `cg_weapons.c`.

---

## Tier 3 — Feedback polish (you're most of the way here)

- **Clank vs clunk** — pitch-up the `EV_SWORD_HIT` sound on a *clean* quadrant parry, dull it
  on a glancing one (`cg_event.c:1277`). Best-documented "did I nail it" cue in Sekiro.
- **Whiff swish** — give a *miss* its own airy sound + no hitstop, so a whiff *feels*
  exposed.
- **Scale hitstop up** on clean parry / finisher (`SWORD_HITSTOP_MS` = 55 → ~90 on those).

---

## Tier 4 — Tekken-style string system (the mastery ceiling)

The insight that makes this fit STRAFE 64: **your quadrant system already reads movement
direction (WASD) like a fighting-game stick** (`bg_misc.c:1456`, `BG_SwordPickQuads`). So you
already have the "directional" half of Tekken input. Add a **string grammar** over two
buttons — **Slash (S)** and **Parry (P)** — plus direction, and you get move-strings,
launchers, and juggles with no new controls.

### Notation
`S` = Slash (attack / MOUSE1). `P` = Parry (guard / MOUSE2). Direction prefix from movement:
`N` neutral, `F/B/L/R` cardinal, plus diagonals — the eight you already map to quadrants.
`~` = *hold*. `,` = *followed by* (Tekken convention). A **string** is a direction + a
sequence of button presses buffered within the combo window.

### Design rules (from Tekken)
- **Natural combo (NC):** if the first hit *lands*, the rest of the string is guaranteed
  within the combo window. If a hit *whiffs or is blocked*, the string **drops** and the
  opponent can interrupt. Risk/reward — long strings are greedy. You already have
  `SWORD_COMBO_WINDOW` (600ms) + `combomul` + the every-3rd finisher; this generalizes it.
- **Counter-hit (CH):** hitting an enemy *during their own swing windup* = counter-hit → auto
  small launch / bonus damage. This is the mechanic that *rewards the neutral game* directly
  — you win the read, you get the launch. (Requires the Tier 1 windup to exist.)
- **Direction picks the ender:** because direction already sets the quadrant, different
  directional enders are naturally different moves — `F,S` spikes, `B,S` carries, `Up,S`
  launches. No new input needed.

### Proposed move list

| Input | Name | Effect | Hooks into |
|---|---|---|---|
| `S, S, S` | **Jab string** | Existing 3-hit; 3rd is finisher (wide arc + cleave-launch). Keep as-is. | `Weapon_Sword` combo path |
| `S, P` | **Feint** | Cancel swing windup into guard — bait the block. | Tier 1 #1 windup + Tier 2 #5 |
| `P (clean parry), S` | **Riposte** | Clean quadrant parry → buffed counter-slash in the riposte window. "Parry then slash." | Tier 2 #4 |
| `P~S` (hold parry, then slash) | **Guard-break heavy** | Slow committed overhead that *breaks* a held guard + big posture damage. "Hold parry then slash." | new charge state; `EF_BLOCKING` read |
| `B, P~S` or `Up-charged` | **Launcher** | Slow, whiff-punishable pop that lofts the enemy airborne. | reuses existing victim loft `velocity[2] += kick*0.45` |
| (airborne target) `S, S` | **Juggle filler** | In bullet-time, slashes connect on the lofted enemy; direction steers. | cleave-launch already lofts victims |
| (during juggle) `F, S` | **Ender** | Spike / carry fling along the cut line. | existing `SWORD_CLEAVE_KICK` fling |

The launcher → juggle loop is the payoff and it's *half-built already*: the cleave-launch
path already lofts victims upward (`velocity[2] += kick * 0.45`), and bullet-time dilates the
air-time so a juggle is readable and stylish instead of twitchy. This is the STRAFE 64 spin
on Tekken — juggles happen in slow-mo.

### What to build for the string layer
1. **Input buffer** — queue the next S/P during recovery so presses chain cleanly (the code
   already alternates `TORSO_ATTACK`/`TORSO_ATTACK2`; extend with a buffered next-input).
2. **String routing table** — a data-driven `BG_SwordString(state, dir, button) → nextMove`
   map, packed into the event parm exactly like the quadrants already are
   (`SWORD_PACK_QUADS`). Keep it in `bg_misc.c` so client + server agree (prediction-safe).
3. **String state in `playerState`** — current string node + a drop timer (reuse the combo
   window). Whiff / block / timeout → reset to neutral.
4. **Counter-hit flag** — set when the victim is in swing-windup at hit time; server-side in
   `Weapon_Sword`, routed to the launch/bonus path.

### Cvars
`g_swordStrings` (master toggle), `g_swordCounterHit`, `g_swordJuggle`,
`g_swordLauncherWindup`.

---

## Suggested build order

1. **Tier 1 behind cvars**, A/B live in-engine (`engine_compare` / `engine_movement_set`
   for live cvars; the `bg_pmove` constants need a rebuild). Tier 1 alone should kill the
   ram-and-bump.
2. **Tier 3 polish** — cheap, immediate feel win, no balance risk.
3. **Tier 2** — riposte window + feints + reticle cue turn it into a duel.
4. **Tier 4 strings** — the mastery ceiling. Windup (Tier 1) and riposte (Tier 2) are
   prerequisites; the launcher/juggle reuses the existing loft, so it's less work than it
   looks.

---

## Sources

- [Playtank — Building Systemic Melee](https://playtank.io/2024/08/12/building-systemic-melee/)
- [SuperJump — The Art and Science of Sekiro's Combat](https://www.superjumpmagazine.com/the-art-and-science-of-sekiros-combat/)
- [HiFight — FOOTSIES (spacing/whiff-punish teaching game)](https://hifight.github.io/footsies/)
- [Patrick Miller — Going from Footsies to Neutral](https://pattheflip.medium.com/going-from-footsies-to-neutral-6424008ea6a1)
- [Mordhau's combat depth (ResetEra)](https://www.resetera.com/threads/mordhaus-melee-combat-seems-to-be-on-another-level.108605/)
- [MoCap Online — Combat Animation for Games (hitstop/recovery)](https://mocaponline.com/blogs/mocap-news/combat-animation-game-dev-guide)
- [Ahmad Mohammadnejad — A More Realistic HitStop](https://www.ahmadmohammadnejad.com/sandbox/a-more-realistic-hitstop)
- [Kotaku — Solving First-Person Melee](https://kotaku.com/these-guys-hope-to-solve-the-problem-of-first-person-me-5892918)
- [Wavu Wiki — Tekken Controls / string notation](https://wavu.wiki/t/Controls)
- [Tekken Move Terminology (natural combo, counter-hit, launcher)](https://tekken.fandom.com/wiki/Move_Terminology)
