#!/usr/bin/env python3
"""loglint — score STRAFE 64 engine console logs into a recurring-issue table.

Sweeps engine_console.log files (default: every ~/.strafe64-engine/<home>/baseoa/),
normalizes lines (colour codes stripped, numbers folded), classifies severity,
filters known-benign noise, and prints a table sorted by score. Patterns are
split into RECENT (a log touched in the last --days days) vs historical-only,
so long-fixed spam (e.g. the 6k-line weapon-out-of-range burst from before the
botlib sword fix) doesn't drown live problems.

    tools/loglint.py                      # sweep all engine homes
    tools/loglint.py path/to/console.log  # lint one log
    tools/loglint.py --days 3 --top 30
    tools/loglint.py --strict             # exit 1 if any HIGH issue is recent

Severity: HIGH = errors that break a load (Sys_Error, ERROR:, failed to load);
MED = engine WARNINGs; LOW = soft "couldn't find" grumbles.
"""

import argparse
import glob
import os
import re
import sys
import time
from collections import defaultdict

COLOR = re.compile(r"\^[0-9a-zA-Z]")
NUMS = re.compile(r"\d+")

# (regex, severity) — first match wins. Patterns are matched on the RAW line.
RULES = [
    (re.compile(r"Sys_Error|ERROR:", re.I), "HIGH"),
    (re.compile(r"failed to load|Failed to load", re.I), "HIGH"),
    (re.compile(r"WARNING", re.I), "MED"),
    (re.compile(r"couldn.t|can.t find|missing|out of range|invalid|not defined", re.I), "LOW"),
]

# Known-benign noise — matched on the raw line, dropped before scoring.
BENIGN = [
    re.compile(r"NET_IP6?Socket: bind: Address already in use"),   # concurrent engines
    re.compile(r"couldn.t exec q3(config|config_server|history)", re.I),  # first boot
    re.compile(r"Couldn.t read q3history", re.I),
    re.compile(r"Secure coding is automatically enabled"),          # macOS notice
    re.compile(r"NET_JoinMulticast"),                                # no multicast on lo
    re.compile(r"players/beret/spec_skin"),                          # stock OA pak ref
    re.compile(r"sound/player/skelebot/taunt", re.I),                # stock OA pak ref
]

SEV_WEIGHT = {"HIGH": 100, "MED": 10, "LOW": 1}


def classify(line):
    for rx, sev in RULES:
        if rx.search(line):
            return sev
    return None


def normalize(line):
    line = COLOR.sub("", line.strip())
    return NUMS.sub("N", line)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="*", help="log files (default: all engine homes)")
    ap.add_argument("--days", type=float, default=7.0,
                    help="a log modified within DAYS counts as recent (default 7)")
    ap.add_argument("--top", type=int, default=20, help="rows to print (default 20)")
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 if any HIGH-severity issue appears in a recent log")
    args = ap.parse_args()

    paths = args.logs or sorted(
        glob.glob(os.path.expanduser("~/.strafe64-engine/*/baseoa/engine_console.log")))
    if not paths:
        print("loglint: no logs found", file=sys.stderr)
        return 2

    cutoff = time.time() - args.days * 86400
    # pattern -> [count, sev, recent_count, example_home]
    table = defaultdict(lambda: [0, "LOW", 0, ""])
    scanned = 0
    for path in paths:
        try:
            recent = os.path.getmtime(path) >= cutoff
            with open(path, errors="replace") as f:
                lines = f.readlines()
        except OSError:
            continue
        scanned += 1
        home = path.split(os.sep)[-3] if path.count(os.sep) >= 2 else path
        for line in lines:
            if any(rx.search(line) for rx in BENIGN):
                continue
            sev = classify(line)
            if not sev:
                continue
            key = normalize(line)
            row = table[key]
            row[0] += 1
            if SEV_WEIGHT[sev] > SEV_WEIGHT[row[1]]:
                row[1] = sev
            if recent:
                row[2] += 1
            row[3] = home

    rows = sorted(table.items(),
                  key=lambda kv: (kv[1][2] > 0, SEV_WEIGHT[kv[1][1]] * kv[1][0]),
                  reverse=True)

    print(f"loglint: {scanned} log(s), {len(rows)} distinct issue pattern(s), "
          f"recent = touched < {args.days:g}d\n")
    print(f"{'COUNT':>6} {'RECENT':>6}  SEV   PATTERN")
    bad = False
    for key, (count, sev, recent_count, home) in rows[: args.top]:
        flag = "!" if (sev == "HIGH" and recent_count) else " "
        if sev == "HIGH" and recent_count:
            bad = True
        print(f"{count:>6} {recent_count:>6}  {sev:<4}{flag} {key[:110]}  [{home}]")
    if len(rows) > args.top:
        print(f"... {len(rows) - args.top} more (raise --top)")

    return 1 if (args.strict and bad) else 0


if __name__ == "__main__":
    sys.exit(main())
