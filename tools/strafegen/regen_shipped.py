#!/usr/bin/env python3
"""regen_shipped — rebuild the shipped strafegen maps (in the current default
theme, baked) from one manifest, a few bakes in parallel.

    python3 regen_shipped.py OUT_DIR [--jobs 3] [--only NAME ...]

The manifest maps each shipped pk3 name back to the generator arguments its
name encodes. Deliberately NOT here: dojo_* (fixed bot-dojo measurement
baselines -- re-theming them invalidates past runs), domtest_* (bake tests),
lattice_arena_iter (hand-tuned iteration), and third-party study maps.
Writes OUT_DIR/<name>.pk3 + OUT_DIR/logs/<name>.log, then the shared pak.
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))

MANIFEST = {}
for s in (1337, 2026, 314, 4201, 4242, 64, 6417, 7, 7700, 911):
    MANIFEST[f"strafe64_{s}"] = [str(s)]
for s in (1337, 7):
    MANIFEST[f"strafe64_{s}_x3"] = [str(s), "--length", "3"]
for d in (20260611, 20260613):
    MANIFEST[f"strafe64_daily_{d}"] = [str(d), "--length", "3",
                                       "--name", f"strafe64_daily_{d}"]
MANIFEST["strafe64cb_7700"] = ["7700", "--combat"]
for s in (1337, 7, 7140):
    MANIFEST[f"strafe64dm_{s}"] = [str(s), "--arena"]
for s in (7001, 8224, 8225, 8226, 8228, 8232, 8236, 9000):
    MANIFEST[f"strafe64kb_{s}"] = [str(s), "--killbox"]
MANIFEST["strafe64kb_8224_d2"] = ["8224", "--killbox", "--difficulty", "2"]
MANIFEST["strafe64kb_court_8240"] = ["8240", "--killbox", "--arch", "court"]
MANIFEST["strafe64tr_7"] = ["7", "--trucks"]
MANIFEST["strafe64tr_7_d2"] = ["7", "--trucks", "--difficulty", "2"]
for s in (1337, 2025, 64, 7):
    MANIFEST[f"surf_{s}"] = [str(s), "--surf"]
MANIFEST["lattice_arena_1337"] = ["1337", "--latticearena"]


def build(name, args, out):
    log = os.path.join(out, "logs", name + ".log")
    with open(log, "w") as fh:
        r = subprocess.run([sys.executable, "strafegen.py"] + args
                           + ["--pk3", "--bake", "--out", out],
                           cwd=HERE, stdout=fh, stderr=subprocess.STDOUT)
    ok = r.returncode == 0 and os.path.isfile(os.path.join(out, name + ".pk3"))
    aas = ok and "aas=True" in open(log).read()
    print(f"{'OK ' if ok else 'FAIL'} {name}{'' if aas or not ok else '  (NO AAS)'}",
          flush=True)
    return name, ok, aas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    os.makedirs(os.path.join(out, "logs"), exist_ok=True)
    todo = {k: v for k, v in MANIFEST.items() if not a.only or k in a.only}
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(lambda kv: build(kv[0], kv[1], out), todo.items()))
    subprocess.run([sys.executable, "strafegen.py", "--emit-shared", out], cwd=HERE)
    bad = [n for n, ok, aas in res if not ok or not aas]
    print(f"done: {len(res) - len(bad)}/{len(res)} ok" + (f"; check {bad}" if bad else ""))


if __name__ == "__main__":
    main()
