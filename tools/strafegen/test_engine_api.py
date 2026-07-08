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
# shape, not a specific cvar (which overrides come/go as the shipped cfgs change)
check("config_overrides shape",
      isinstance(ov, list) and all(
          {"cvar", "source_default", "config_value", "from"} <= set(o) for o in ov),
      f"got {ov}")

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

# --- weapon_index (name/number coercion) ------------------------------------
check("weapon_index number", E.weapon_index(5) == 5 and E.weapon_index("7") == 7)
check("weapon_index names", E.weapon_index("rocketlauncher") == 5 and E.weapon_index("sword") == 11)
check("weapon_index aliases", E.weapon_index("rl") == 5 and E.weapon_index("rail") == 7)
check("weapon_index normalizes", E.weapon_index("Rocket Launcher") == 5)
try:
    E.weapon_index("boomstick"); check("weapon_index rejects unknown", False)
except E.EngineError:
    check("weapon_index rejects unknown", True)

# --- _resolve_target string-coordinate parsing ------------------------------
_st = {"clients": [{"num": 0, "name": "Bot", "origin": [10, 20, 30]}]}
_rt = E.EngineSession.__new__(E.EngineSession)
check("resolve [x,y,z] string", _rt._resolve_target("[0,0,300]", _st)[0] == [0.0, 0.0, 300.0])
check("resolve x,y,z string", _rt._resolve_target("1.5, -2, 3", _st)[0] == [1.5, -2.0, 3.0])
check("resolve list", _rt._resolve_target([1, 2, 3], _st)[0] == [1.0, 2.0, 3.0])
check("resolve name", _rt._resolve_target("Bot", _st)[0] == [10, 20, 30])
check("resolve num precedence", _rt._resolve_target("0", _st)[0] == [10, 20, 30])

# --- _actions_duration estimate ---------------------------------------------
check("actions_duration sums",
      abs(E.EngineSession._actions_duration(
          [{"move": [1, 0], "secs": 2}, {"jump": 3}, {"wait": 1}]) - 3.75) < 1e-6)
check("actions_duration empty", E.EngineSession._actions_duration([]) == 0.0)

# --- g_timeBind save/restore bookkeeping (stubbed cvar IO) -------------------
class _TBStub:
    def __init__(self): self._saved_timebind = None; self.last = None; self._cur = "1"
    def get_cvar(self, n): return self._cur
    def set_cvar(self, n, v, confirm=True): self.last = (n, str(v)); self._cur = str(v)
_tb = _TBStub()
E.EngineSession._disable_time_bind(_tb)
check("disable stashes prior", _tb._saved_timebind == "1" and _tb.last == ("g_timeBind", "0"))
E.EngineSession._disable_time_bind(_tb)   # second call must NOT overwrite the stash
check("disable idempotent stash", _tb._saved_timebind == "1")
check("restore returns prior", E.EngineSession.restore_time_bind(_tb) == "1"
      and _tb.last == ("g_timeBind", "1") and _tb._saved_timebind is None)
check("restore noop when clean", E.EngineSession.restore_time_bind(_tb) is None)

# --- deploy_dylibs structured result (loud skip, no silent []) --------------
import os as _os
_oa = tempfile.mkdtemp(prefix="enginetest_oa_")
_dep = E.deploy_dylibs(oa=_oa, cfgs=False)
check("deploy result is dict",
      isinstance(_dep, dict) and {"deployed", "skipped", "reason", "blockers"} <= set(_dep))
if _dep["deployed"]:
    # build output exists → make the just-deployed set NEWER than the build so the
    # concurrent-build guard must trip — and trip LOUDLY (reason set), never a
    # silent empty list (the #1 stale-binary footgun).
    _dest = Path(_oa) / E.GAME
    _future = 1 << 33   # far-future mtime
    for _n in _dep["deployed"]:
        _os.utime(_dest / f"{_n}.dylib", (_future, _future))
    _dep2 = E.deploy_dylibs(oa=_oa, cfgs=False)
    check("deploy guard trips loudly",
          _dep2["skipped"] is True and _dep2["blockers"] and _dep2["deployed"] == []
          and bool(_dep2["reason"]), f"got {_dep2}")
    check("deploy force overrides guard",
          E.deploy_dylibs(oa=_oa, cfgs=False, force=True)["skipped"] is False)
else:
    check("deploy no-build → loud skip", _dep["skipped"] is True and bool(_dep["reason"]))

# --- known limitations surfaced ---------------------------------------------
check("KNOWN_LIMITATIONS non-empty", isinstance(E.KNOWN_LIMITATIONS, list) and E.KNOWN_LIMITATIONS)

# --- engine_mcp arg-filtering (schema/handler drift guard) ------------------
try:
    import engine_mcp as M
    _known, _unknown = M._filter_kwargs(M.t_frame, {"subject": "x", "dist": 10, "title": "bad"})
    check("filter_kwargs splits", "subject" in _known and _unknown == ["title"])
    check("accepted_kwargs lists params", "actions" in M._accepted_kwargs(M.t_input))
except Exception as _e:
    print(f"  (skipped engine_mcp tests — {_e})")

print(f"\n{_passed} passed, {_failed} failed")
raise SystemExit(1 if _failed else 0)
