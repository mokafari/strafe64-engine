# Melee, Bullet-Time & Movement-Combat — Research
> Research for STRAFE 64 procedural arena tuning. Sources cited at bottom.

STRAFE 64's core loop: a sword/katana in bullet-time where **standing still nearly
freezes the world and swinging surges time forward** — so *speed = power, speed =
time*. Damage is speed-scaled (a standing hit barely scratches at the 0.85× floor;
a fast pass-by cleaves at up to 2.0×, red hit-marker at 1.5× / ≥500 ups). The level
must therefore *force motion* and *punish stopping* — the opposite of every cover
shooter. This doc mines melee, bullet-time and flow-combat games for arena geometry
and encounter rules, then maps them onto our run speed **320 ups** and sword reach
**~80u standing → ~160u at full speed**.

## TL;DR
- **Force forward motion, never reward stopping.** Doom Eternal's "push-forward
  combat" makes resources come *from* the enemy (glory-kill = health, chainsaw =
  ammo) so hiding is starvation. Our analogue is already built: stopping drops you
  to 0.85× damage and freezes time; moving fast gives 1.0–2.0× damage and flow-regen
  (heal ≥500 ups). The *level* must reinforce this — no safe corners to turtle in.
- **Melee engagement happens in a tight band.** Real execution/melee ranges are
  ~2–2.5 m (Titanfall execution at ~2.19 m). Our sword reaches 80u still / 160u fast
  (~1.5–3 m at Q3 scale ~52u/m). Design "kill pockets" — the spot where an approach
  lane delivers you into reach — at **~120–180u** wide so a moving slash connects but
  a standing one whiffs.
- **Approach lanes are the real weapon.** Ghostrunner/Severed Steel give you a
  *traversal route that ends on the enemy* — wall-run → slash, dive → slash. Geometry
  should funnel a fast line *through* the enemy's position, not stop short of it.
- **Verticality enables the air-slash.** Doom's "jungle gym" (distinct floors) and
  "canyon" (open, chaotic) vertical arenas exist so combat moves up/down. Put enemies
  at jump-pad apex and platform edges so you cleave them mid-air at peak speed.
- **Bullet-time turns each room into a frozen puzzle.** Superhot ("time moves only
  when you move") and Max Payne reward *reading* a stalled tableau then executing a
  flow line through it. Telegraph clearly (slow projectiles are visible), and let the
  player *plan the cut* — density can be higher because time is on their side while
  moving.
- **Pulse, don't carpet.** Doom marks discrete "encounters"; Max Payne design docs
  break a fight into **5 waves** escalating on a Fibonacci count, ~13–21 enemies
  before a small-medium arena feels crowded. Place combat *beats* at apex/landing
  along a run, with rest (pure movement) between — not a constant enemy soup.
- **Pillars + counter-circle prevent flanks but also enable juking.** A pillar lets a
  defender deny back-stabs by counter-strafing; for us, pillars are *juke posts* the
  attacker arcs around to keep speed up. Space them so a 320-ups arc fits (radius
  ~150–250u of clear floor around each).
- **Anti-turtle is a timer + geometry problem.** Arena PvP uses "dampening" (start at
  ~3 min) to break stalemates. Our timescale *is* the dampener: a turtle freezes
  themselves into uselessness. Reinforce with dissolving floors / momentum gates so
  standing literally drops you out of the fight.
- **Slice-gate enemies live on the movement line.** Clustertruck/Ghostrunner-style
  "cut-through" hazards belong *on the desire line* at spice beats — apex gates and
  2–3-enemy phrases — so killing is a side-effect of flowing, not a detour.
- **Bait aggression with powerups on the fast route.** Q3 item logic: place the
  reward (quad/health) where reaching it *costs commitment* and *adds noise/exposure*.
  Put the quad at the end of the most aggressive line so greed = speed.
- **Size for melee, not rail.** Sword duels want rooms big enough to circle-strafe a
  pillar (~600–1000u across) but small enough that re-approach after a whiff is one
  bhop chain (~1–2 s), not a long walk. Too big = ranged stalemate; too small =
  facehug chaos with no juke.
- **Reward flow, punish hesitation, every system.** Speed-scaled damage + flow-regen
  + time-surge-on-swing already align; the generator's job is to lay down geometry
  that makes the *fast* line and the *aggressive* line the *same* line.

## Melee in Fast Games

**Engagement distance is short and positional.** Titanfall 2 executions trigger from
within ~2.19 m and a 40° rear cone — 35% farther than a normal jump-kick, but still
melee-tight. The lesson: melee "connects" in a narrow shell around the target, and
*positioning into that shell* (behind, above, fast) is the skill. For STRAFE 64 our
shell is dynamic: 80u when still, 160u at speed. That means the same enemy is *out of
reach* standing and *in reach* at a sprint — geometry should deliver the player into
the 160u shell *while moving*, not park them at 100u where a standing swing whiffs.

**Doom Eternal: melee as a resource-and-tempo verb, not a finisher.** Glory kills are
context-melee that only unlock after enough damage, always drop health, and exist to
*keep you in the fray* — ammo from chainsaw, armor from flame-belch, health from
glory. The ethos ("stay moving, keep firing, never back down") is enforced by sparse
ammo so cover-camping starves you. **Why it matters for us:** we already invert
resource flow via flow-regen (heal while ≥500 ups) and speed-damage. The arena should
make the *only* way to feed those systems be a committed fast pass — i.e., no enemy is
killable from a standing hold.

**Ghostrunner: the route IS the attack.** One-hit katana, one-hit death; you "flow
together an array of quick moves" and the next objective is always kept in the FOV so
you never stop to wayfind. Wall-runs, slides and dashes are *synced in quick
succession* and combat resolves *inside* that traversal — you wall-run *to* the enemy
and slash on the way past. **Implication:** an arena that supports air-slashes needs a
clean traversal line whose terminus is an enemy at reach — a wall-run wall that ends
beside a slice-gate, a jump-pad whose apex passes through a gate.

**Devil Daggers / Hyper Demon: shrinking safe space forces motion.** A flat floating
platform where accumulating enemies eat the safe area, so you must keep moving and
keep spatial awareness. Hyper Demon adds an air-dash and dive-bomb that *stuns enemies
into launchpads*. **Implication:** density itself can be a motion-forcing pressure;
and "enemy as launchpad/momentum source" is a great fit for slice-gates that, when cut
at speed, surge time and fling you onward.

**Severed Steel: invincible-while-moving makes flow the defense.** Wall-run, slide,
dive can be chained; you're effectively invulnerable mid-stunt and bullet-time
recharges off kills, so skilled players spend whole levels airborne in slow-mo.
**Implication:** tie defensive value to motion (our parry + the freeze-when-still
already do the offensive half) — geometry that lets you *always* be mid-traversal
means you're rarely a static target.

**Mordhau/Chivalry: spacing and the 45° line.** Weapons swing in an arc; standing
*directly in front* of a swing is the longest (most-telegraphed) point, while *moving
45° off* exits the blade's tracers. Offense closes to facehug (hard to read);
defense holds distance (time to react). Short blades reward closing, long blades
reward spacing. **Implication for a single katana:** our reach grows with speed, so a
fast attacker is effectively wielding a "longer" blade — geometry should let the
attacker control distance (open approach lanes) while giving the defender pillars to
break the line. The 45° insight argues for *diagonal* approach lanes into a gate, not
head-on, so the player naturally arcs through reach.

## Bullet-Time / Time-Dilation Design

**Superhot: "time moves only when you move" turns rooms into puzzles.** Tight corners
and cover accommodate minute movement; each level is a plan-ahead puzzle of bullet
streams and weapon-snatching. Critically, *enemies are frozen until you move*, so the
space reads as a solvable tableau. **STRAFE 64 is the kinetic inverse:** we *want* you
to move (movement = damage = time), so where Superhot rewards stillness-then-burst, we
reward *continuous* motion. But the spatial lesson transfers: design rooms as
**readable frozen moments** — clear sightlines to every threat so the player can plan
the cut, then execute a flow line that resolves the whole tableau in one pass.

**Max Payne: bullet-time as the outnumbered-equalizer; arenas sized to wave count.**
Slow time to see bullets while aiming in real time — your friend when enemies rush or
ambush. A surviving design doc shows a small-medium arena suits ~13–21 enemies before
crowding, with fights broken into ~5 waves escalating on a Fibonacci count.
**Implication:** time-dilation lets you raise *apparent* density safely because the
player has time to act while moving. Budget our slice-gate/enemy counts per beat with
a similar wave shape (e.g. 1 → 1 → 2 → 3 → 5 across a run's spice beats), and size
combat pockets to the wave they hold.

**My Friend Pedro / Quantum Break: slow-mo as a near-infinite, kill-fed flow meter.**
Pedro's "Focus" is toggleable and "pretty much never runs out"; kills extend it,
levels are "bloody playgrounds" of chained stylish kills and environmental hazards.
**Implication:** our time-surge-on-swing is the same loop — each cut buys forward time.
The level should provide *chainable opportunities* (gates spaced so each kill's
time-surge carries you into the next), making a whole run a single sustained flow.

**Telegraphing under time control.** Because the world is slow when you're slow,
projectiles are readable — so encounters can *afford* more incoming fire as long as it
is *visible and dodgeable on the flow line*. Avoid hitscan-from-offscreen (no time to
read); prefer slow, glowing, lateral threats the player weaves through (matches our
deflectable-projectile / bolt-trail systems).

## Dueling & Close-Quarters Geometry

**Room size: big enough to circle, small enough to re-engage.** WoW-arena lore: open
arenas with few pillars (Tol'viron — 3 small pillars, max exposure) punish mistakes;
verticality (Tiger's Peak — flanking platforms + a central pit "where melee thrives")
rewards positioning. A good duel zone has "open space *and* trees/pillars" to create
tactical lanes *and* encourage engagement. For a sword duel at our scale, target a
floor span of **~600–1000u**: enough to circle-strafe a central pillar at 320 ups
(circumference of a 200u-radius circle ≈ 1257u ≈ ~4 s lap — a real juke), but small
enough that a whiffed pass re-engages within one bhop chain (~1–2 s).

**Pillars do double duty.** Defensively, hugging a pillar and *counter-circling* the
opposite way denies the attacker a back-stab/flank. Offensively (our framing), a
pillar is a *juke post* the attacker arcs around to maintain speed and reach (remember:
reach scales with speed, so the attacker *wants* to keep moving). Space pillars so a
~150–250u-radius arc of clear floor surrounds each, and so adjacent pillars are
~300–500u apart (close enough to chain juke-to-juke, far enough not to clip).

**Height advantage and the air-slash.** Elevated platforms flanking a central pit give
the high player a diving approach into the 160u speed-shell. Place 1–2 platforms at
~150–300u above the floor with jump-pad or wall-jump access so a duel naturally cycles
floor ↔ air, and an air pass-by at peak speed lands the 1.5–2.0× cleave.

**Avoiding turtle stalemates.** PvP solves this with dampening timers (~3 min). We have
a *better* anti-turtle baked in: standing still freezes your own clock and drops you to
the 0.85× damage floor — a turtle is self-defeating. Reinforce it geometrically:
(a) no fully enclosed safe nook (every cover has a flanking lane), (b) dissolving /
hot floors that punish dwelling, (c) the reward (quad/health) lives on the aggressive
line so passivity = falling behind. The duel should make *the fast circler* strictly
beat *the holder*.

## Speed=Damage & Flow-State Combat

**Our canonical speed ladder** (from `docs/MOVEMENT.md`, keep all systems coherent):
320 ups = run speed (1.0× damage, flow 0); **500 ups = flow** (1.5× hits, red
hit-marker, flow-regen heals); **768 ups = peak flow** (vignette/speedometer max).
Speed-damage runs 0.85× near-still → 1.0× at 320 → +50% per extra 320 ups of velocity,
capped 2.0×. **Design corollary:** a combat beat is only satisfying if the player can
*arrive at ≥500 ups*. Every gate/enemy placement must sit at the end of a section that
can build to flow — i.e., after a descent, a slide-out, a pad, or a bhop straight —
never at a spot that requires stopping to line up.

**The level pushes continuous motion.** Mechanisms observed: Doom's resource-from-combat
(can't camp), Devil Daggers' shrinking safe space, Ghostrunner's keep-objective-in-FOV,
Severed Steel's invincible-while-moving. STRAFE 64's toolkit to push motion: dissolving
floors, hot-floor damage when slow (the inverse of flow-regen), momentum gates
(velocity-preserving portals), and the time-freeze penalty. **Generator rule:** every
combat pocket should have an *entrance that grants speed* and an *exit that demands it*
(a gap to clear, a gate to cut, a pad to hit) so the player flows *through* rather than
*into-and-stop*.

**Pulse the combat into the run.** Doom delineates discrete "encounters" with movement/
puzzle connective tissue at high tempo. Our flow-loop (Descend → Tighten → Revector →
Slice → Release) already prescribes this: combat *beats* at apex and landing, rest
(pure movement) between. Carpet-bombing enemies kills flow; a *pulse* (1–3 cut targets
per beat, at apex/landing) sustains it. Pace beats so each kill's time-surge bridges to
the next — roughly one beat every **~2–4 s** of run, escalating in count toward a
section's climax (Fibonacci-ish: 1,1,2,3,5).

## Slice-Gate Enemies & Combat Pacing

**Cut-through enemies belong on the desire line.** Clustertruck/Ghostrunner hazards are
obstacles you *pass through by destroying* — the kill is a by-product of correct
movement, not a detour. Our slice-gate (sliceable flow-gate entity, `FL_SLICE_GATE`)
is exactly this. Key learned constraint (from memory): a *standing* slice does ~0 damage
(speed-damage floor), so a slice-gate **must be placed where the player arrives moving**
— at an apex, off a pad, on a slide-out — or it becomes an unkillable wall. Never place
a slice-gate at a dead stop or behind a required precision-stand.

**Placement by beat type:**
- **Apex gates** — at jump-pad / wall-jump peak, where speed and height are maxed; the
  cut lands the air-slash and the time-surge extends the arc onward.
- **Flow-phrase gates** — 2–3 enemies strung along a fast straight or banked-wall line,
  each spaced so one kill's time-surge carries into the next cut (~120–200u apart along
  the line at flow speed).
- **Release gates** — a single big cut at a section's end that surges you into the next
  section (Descend→…→Release).
- **Openers/rest** — *no* gates; pure movement so the player builds to ≥500 ups before
  the first cut.

**Density discipline.** Following Max Payne's wave shape and Doom's pulse: per combat
section, escalate gate count 1 → 1 → 2 → 3 → 5 across spice beats; keep openers and
flow segments enemy-free as rest. A combat pocket sized small-medium (our ~600–1000u
span) tops out around a handful of simultaneous threats before it reads as a wall, not
a flow.

## Implications for STRAFE 64

- **Engagement geometry tuned to a *moving* 160u reach, not a standing 80u.** Funnel
  the player into the kill shell *via a fast lane*. Approach lanes should be ~diagonal
  (the 45° insight) so the player arcs through reach at speed and the cut connects at
  ≥1.5×. A gate parked where you can only reach it standing (80u, 0.85×) is a design
  bug.
- **Every combat pocket = speed-in / speed-out.** Entrance is a descent/pad/slide-out
  that delivers ≥500 ups; exit is a gap/gate/pad that demands you keep it. No
  enter-and-stop pockets, no enclosed turtle nooks.
- **Verticality for air-slashes.** 1–2 platforms ~150–300u above floor with pad/wall
  access; put apex gates at pad peaks so the air pass-by lands the 1.5–2.0× cleave.
- **Pillars as juke posts, sized to a 320-ups arc.** ~150–250u clear radius around each;
  pillars ~300–500u apart so juke-to-juke chains. This both prevents flank-camping and
  gives the attacker a way to hold speed and reach.
- **Anti-turtle is geometry + our time-freeze.** Standing already self-penalizes
  (0.85× + frozen clock + hot-floor). Reinforce: flanking lane on every cover,
  dissolving/hot floor in dwell spots, reward (quad/health) on the most aggressive line.
- **Bait aggression with placement.** Quad/major pickup at the *end of the fastest,
  most-committed line* (Q3 logic: reward costs commitment + exposure). Greed becomes
  speed becomes damage.
- **Pulse combat into the movement run.** Beats at apex/landing, 1–3 cut targets per
  beat, escalating toward a climax; openers and flow segments left as rest so the player
  can hit flow speed before each beat.
- **Duel arenas: 600–1000u span, central pillar(s) + flanking high ground.** Big enough
  to circle-strafe (~4 s lap), small enough to re-engage in one bhop chain after a whiff.
  Avoid both ranged-stalemate (too big/open) and facehug-soup (too small).
- **Target telemetry:** high midair-kill % (air-slashes landing), high mean
  kill-speed (cuts at ≥500 ups, not standing), short re-engage gaps, low "stuck/standing"
  time, frags concentrated at apex/landing beats. A run where most kills happen below 320
  ups means the geometry is letting players turtle.

## TUNING KNOBS → strafegen

| Finding | Generator knob | Suggested value / target |
|---|---|---|
| Melee connects in a moving 160u shell, whiffs standing at 80u | Gate/enemy distance from approach-lane centerline | place gate so player passes within **120–180u** at speed; never reachable only from a standing pose |
| Approach lanes should arc through reach (45° line) | Lane→gate angle | diagonal approach (~30–60° off head-on), not perpendicular dead-stop |
| Every pocket needs speed-in / speed-out | Pocket entrance/exit feature | entrance = pad/descent/slide-out (delivers ≥500 ups); exit = gap/gate/momentum-portal (demands speed) |
| Duel/melee arena size | Combat-pocket floor span | **600–1000u** across (circle-strafe a pillar ~4 s, re-engage ≤2 s) |
| Pillars as juke posts (hold speed + deny flank) | Pillar clear-radius / spacing | **150–250u** clear floor around each; pillars **300–500u** apart; 1 central + 2–3 peripheral in a duel pocket |
| Air-slash verticality | Elevated platform count / height / access | **1–2** platforms at **150–300u** above floor, each with jump-pad or wall-jump access |
| Apex gates land the air-slash | Slice-gate at pad/wall-jump peak | place gate at jump trajectory apex (max speed+height); 1 per major pad on combat sections |
| Flow-phrase gate spacing (chain time-surges) | Gates-per-line along a fast straight | **2–3** gates, **120–200u** apart along the flow line |
| Pulse not carpet; wave escalation | Slice-gate count per spice beat | escalate **1 → 1 → 2 → 3 → 5** across a section; openers/flow = **0** |
| Combat beat cadence | Beats per run / spacing in time | one combat beat every **~2–4 s** of run, at apex/landing only |
| Pocket density ceiling | Max simultaneous threats in a 600–1000u pocket | cap **~5** before it reads as a wall (Max-Payne 13–21 is per *level*, scaled down per pocket) |
| Standing slice does 0 dmg → never place at a stop | Slice-gate placement validity check | reject gates whose only reachable approach is <320 ups; require an upstream speed source |
| Bait aggression | Powerup (quad/health/flow) placement | on the **end of the fastest/most-committed line**; adds exposure/noise to grab |
| Anti-turtle geometry | Cover-nook check + hazard floors | every cover has a flanking lane; dissolving/hot floor in dwell spots; no enclosed safe nook |
| Reward continuous motion | Floor hazard / momentum gate density on long sections | dissolving floor or momentum-portal exit on rest segments so dwelling drops you out |
| Telemetry — flow combat working | Bot playtest metrics | high **midair-kill %**, mean **kill-speed ≥500 ups**, low standing-time, frags clustered at apex/landing; flag if most kills <320 ups |

## Sources
- [Execution — Titanfall Wiki](https://titanfall.fandom.com/wiki/Execution) — execution range ~2.19 m, 40° rear cone, 35% farther than jump-kick (melee shell is tight + positional).
- [Termination — Titanfall Wiki](https://titanfall.fandom.com/wiki/Termination) — melee finishers tied to positioning/tactical abilities.
- [Pushing "Push Forward" Combat With Gameplay — Game Developer](https://www.gamedeveloper.com/game-platforms/pushing-push-forward-combat-with-gameplay) — Doom Eternal resource-from-combat; remove anything that takes the player out of the fray; no cover-camping.
- [Design of Doom Eternal — Adam Saltsman](https://blog.adamatomic.com/post/613311014289768448/design-of-doom-eternal) — corridors linking wave arenas, encounters delineated, high-tempo connective tissue.
- [Doom Eternal — Grokipedia](https://grokipedia.com/page/Doom_Eternal) — "jungle gym" (distinct floors) vs "canyon" (open/chaotic) vertical arena types; platforming punctuates combat without halting pace.
- [Doom Eternal combat loops — Mastering DOOM Eternal](https://cuboldgaming.com/blogs/gaming/mastering-doom-eternal-weapons-movement-and-glory-kills) — glory kill = health, chainsaw = ammo, flame-belch = armor; stay moving.
- [Ghostrunner devs interview — Inverse](https://www.inverse.com/gaming/ghostrunner-interview-cyberpunk-hotline-miami) — "cyberpunk Hotline Miami with a sword"; one-hit kill flow philosophy.
- [Ghostrunner review — Explosion Network](https://explosionnetwork.com/ghostrunner-review/) — keep next objective in FOV; sync wall-runs/slides/swings; one-hit katana, route = attack.
- [Devil Daggers — Wikipedia](https://en.wikipedia.org/wiki/Devil_Daggers) — flat floating platform; shrinking safe space forces motion; jump-strafe speed boost, fire-down double jump.
- [HYPER DEMON review — Screen Rant](https://screenrant.com/hyper-demon-review-fps-game-sorath-devil-daggers/) — speed-not-survival scoring; air-dash; dive-bomb stuns enemies into launchpads.
- [Severed Steel on Steam](https://store.steampowered.com/app/1227690/Severed_Steel/) — fluid stunt system, bullet-time, destructible voxels; invincible while doing parkour.
- [Severed Steel review — bit-tech](https://www.bit-tech.net/reviews/gaming/pc/severed-steel-review/1/) — chain wall-run → dive → slide; bullet-time refills off kills; melee keeps combos alive.
- [Footwork — Mordhau Wiki](https://mordhau.fandom.com/wiki/Footwork) — footwork as survival; constant movement to read/react; 45° off a swing exits the tracers.
- [Mordhau Fundamentals of Dueling — Steam guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2406953105) — offense closes to facehug (hard to read); defense holds distance (time to react); short vs long blade spacing.
- [SUPERHOT — Wikipedia](https://en.wikipedia.org/wiki/Superhot) — "time moves only when you move"; only deliberate movement advances time.
- [Level Design of Video Games – SUPERHOT — battzcave](https://battzcave.wordpress.com/2016/06/11/leveldesignofvideogames08-superhot/) — tight corners/cover for minute movement; each level a plan-ahead puzzle; enemies frozen tableau.
- [Max Payne level design — Maddieman Games](https://maddieman.wordpress.com/tag/max-payne-level-design/) — small-medium arena ~13–21 enemies before crowded; fights split into ~5 waves escalating on Fibonacci count.
- [Bullet Time — Max Payne Wiki](https://maxpayne.fandom.com/wiki/Bullet_Time) — slow time while aiming real-time; the outnumbered-equalizer in rushes/ambushes.
- [My Friend Pedro review — GodisaGeek](https://godisageek.com/reviews/friend-pedro-review/) — toggleable Focus slow-mo that "never runs out"; kills extend it; levels as chain-kill playgrounds.
- [Flow — The Level Design Book](https://book.leveldesignbook.com/process/layout/flow) — flow = speed + direction (smooth vs sharp) + wayfinding + metrics; fast games want smooth generous turns; desire lines & circulation/lanes.
- [Q3A Item Placement Guide — Quake3World](https://www.quake3world.com/forum/viewtopic.php?t=50729) — duel maps ~125–175 HP; rewards placed to cost commitment/exposure; more health if open or very vertical.
- [Guide to Melee PvP / circle-strafing — narkive](https://alt.games.warcraft.narkive.com/tyaq3ehD/guide-to-melee-pvp-and-circle-strafing-or-twitch-gameplay) — hug a pillar/wall + counter-circle to deny flanks; geometry of circle-strafe duels.
- [Mists of Pandaria PvP arena guide — Blazingboost](https://blazingboost.com/wow-classic-mists-of-pandaria/mists-of-pandaria-classic-pvp-guide) — verticality (flanking platforms + central melee pit) vs open low-cover arenas; dampening (~3 min) breaks turtle stalemates.
