# STRAFE 64 — landing page

A single-page marketing site for STRAFE 64, styled after the game's own art
bible: *"a PlayStation-era racing sim, rendered on a dying MAGI defense
terminal."* VOID-black background, MAGI amber HUD chrome, the chromatic-split
`STRAFE 64` wordmark, AC-data blue spec panels, and the section-color "color =
verb" vocabulary — all pulled straight from [`docs/ART_DIRECTION.md`](../docs/ART_DIRECTION.md).

## Run it

It's a static site — any web server works:

```sh
cd web
python3 -m http.server 8099
# open http://localhost:8099
```

(There's also a `.claude/launch.json` config named `strafe64-web`.)

## Structure

- `index.html` — all markup, one page, scroll-driven.
- `style.css` — the full MAGI-terminal theme + responsive layout.
- `main.js` — starfield canvas, scroll reveals, VOID-collapse gauge, nav dots,
  count-up readouts, wordmark glitch, autoplay-on-view video.
- `assets/` — real in-engine captures (dusk world, assassin duel, lattice,
  katana, blood) plus a live bullet-time clip.

## Sections (top → bottom)

Hero → Doctrine (3 pillars) → Pilot Kit (9 moves) → Time-Bind → **Signal**
(audio-visual + soundtrack player) → The Katana → The Vectorgun → Modes →
The World (color legend) → Gallery → Spec → Acquire → Footer.

The combat focus is deliberately **just the two weapons** — katana (melee /
time-bind) and vectorgun (ranged, void-blue, deflectable) — matching the
Arena's sword|vectorgun loadout. No multi-gun arsenal.

## Wiring it up

- **Store / demo links** — edit the `LINKS` object at the top of the behaviour
  block in [`main.js`](main.js): `buy` (license checkout) and `demo` (free
  download). It auto-applies to every `.tier__cta` and `.js-demo` button.
  Currently placeholders (`gumroad` / `itch.io`).
- **Trailer** — the hero "WATCH TRAILER" button opens a modal playing
  `assets/bullettime-assassins.mp4`. Swap that file for a real trailer.
- **Soundtrack (mokafari)** — the Signal section has a working player with a live
  WebAudio visualizer. Drop tracks into [`assets/music/`](assets/music/) and flip
  them on in the `TRACKS` array in `main.js` (see
  [`assets/music/README.md`](assets/music/README.md)). Until then they show a
  `SOON` badge and the visualizer runs a synthetic idle spectrum.

## Updating the imagery

All screenshots come from the live engine via the `strafe64-engine` MCP
(`engine_frame` / `engine_cinematic`). Drop new captures into `assets/` and
update the `src`/`background-image` references.
