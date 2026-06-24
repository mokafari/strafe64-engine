#!/bin/sh
# STRAFE 64 — VECTORGUN ARENA, full graphics.
#
# One-command entry for the speed-scaled rail arena with every graphics
# enhancement on: the GFX-dressed arena map (machined-hull centre tower +
# pillars, chrome accents, q3gl2 dusk sun + cascaded shadows), GL2 HDR/bloom/
# tonemap, normal+specular+parallax materials, SSAO, and the audio-reactive
# bullet-time effect stack vs a squad of Assassins.
#
# This is just a thin wrapper over showcase.sh that forces the vectorgun
# ruleset — showcase.sh does the real work (generate+deploy a graphics-current
# map, latch g_vectorgun, layer arena.cfg, apply the beauty cvars).
#
#   ./scripts/vectorgun_arena.sh              windowed, seed 1337
#   ./scripts/vectorgun_arena.sh -f           fullscreen
#   ./scripts/vectorgun_arena.sh 4242         a different arena seed
#   ./scripts/vectorgun_arena.sh new          a fresh random arena
#   REGEN=1 ./scripts/vectorgun_arena.sh      force-regenerate the map pk3
#   BOTS=6 SKILL=5 ./scripts/vectorgun_arena.sh   bigger / harder squad
#
# The melee twin is the same launcher with WEAPON=sword (the showcase default).

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# pass through -f (fullscreen) before the mode arg; default seed handled by showcase
FS=""
if [ "$1" = "-f" ]; then FS="-f"; shift; fi

exec env WEAPON=vectorgun "$ROOT/scripts/showcase.sh" $FS arena "$@"
