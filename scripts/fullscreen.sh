#!/bin/sh
# STRAFE 64 — FULLSCREEN launcher.
#
# The SHOWCASE beauty preset (GL2 HDR + filmic tonemap + bloom + eye
# adaptation + MSAA/aniso, every gameplay effect cranked, sword
# bullet-time, cyber-Angelyss, bots) — hardwired to FULLSCREEN.
#
# This is just showcase.sh with FULLSCREEN forced on, so every mode,
# env var and beauty cvar lives in ONE place (scripts/showcase.sh) and
# can't drift out of sync.
#
#   ./scripts/fullscreen.sh                 arena, Assassins, fullscreen
#   ./scripts/fullscreen.sh course [seed]   a flowing surf/strafe course
#   ./scripts/fullscreen.sh kb [seed]       killbox — vertical melee pit
#   ./scripts/fullscreen.sh <mapname>       a literal, already-deployed map
#
#   WEAPON=vectorgun ./scripts/fullscreen.sh   arena with the speed-rail
#   BOTS=6 SKILL=5 ./scripts/fullscreen.sh     fill + skill overrides
#   MAXQ=1 ./scripts/fullscreen.sh             screenshot-grade renderer
#   FORCEMODEL=1 ./scripts/fullscreen.sh       everyone = cyber Angelyss
#
# All other flags/env behave exactly as in showcase.sh.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Force fullscreen, then hand off everything else to the showcase launcher.
exec env FULLSCREEN=1 "$ROOT/scripts/showcase.sh" "$@"
