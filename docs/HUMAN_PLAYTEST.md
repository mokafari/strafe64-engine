# STRAFE 64 — Human Playtest Guide

Everything the autonomous loop built (iters 15–30) that **needs a human at the
controls to validate**. Bots verified no-regression on traversal quality, but
the things below are feel-, skill-, or client-only and can't be bot-checked.
Work top to bottom; tick each box; jot what feels off.

## Launch

```sh
./run-openarena.sh            # windowed, main menu  (-p for the PSX look)
# then drop the console (~) and:
\map surf_64                  # the ★ surf core loop (also surf_7 / surf_1337 / surf_2025)
\map dojo_flow                # a bot movement course, for the moveset
```

Dedicated + connect, or `-b 4 <map>` for a bot match. `/exec strafe64` resets
the canonical ruleset after experimenting. All dylibs auto-deploy + re-sign on
launch (don't hand-copy one — they share networked layout).

---

## 1. The ★ surf core loop  (the headline — `\map surf_64`)

The whole game in one map: surf a chain of steep banked ramps, cross the
finish, bank points, respawn at the start, spend, go again.

- [ ] **Surf feel** — the ramps are 51° (non-walkable). Land on a ramp, hold a
      strafe key + steer into the slope: do you *slide and accelerate* rather
      than stand? This is the make-or-break. If it feels sticky/wrong, the ramp
      angle (`surf_ramp` in strafegen.py, currently 49–55°) is the dial.
- [ ] **Momentum hand-off** — at each transition pad between ramps, do you keep
      speed onto the next ramp, or get dumped? (Procedural chain, iter19.)
- [ ] **Lap loop** — cross the finish gate: it should teleport you back to the
      start (lap +1) and the run continues.
- [ ] Try the other seeds (`surf_7`, `surf_1337`, `surf_2025`) — each is a
      different procedural circuit (3–5 ramps).
- [ ] **Banked turn** (`\map surfturn_64`, iter32) — a steep banked 180° arc.
      Enter at the top mouth, lean into the bank: does it **carry your speed
      around the corner** (velodrome-style) and spit you out the bottom? This is
      the candidate corner geometry — if it rides well, it folds into the
      procedural line for true closed circuits with turns. The key unknown.

> Bots can't surf (AAS doesn't model air-strafe-on-ramps), so this section is
> entirely on you. It's the #1 thing to judge.

## 2. Lap → points → shop  (the WC3-mod-for-CS progression)

- [ ] On crossing the finish you should see a centerprint: `LAP n / time /
      +award / BANK total / type buy to spend`. The award scales with lap speed
      + a new-best bonus (iter17).
- [ ] **Shop**: drop console, type `buy` → it lists rocket/rail/bfg/armor/heal/
      airjump + costs + your score. Type `buy rocket` (etc.) with enough score —
      you get the weapon; score is deducted.
- [ ] **Persist across death**: buy a weapon, then `\kill`. After respawn you
      should **still have the bought weapon** (armor/heal are per-life, weapons
      persist — iter20).
- [ ] **`buy airjump`** (800) — the movement upgrade. After buying, in mid-air
      press jump a *second* time: you should get an extra air-jump you didn't
      have before (iter23). Buy MOVEMENT with lap points — the core fantasy.

## 3. Mission Report  (the death screen — die to see it)

`\kill` (or fall) on any map to bring up the NERV scorecard (iters 15/22/24/29):

- [ ] **TOP SPEED** (this life's peak), **STYLE n (PB m)** (this run vs session
      best), **MAP BEST** ups, **BEST LAP** time (your saved-ghost finish),
      an **S/A/B/C RANK** on peak speed.
- [ ] **Shop panel** on the same screen: SCORE + every item, coloured **green
      = affordable, amber = OWNED, dim = can't afford**. Buy from here (console)
      then `FIRE TO RUN AGAIN`.

## 4. Run mutators  (`g_mutator`, iters 26/28/30)

Set on a dedicated server or local: `\g_mutator 1` then `\map surf_64`.

- [ ] **1 LOW GRAVITY** — floaty, long air-strafes (great on surf).
- [ ] **2 RUSH** — everything ~40% faster (proven on bots: avgspd 256→313).
- [ ] **3 HEAVY** — snappier falls, less forgiving.
- [ ] Each should **announce on spawn** (centerprint) and show a **top-centre
      HUD label** tinted by feel (green/amber/red).
- [ ] **Daily**: `\g_mutator 9` → resolves to a per-day twist at map load
      (check the console log: `daily mutator -> N`). Same day = same twist.

## 5. Moveset  (any movement map — `\map dojo_flow`, iters earlier)

- [ ] **Bhop** — hold jump, chain hops, build speed past run cap.
- [ ] **Wall-run** — run along a wall (camera leans, STAT_WALLRUN).
- [ ] **Double / air jump** — fresh jump press mid-air.
- [ ] **Vault** — come at a ledge edge with speed → pull up and over keeping
      momentum (PM_CheckVault). *Not yet validated on a vaultable-ledge map —
      if you find a ledge, confirm it triggers.*
- [ ] **Strafe-jump to 800+ ups** — the skill ceiling. Achievable?

## 6. Identity / juice  (any map)

- [ ] NERV/MAGI amber HUD, LED matrix font, speed meter, combo/FLOW multiplier,
      shake + fov-punch on speed, race timer with TARGET ghost.
- [ ] **Standstill glitch** — stand still: the corruption effect should be
      *subtle* now (toned down). Too strong? Note it.
- [ ] **PSX look** (`-p`) — affine warp defaulted OFF (felt too harsh); the rest
      of the low-fi vibe on.
- [ ] **Tracker music** — jungle .mod/.xm/.it playback.

---

## Diagnostics you can run yourself (no play needed)

```sh
strafegen/viz.py dojo_ztrick --void 1   # tracers + time-heatmap + DEATH X-marks
strafegen/dojo.py                        # 4-archetype bot regression (self-stabilising)
strafegen/strafegen.py --surf 1337       # generate a fresh surf circuit
```

## Feedback → loop

Anything that feels wrong here is the highest-value input to the dev loop —
it re-opens concrete tuning work (ramp angle, award curve, mutator strength,
glitch intensity). The autonomous loop has converged on what it can verify
without you; **your playtest notes are now the steering wheel.**
