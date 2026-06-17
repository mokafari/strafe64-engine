#!/usr/bin/env python3
"""Unit tests for the pure (no-engine) logic in engine_api.py.

Covers parsing/computation that doesn't need a running engine, so regressions in
the big module are caught fast. Stdlib only — run: python3 test_engine_api.py
(For the live end-to-end surface use engine_api.selftest() / engine_selftest.)
"""
import math
import tempfile
from pathlib import Path

import engine_api as E

_passed = _failed = 0


def check(name, cond, detail=""):
    global _passed, _failed
    if cond:
        _passed += 1
    else:
        _failed += 1
        print(f"  FAIL {name}  {detail}")


# --- pure helpers -----------------------------------------------------------
check("strip_colors", E.strip_colors("^1red^7white") == "redwhite")
check("cvar_print regex", (m := E._CVAR_PRINT.search('"pm_x" is:"70"')) and m.group(1) == "70")
check("live_home name-safe", E.live_home("a b/c").endswith("a_b_c"))
check("pid_alive(0) false", E._pid_alive(None) is False and E._pid_alive(0) is False)
check("free_port returns int", isinstance(E.free_port(), int))
check("free_port excludes", E.free_port(exclude={E.free_port()}) != "" )  # smoke: no raise

# --- palettes ---------------------------------------------------------------
check("MOVEMENT_CVARS shape", all("range" in v and "doc" in v for v in E.MOVEMENT_CVARS.values()))
check("EFFECT_CVARS shape", all("range" in v and "restart" in v for v in E.EFFECT_CVARS.values()))

# --- source_constants (reads bg_pmove.c / bg_local.h, read-only) ------------
sc = {c["name"]: c for c in E.source_constants()}
check("source_constants found pm_strafeAccelerate", "pm_strafeAccelerate" in sc)
check("source_constants marks live", sc.get("pm_strafeAccelerate", {}).get("live") is True)
check("source_constants rebuild-only", sc.get("pm_wallJumpKick", {}).get("live") is False)
check("source_constants has a #define", any(c["kind"] == "define" for c in sc.values()))

# --- config_overrides (reads g_main.c + cfgs, read-only) --------------------
ov = E.config_overrides()
check("config_overrides catches g_timeBindMin",
      any(o["cvar"] == "g_timeBindMin" for o in ov), f"got {ov}")

# --- _aggregate_playtest (pure) ---------------------------------------------
recs = [
    {"ev": "death", "mod": "MOD_FALLING", "flowpct": 50, "airpct": 90, "maxspd": 400,
     "maxbhop": 5, "wallrunpct": 0, "wj": 0, "dj": 1, "stuckms": 100},
    {"ev": "finish", "racems": 12000, "flowpct": 60, "airpct": 95, "maxspd": 500,
     "maxbhop": 8, "wallrunpct": 10, "wj": 2, "dj": 1, "stuckms": 50},
]
rep = E._aggregate_playtest(recs, "testmap")
check("aggregate bot_runs", rep["bot_runs"] == 2)
check("aggregate flow mean", abs(rep["flow_pct"] - 55.0) < 0.1)
check("aggregate has verdict + completion_note", "verdict" in rep and "completion_note" in rep)
check("aggregate empty → note", E._aggregate_playtest([], "m")["bot_runs"] == 0)

# --- collage (Pillow grid from synthetic frames) ----------------------------
try:
    from PIL import Image
    d = tempfile.mkdtemp(prefix="enginetest_")
    paths = []
    for i in range(3):
        p = Path(d) / f"f{i}.jpg"
        Image.new("RGB", (160, 90), (i * 40, 80, 120)).save(p)
        paths.append(str(p))
    # collage is a method; call it unbound with a minimal stub carrying _clips_dir
    class _Stub:
        def _clips_dir(self):
            return Path(d)
    out = E.EngineSession.collage(_Stub(), paths, out=str(Path(d) / "c.jpg"),
                                  cols=2, labels=["a", "b", "c"], title="t")
    w, h = Image.open(out).size
    check("collage produces image", w > 0 and h > 0, f"{w}x{h}")
except ImportError:
    print("  (skipped collage test — Pillow not available)")

# --- look_at geometry (pure math via a stub) --------------------------------
# straight down: eye above target → pitch ~ +90, yaw 0
captured = {}
class _CamStub:
    def setviewpos(self, x, y, z, yaw=0.0, pitch=None):
        captured.update(yaw=yaw, pitch=pitch)
E.EngineSession.look_at(_CamStub(), (0, 0, 100), (0, 0, 0))
check("look_at straight down → pitch ~90", abs(captured.get("pitch", 0) - 90) < 1,
      f"pitch={captured.get('pitch')}")

print(f"\n{_passed} passed, {_failed} failed")
raise SystemExit(1 if _failed else 0)
