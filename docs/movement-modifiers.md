# Movement Modifiers — replacing arena powerups with flow modifiers

**Status:** design spec (not yet implemented)
**Delivery decision:** keep them as **world pickups** (reskin the existing 6 powerup
items). No new HUD, no new netcode — we re-point timer slots that already replicate.

---

## 1. Why

A powerup in this engine is just a timer: `ps->powerups[PW_X]` holds an expiry
timestamp. The whole pipeline is content-agnostic and already built:

- pickup adds time — `Pickup_Powerup` ([g_items.c:49](../engine/code/game/g_items.c))
- a per-frame loop expires it — [g_active.c:1518](../engine/code/game/g_active.c)
- the HUD auto-draws any active slot — `CG_DrawPowerups` ([cg_draw.c:1172](../engine/code/cgame/cg_draw.c))
- `powerups[]` is part of `playerState_t`, so it is **network-replicated and
  client-predicted** for free.

To change what a powerup *does*, we touch only two things: the **effect read** (one
`if` in pmove or ClientThink) and the **visual**. Everything else is reused.

The baseq3 set (Quad / Haste / Battlesuit / Invis / Regen / Flight) is arena
stat-economy. STRAFE 64's loop is **Descend → Tighten → Revector → Slice → Release**
with bullet-time melee. Pickups should remix *flow and movement expression*, not top up
health bars. This spec repurposes 5 of the 6 slots into movement modifiers and keeps
Quad (the one pickup that still reads as "powerful" in melee bullet-time).

---

## 2. Slot mapping

We **reuse existing enum slots** — no new `PW_*` values, no `MAX_POWERUPS` change. The
item definitions in `bg_itemlist[]` ([bg_misc.c:537+](../engine/code/game/bg_misc.c))
get new names / pickup text / icons / glow; the slot constant stays.

| Slot (`bg_public.h:310`) | Old | New modifier | One-line |
|---|---|---|---|
| `PW_HASTE` | Speed | **Overdrive** | strafe better → go faster (skill-scaled, not flat) |
| `PW_FLIGHT` | Flight | **Featherfall** | low-G + extra air-jump + longer walljump (keeps normal movement) |
| `PW_INVIS` | Invisibility | **Phase Dash** | near-zero dash cooldown + i-frames during the dash |
| `PW_REGEN` | Regeneration | **Tempo** | widens the bullet-time band so slow-mo lasts longer |
| `PW_BATTLESUIT` | Battle Suit | **Magnetize** | grapple auto-latches, longer range, springier rope |
| `PW_QUAD` | Quad Damage | *(keep)* | unchanged — melee damage amp |

Two free enum slots remain (`PW_INVULNERABILITY` is 14, `PW_NUM_POWERUPS` is 15) for
later ideas (Reverse-G, Double-time wallrun) — see §6.

---

## 3. Where the effect goes (important)

Movement modifiers must be read in **shared, predicted code** — i.e. inside
`bg_pmove.c`, gated on `pm->ps->powerups[PW_X] > pm->cmd.serverTime` — so the client
predicts them and the feel stays crisp. The legacy `PW_HASTE` speed mult lives in
server-only `ClientThink_real` ([g_active.c:1220](../engine/code/game/g_active.c)),
which works for a propagated `ps.speed` but would cause prediction misses for
accel/gravity/air-jump changes. **Rule: anything that alters acceleration, gravity,
jump counts, or air control belongs in `bg_pmove.c`, not ClientThink.** Effects that
are inherently server-side (grapple trace-attach, dash homing that must see enemies)
stay server-side.

Every modifier's magnitude is exposed as a **live cvar** (`CVAR_ARCHIVE`, registered in
the table at [g_main.c:188](../engine/code/game/g_main.c)) so it tunes without a rebuild
— consistent with the engine's live-tuning workflow.

---

## 4. The modifiers

### Overdrive  (`PW_HASTE`)
**Feel target:** "if I strafe clean, I rocket." Rewards input quality, not a flat dial.

- **Effect:** while active, scale the strafe/air tunables, *not* base speed:
  `pm_strafeAccelerate ×= g_modOverdriveAccel`, `pm_airControlAmount ×=
  g_modOverdriveAir`, and lift the air speed cap so a good strafer actually reaches the
  new ceiling. Keep a small (~1.15×) base-speed nudge so it still feels good standing
  still, but the headroom comes from skill.
- **Hooks:** strafe accel / air control are read in the air-move path of
  `bg_pmove.c` (the `pm_strafeAccelerate` / `pm_airControlAmount` reads). Apply the
  multipliers at the top of `PM_AirMove` when the slot is hot.
- **Cvars / ranges:** `g_modOverdriveAccel` 1.5–3.0 (default **2.0**),
  `g_modOverdriveAir` 1.3–2.0 (default **1.5**), `g_modOverdriveBase` 1.0–1.3 (**1.15**).
- **Visual:** keep the haste smoke trail (`CG_HasteTrail`, [cg_players.c:1898](../engine/code/cgame/cg_players.c)) but recolor to the Overdrive accent; add a speed-line burst past the air-cap.
- **Risk:** uncapped strafe accel can break maps. Clamp the air-cap rise.

### Featherfall  (`PW_FLIGHT`)
**Feel target:** floaty surf bursts — *augments* normal movement instead of replacing it
(unlike old Flight, which swapped in `PM_FlyMove` and killed bhop/walljump).

- **Effect:** `ps.gravity ×= g_modFeatherGravity` (~0.55), `pm_airJumpMax += 1`,
  `pm_wallJumpMax += 1`, and extend `pm_wallRunDurationMs` while held.
- **Hooks:** gravity is `pm->ps->gravity` in pmove; `pm_airJumpMax`
  ([bg_pmove.c:68](../engine/code/game/bg_pmove.c)), `pm_wallJumpMax`
  ([bg_pmove.c:95](../engine/code/game/bg_pmove.c)), `pm_wallRunDurationMs`
  ([bg_pmove.c:104](../engine/code/game/bg_pmove.c)). Note these are file-scope ints —
  read them into locals at the top of `PM_Pmove` and apply the modifier there so we
  don't mutate globals across players.
- **Cvars / ranges:** `g_modFeatherGravity` 0.40–0.70 (**0.55**),
  `g_modFeatherAirJump` 0–2 (**1**), `g_modFeatherWallJump` 0–2 (**1**).
- **Visual:** soft particle drift + a pale up-glow dlight; reuse Low-G mutator feel.
- **Risk:** low-G + extra air-jumps can float a player out of bounds — pair with map
  ceilings or a soft altitude clamp on generated courses.

### Phase Dash  (`PW_INVIS`)
**Feel target:** chain dashes through danger — the slice-setup tool on tap.

- **Effect:** drop the dash cooldown to near-zero and grant i-frames during the dash
  window. Dash already exists: `G_ClientDash` ([g_active.c:861](../engine/code/game/g_active.c))
  gates on `level.time < client->dashTime` and sets `client->dashTime = level.time +
  DASH_COOLDOWN`. While the slot is hot, substitute a short `g_modPhaseCooldown`.
- **i-frames:** reuse the existing invulnerability-window pattern —
  `targ->client->invulnerabilityTime > level.time` ([g_combat.c:946](../engine/code/game/g_combat.c)).
  Set `invulnerabilityTime = level.time + g_modPhaseIFrame` at dash start.
- **Cvars / ranges:** `g_modPhaseCooldown` 100–400 ms (**200**), `g_modPhaseIFrame`
  120–300 ms (**180**).
- **Visual:** **keep the invis shader** ([cg_players.c:2207](../engine/code/cgame/cg_players.c))
  as the tell — the player shimmers translucent for the i-frame window — plus the dash's
  chromatic-ghost strobe (`EV_DASH`, [bg_public.h:505](../engine/code/game/bg_public.h)).
- **Risk:** i-frames in a duel are strong; keep the window short and the cooldown honest.

### Tempo  (`PW_REGEN`)
**Feel target:** lean into the core `g_timeBind` bullet-time — slow-mo bites deeper and
holds longer, so a Tempo pickup is a *combat-flow* amplifier, not a heal.

- **Effect:** while held, push the time-bind band: lower `g_timeBindMin` (deeper freeze
  when still), raise `g_timeBindCurve` (stays slow longer), and lift `g_timeBindFire`
  (the swing surge reads bigger). Apply as scaled overrides, restore on expiry.
- **Hooks:** all live cvars at [g_main.c:61+](../engine/code/game/g_main.c); the
  timescale is computed at [g_active.c:1294](../engine/code/game/g_active.c)
  (`pm.timeScale`). Cleanest impl: a `G_ApplyTempo()` that snapshots the base values,
  scales them while the slot is hot, and reverts when it clears in the expiry loop.
- **Cvars / ranges:** `g_modTempoMinScale` 0.4–0.8 (**0.6**, multiplies `g_timeBindMin`),
  `g_modTempoCurve` 1.0–1.6 (**1.3**), `g_modTempoFire` 1.1–1.5 (**1.25**).
- **Visual:** a clock-pulse vignette / desat shimmer when Tempo is active; tie to the
  existing audio time-pitch so the world *sounds* heavier too.
- **Risk:** time-bind is global-feel; over-scaling makes the match sludgy. Conservative
  defaults, and consider capping Tempo to the holder's own clock if per-player slow-mo
  exists.

### Magnetize  (`PW_BATTLESUIT`)
**Feel target:** a traversal toy — the grapple becomes generous and bouncy.

- **Effect:** while held: force `g_grappleInstant` on, multiply `g_grappleRange`
  (default 4000) by `g_modMagnetRange`, and soften the rope (`pm_grappleSpring` up a
  touch, `pm_grappleDamp` down) for a springier swing. Optional auto-latch: latch to the
  nearest valid surface in the crosshair cone without a perfect aim.
- **Hooks:** grapple cvars at [g_main.c:188+](../engine/code/game/g_main.c)
  (`g_grapple`, `g_grappleInstant`, `g_grappleRange`, `g_grappleSpeed`); range trace at
  [g_missile.c:887](../engine/code/game/g_missile.c); rope spring/damp at
  [bg_pmove.c:99-100](../engine/code/game/bg_pmove.c).
- **Cvars / ranges:** `g_modMagnetRange` 1.25–2.0 (**1.5**), `g_modMagnetSpring`
  0.35–0.6 (**0.5**), `g_modMagnetDamp` 3.0–6.0 (**4.0**).
- **Visual:** a magnetic field shimmer on the player + a brighter grapple line; reuse the
  battlesuit shader slot recolored.
- **Risk:** auto-latch can feel like it grapples *for* you — make it assist aim, not
  replace it.

### Quad  (`PW_QUAD`) — kept
Unchanged. Damage mult at [g_weapon.c:113](../engine/code/game/g_weapon.c) and
[g_weapon.c:804](../engine/code/game/g_weapon.c). Stays as the one combat pickup.

---

## 5. Implementation plan

1. **Cvars:** add the `g_mod*` block to the registration table at
   [g_main.c:188](../engine/code/game/g_main.c) + `extern` decls in `g_local.h`. Mirror
   any pmove-side ones the predicted code needs (the strafe/gravity reads are already
   cvar-driven on both sides).
2. **Effect reads (movement):** in `bg_pmove.c`, read the affected file-scope tunables
   into locals at the top of `PM_Pmove` / `PM_AirMove`, and scale them when
   `pm->ps->powerups[PW_X] > pm->cmd.serverTime`. This keeps prediction correct and never
   mutates per-player globals.
3. **Effect reads (server):** Phase-Dash cooldown/i-frames in `G_ClientDash`; Tempo
   snapshot/restore around the expiry loop ([g_active.c:1518](../engine/code/game/g_active.c));
   Magnetize grapple overrides where the grapple fires.
4. **Items:** rename the 5 entries in `bg_itemlist[]`
   ([bg_misc.c:537+](../engine/code/game/bg_misc.c)) — pickup name, icon, world model,
   glow color. Keep `giTag` = the same `PW_*` slot.
5. **Visuals:** per-modifier dlight/shader/trail in `CG_PlayerPowerups`
   ([cg_players.c:1898](../engine/code/cgame/cg_players.c)) and
   `CG_AddRefEntityWithPowerups` ([cg_players.c:2207](../engine/code/cgame/cg_players.c)).
   The arena trail-effects system is reusable here.
6. **HUD:** nothing — `CG_DrawPowerups` already draws active slots. Only the icon assets
   change.

Each modifier is independently shippable and **live-testable via the engine MCP**
(`engine_give` the item, `engine_movement_set`/`engine_set_cvar` to sweep the `g_mod*`
ranges, `engine_compare` for value sweeps, `engine_input` to feel it).

---

## 6. Future slots (free enum space)

- **Reverse-G** — invert gravity for a few seconds (ceiling running on flow courses).
- **Double-time wallrun** — `pm_wallRunDurationMs` ×3 + frictionless wall ride.
- **Course-gate delivery** — the same timer slots can later be driven by strafegen
  flow-gate triggers (run a gate → N s of a modifier) for generated courses, without
  changing any of the above. Out of scope for this pass (we chose world pickups), but the
  effect code is identical.

---

## 7. Ideation — the unhinged list

> **Design north star: "for X seconds I can do something I normally can't."**
> The best modifiers aren't a bigger number — they're a **new verb**. When the timer is
> hot, the player's vocabulary of movement *expands*: a button does something it never
> did, a surface behaves differently, the rules bend. When it expires, you snap back to
> baseline and *miss it*. That craving is the whole reward loop. Bias every idea below
> toward **unlock a verb**, away from **scale a stat**.

A rule of thumb while reading: if you can describe it without the word "more," it's
probably good. "More speed" is a dial. "You can now run on walls indefinitely and
ceiling-jump off them" is a verb.

Grounded-but-wild candidates (most reuse a system that already exists — wall ride,
dash, grapple, sword/deflect, bullet-time, lattice trail walls, slice gates, ragdoll,
time-pitch audio):

### Traversal verbs
- **Wallflower** — every surface is a wallrun surface and walljumps are *unlimited*. For
  the duration the level is a skatepark; floors are optional. (`pm_wallJumpMax` → ∞,
  `pm_wallRunDurationMs` → huge, drop the wall-detach friction.)
- **Spiderhands** — the grapple fires *continuously* on hold and re-latches the instant
  it breaks: full Spider-Man traversal, no aim-and-release rhythm. (Magnetize taken to
  its logical extreme — auto-latch + zero re-fire cooldown.)
- **Phase Walker** — for X seconds you no-clip through *thin* world brushes (one-way
  walls become shortcuts). Dangerous, map-breaking, glorious. Gate it to a whitelist of
  surface flags so it's a designed shortcut, not a fall-out-of-world bug.
- **Rocket Boots** — every jump while held is a *charged* leap: hold to compress, release
  to launch on a parabola. Turns the jump button into a slingshot.
- **Gecko** — invert your relationship with gravity: stick to whatever surface you touch
  and reorient your "down" to it. Ceiling becomes floor. (Reuses the Reverse-G plumbing
  but per-contact instead of global.)

### Time verbs (lean on the `g_timeBind` core)
- **Bullet Dancer** — *only you* move at full speed; the rest of the world is frozen near
  zero regardless of your movement intent. SUPERHOT but you're the cheat. (Decouple your
  client clock from the intent curve while held.)
- **Rewind** — tap a button to snap back to where you were 1.5 s ago (position +
  velocity), like the race-ghost recorder but live and self-applied. Escape verb. Reuses
  the ghost record/replay buffer.
- **Afterimage** — leave a solid, sword-blocking *copy of yourself* every 0.5 s that
  enemies waste hits on; dash to swap places with your oldest image. (Lattice trail-wall
  system + the deflect logic.)
- **Stutter Step** — your own dashes cost no cooldown *and* freeze the world for the dash
  duration. Chain ten dashes in one frozen instant, then time resumes. Phase Dash's
  evil twin.

### Combat-movement fusion verbs
- **Vector Blade** — every sword swing *launches you* along the swing arc (lunge-cancel
  into the next slice). Movement and attack become the same input. Pure flow-loop fuel.
- **Deflect Dash** — blocking a projectile doesn't just parry it, it *rockets you*
  backward off the impact (recoil-skating on incoming fire). Turns the duel into a
  movement puzzle. (Hooks the EF_BLOCKING / deflect path.)
- **Kill Boost** — every connected slice refills your dash and adds a speed pop in your
  look direction. Aggression *is* traversal; killing keeps you airborne. (Titanfall
  "boost" energy on hit.)
- **Trail of Knives** — your speed-trail wall becomes *lethal*: anything that touches the
  ribbon you leave takes damage. You paint kill-zones by moving well. (Promote the
  lattice trail to a damage volume while held.)

### Chaos / unhinged tier (ship behind a "spice" toggle)
- **Ragdoll Cannon** — you *become* a ragdoll under player steering: no upright pose, all
  momentum and flailing limbs, bouncing off geometry. Slapstick speedrun mode. (Reuse the
  Verlet ragdoll, but driven instead of dead.)
- **Big Head / Tiny Hitbox** — shrink to a fraction of size: same speed feels enormous,
  gaps you couldn't fit open up, and you're nearly impossible to hit. A *spatial* verb.
- **Drunk Gravity** — gravity direction wanders on a slow sine; the floor is a
  suggestion. Hard, hilarious, only on flow courses.
- **Time Bomb** — a *forced* good thing: speed climbs automatically and you cannot stop
  (Clustertruck-on-rails). Drop below a speed floor and you detonate. Inverts the pickup
  from "I get a toy" to "survive the toy."
- **Possession** — fire it at a slice-drone or bot and *become* it for a few seconds,
  then snap back to your body. Scouting + sabotage verb.
- **Mirror World** — flips your left/right input (and the trail rendering) — a cruel
  modifier for a versus dare, a comedy modifier for a streamer.

### Mario Kart tier — throwables, traps & catch-up

> A *different shape* of modifier. The verbs above are **personal** ("I can now do X").
> This tier is **interactive** — items you aim at people, traps you leave behind, global
> disruptions, and catch-up tools. Mario Kart's genius is the **rubber-band economy**:
> who's winning decides what you roll. The pickup item should grant a *random* effect
> weighted by your placement (frags / race position) — last place rolls the nasty
> game-changers, the leader rolls bananas. That weighting (see §8) is what makes this
> tier sing, and it slots straight onto the existing pickup → timer plumbing (the slot
> just carries "armed item N" instead of "buff active").

**Aimed projectiles**
1. **Homing Slice** *(Red Shell)* — fire-and-forget slice-drone that locks the nearest
   enemy and chases. (slice-drone entity + `g_dashHoming`-style tracking.)
2. **Vector Shell** *(Green Shell)* — a deflectable bolt that ricochets off walls and
   keeps going till it connects — or gets parried back at you. (fire_bullet + bounce +
   the deflect path.)
3. **Skyfall** *(Blue Shell)* — auto-targets whoever's in 1st; a slow, telegraphed strike
   rains on the leader. Everyone sees it coming, the leader has to *move* to live.

**Traps you leave behind**
4. **Caltrop Ribbon** *(Banana)* — drop a short lattice trail-wall segment behind you;
   whoever clips it trips/eats damage. You weaponize the racing line.
5. **Proximity Mine** *(Bob-omb)* — spawn a mine that detonates in a knockback blast,
   ragdoll-launching anyone near. (engine_spawn entity + ragdoll impulse.)
6. **Decoy Box** *(Fake Item Box)* — a counterfeit pickup that ragdolls whoever greedily
   grabs it.

**Self bursts (short, snappy — the Mario Kart "use it now" feel)**
7. **Boost** *(Mushroom)* — one instant off-cooldown dash-burst in your look direction.
8. **Triple Boost** *(Triple Mushroom)* — three stored bursts to spend.
9. **Star** *(Invincibility Star)* — brief invuln + ragdoll-launch anyone you touch +
   rainbow trail + the music pitches up. The crowd-pleaser.
10. **Tracer** *(Bullet Bill)* — autopilot: you *become* a guided projectile screaming
    down the optimal flow line, smashing through everyone in the way. Pure last-place
    catch-up — give up control, get a free comeback.
11. **Golden Dash** *(Golden Mushroom)* — unlimited dashes for X seconds.
12. **Feather** *(SNES Feather)* — one enormous hop that also dodges *through* the next
    incoming hit. The classic escape.

**Global disruption**
13. **EMP Pulse** *(Lightning)* — everyone *else* shrinks and goes sluggish for a beat;
    you stay full size. (Tiny-Hitbox + speed cut applied to the field, not you.)
14. **Ground Pound** *(POW Block)* — a telegraphed shockwave staggers everyone touching
    the floor; jump on cue to dodge it.
15. **Blackout** *(Blooper/Ink)* — splatter every rival's screen with a cgame overlay,
    blinding them briefly while you slip away.

**Defensive / counters**
16. **Shockguard** *(Super Horn)* — a melee pulse that vaporizes any incoming
    projectile/shell and ragdolls close enemies. The designated **Skyfall counter** — the
    one thing that makes the blue-shell-equivalent fair.
17. **Mirror Shield** — for X s every projectile that hits you auto-bounces back at the
    shooter, no parry timing required. (Always-on extension of the MOUSE2 deflect.)

**Utility / steal / economy**
18. **Phantom Thief** *(Boo)* — go invisible *and* yank the active modifier off the
    nearest player for yourself.
19. **Momentum Coins** *(Coins)* — collectible speed cap: each coin nudges your max speed
    up a notch, and you shed a few when hit. A persistent, snowball-able catch-up economy
    layered under everything else.
20. **Roulette** *(the "?" Box)* — the meta item: rolls a *random* modifier from the
    entire pool (verbs + this tier), placement-weighted. The default contents of a generic
    pickup if you don't want hand-placed items.

### Arena items — reshape the ring

> A *third* shape. The verbs change **you**; the Mario Kart tier targets **other people**;
> arena items change the **space and the fight itself**. These are blade/weapon pickups,
> deployables you place in the ring, contested objectives, and hazards — built for the
> bullet-time katana duel in the deep-blue ring (`strafe64dm_1337`) and the vertical
> killbox. Most are **spawned entities or weapon swaps**, not `powerups[]` timers — a
> different mechanism (engine_spawn / weapon table / func_ brushwork), so this tier scales
> the arena without touching the buff plumbing.

**Blades & weapon pickups** (lean on the WP_SWORD / vectorgun economy)
1. **Odachi** — long-reach katana: slower swing, wider arc, out-spaces the standard blade.
2. **Twin Tantos** — dual short blades: faster combo cadence, shorter reach, more parry
   windows.
3. **Kusarigama** — sword on a chain: fuses grapple + slice; latch an enemy and reel into
   a kill. (Reuse the grapple trace + spring.)
4. **Phantom Edge** — every swing throws a crescent slice-shockwave: melee that reaches.
   (Spawn a short-lived deflectable arc projectile on swing.)
5. **Bulwark Blade** — guard-heavy weapon: enormous parry cone, feeble offense. The
   defensive pick for a comeback.

**Deployables you place in the ring**
6. **Trail Pylon** — drop nodes; the lattice trail-wall auto-strings a lethal fence
   between your pylons. Zone the arena by building.
7. **Deploy Ramp** — spawn a jump pad / launch ramp mid-fight to rewrite the flow lines.
8. **Cover Slab** — spawn a destructible wall segment; breaks under sustained sword/fire.
   Sightline control.
9. **Tether Anchor** — drop a grapple point anywhere, even mid-air, for a swing you set up
   yourself.
10. **Mag Mine Net** — lay a line of proximity nodes that ragdoll-launch on trip; arena
    denial.

**Hazards & environmental control**
11. **Spike Tile** — arm a floor panel that impales whoever stands on it after a telegraph.
12. **Gravity Well** — a placed sphere that drags players *and* projectiles toward it; turns
    a duel into a movement puzzle.
13. **Slow Bubble** — a localized bullet-time field: cross into it and your clock dilates.
    (Local `g_timeBind` zone — fight at the edge of it.)
14. **Updraft Vent** — a repositionable column of upward force for forcing the fight
    airborne (where the bots are weakest — a real edge).
15. **Mirror Pillar** — a reflective column that bounces deflected projectiles at chaotic
    angles. Risk/reward cover.

**Defensive / vision**
16. **Smoke Censer** — drop a line-of-sight-blocking cloud; combos hard with the invis /
    Phantom-Thief steal.
17. **Aegis Dome** — a placed bubble that eats all incoming projectiles for a few seconds;
    a held position, not a personal shield.

**Contested objectives (the ring identity)**
18. **Healing Shrine** — a point that heals whoever stands on it; forces fighters to
    *contest space* instead of camping.
19. **Time Crystal** — shatter it to freeze the whole arena except you for one beat. The
    duel-ending combo enabler; rare, contested, telegraphed on spawn.
20. **Echo Totem** — records the last 3 s of every fighter, then spawns ghost-replays of
    their own moves as solid decoys. Reuses the race-ghost record/replay buffer; turns the
    arena into a hall of mirrors.

### Selection notes
- **Three layers, two mechanisms.** (a) verb-unlocks = *personal flow*, best for solo
  courses & duels; (b) Mario Kart tier = *interactive party*, best for 3+ player chaos —
  both ride the same `powerups[]` timer plumbing. (c) Arena items = *space & duel control*,
  best for the ring / killbox — these are mostly spawned entities, weapon swaps, and
  func_ brushwork, a separate mechanism. Pick the pool per gamemode.
- The chaos tier (§7) and several Mario Kart / arena items are opt-in spice (a `g_spiceMods`
  cvar or course-tag gate), not in the default pool.
- Strong v1 shortlist by impact-to-effort: **Wallflower**, **Vector Blade**,
  **Kill Boost**, **Bullet Dancer** (verbs) + **Boost**, **Star**, **Homing Slice**,
  **Shockguard** (Mario Kart) + **Phantom Edge**, **Slow Bubble**, **Echo Totem** (arena —
  each reuses sword-projectile / `g_timeBind` / ghost-buffer systems near-wholesale).
- Anti-pattern to avoid: any idea whose pitch is "+30% of something." If it's a dial,
  it's a tuning cvar, not a modifier.

---

## 8. Open questions

- **Stacking:** old powerups stack duration. Should two Overdrive pickups stack the
  multiplier or just the timer? (Recommend: timer only — multipliers don't compound.)
- **Bot awareness:** bots path to powerup items already; do we want them to *use*
  movement modifiers intelligently (dash-chain on Phase)? Likely a follow-up via the
  bot-moveset cvars.
- **Per-player vs global feel:** Tempo touches the world clock. Confirm whether slow-mo
  is per-player or shared before scaling it from a pickup.
- **Rubber-band economy (Mario Kart tier):** should a generic pickup roll a *placement-
  weighted* random item (last place → game-changers, leader → bananas), or do we hand-place
  specific items on the map? Weighted-random needs a placement signal — frags in deathmatch,
  position in a race. Recommend: weighted-random for arena/party modes, hand-placed for
  designed flow courses.
- **Targeting the leader (Skyfall):** "who's in 1st" is trivial in a race (furthest along)
  but fuzzier in a frag arena (most frags? on a kill streak?). Pin the metric before
  building the blue-shell-equivalent — and ship Shockguard alongside it as the counter.
