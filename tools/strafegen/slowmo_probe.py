#!/usr/bin/env python3
"""Slow-mo bot probe: run a movement archetype at timescale 1.0 vs a forced low
timescale and compare the (timescale-invariant) rate metrics. On a headless bot
server G_UpdateTimeBind early-returns for bots and never touches timescale, so a
forced `timescale` cvar sticks — letting us measure how bots degrade under the
uniform slow-mo the human drives in real play. If bots behaved identically when
slowed, avgspd / flowpct / stuckms would match; any gap is the glitch.

Usage: python3 slowmo_probe.py [arch] [ts] [dur_real_s]
"""
import sys, os, tempfile
import dojo

# pmove_fixed default 1 MIRRORS the shipped game: G_UpdateTimeBind forces it on
# whenever g_timeBind slow-mo is active, so the probe reflects real slow-mo feel.
# Set PMF=0 to A/B against the old timestep-dependent physics.
PMF = os.environ.get("PMF", "1")   # pmove_fixed: 1 = timestep-independent physics

def run(arch, ts, dur):
    cfg = dict(dojo.ARCHETYPES[arch])
    cfg["extra"] = list(cfg.get("extra", [])) + ["timescale", str(ts),
                                                 "pmove_fixed", PMF]
    batch = tempfile.mkdtemp(prefix=f"slowmo_{arch}_{ts}_")
    _, recs = dojo.run_scenario(arch, cfg, batch, dur, idx=7)
    p = dojo.profile(arch, recs)
    # stuck FRACTION = stuckMs / aliveMs, duration-invariant (unlike the cumulative
    # stuckMs, which scales with how long each bot lives). This is the apples-to-
    # apples slow-mo comparison: if bot game-logic is timescale-invariant, the
    # fraction is flat across timescales regardless of real-time run length.
    fr = [r["stuckms"] / r["durms"] for r in recs
          if r.get("durms", 0) > 0 and "stuckms" in r]
    p["stuckfrac"] = (sum(fr) / len(fr)) if fr else 0.0
    return p

def show(label, p):
    print(f"  {label:18}  n={p['n']:<3} avgspd={p['avgspd']:6.1f}  "
          f"maxspd={p['maxspd']:6.1f}  flow={p['flowpct']:5.1f}%  "
          f"air={p['airpct']:5.1f}%  stuckFRAC={p['stuckfrac']*100:5.1f}%  "
          f"frags={p['frags']}")

if __name__ == "__main__":
    arch = sys.argv[1] if len(sys.argv) > 1 else "flow"
    dojo.deploy_dylibs()
    # EQUAL real duration per timescale — stuck FRACTION is duration-invariant, so
    # no need to stretch the low-ts runs; equal wall-time keeps sample counts sane.
    dur = int(sys.argv[2]) if len(sys.argv) > 2 else 45
    print(f"=== slow-mo probe: {arch}  ({dur}s each) ===")
    for ts in [1.0, 0.5, 0.3]:
        show(f"timescale {ts}", run(arch, ts, dur))
