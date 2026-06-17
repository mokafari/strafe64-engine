# STRAFE 64 — Shader Library

A growing gallery of visually striking shaders found in the wild (Shadertoy,
GitHub, forums) **re-created with idTech3 shader-script techniques** and bent to
STRAFE 64's visual direction: synthwave / neon / void / dissolving-world /
audio-reactive.

idTech3 can't run arbitrary GLSL per surface, so we port the **look**, not the
code — bake what the fragment shader computes per-pixel into a procedural texture,
then animate it with shader stages (`tcMod scroll/turb/rotate/stretch`,
`deformVertexes`, `rgbGen/alphaGen wave`, and the audio genfuncs
`bass`/`mid`/`high`/`level`). Where it fits, at least one stage rides the music.

## Build & view

```sh
cd tools/strafegen/shaderlib
python3 gallery.py            # generates textures + a loose shader + a gallery map, deploys to baseoa
```

Then in-engine (a sealed dark neon-grid room with one glowing panel per shader):

```
/map shaderlib_gallery
```

Tip: play a bass-heavy track (`/music music/liquid_calibre_even-if.mp3`) — the
audio-reactive panels pump with the kick. The generated assets live under the
git-ignored `assets/openarena/baseoa/` (run `gallery.py` to regenerate); only the
generator, this doc, and the preview thumbnails are versioned.

---

## Shaders

### 01 · Neon Plasma Flow — `shaderlib/plasma`

![Neon Plasma Flow](previews/01_plasma.jpg)

Flowing synthwave plasma: deep-indigo void shot through with magenta/pink/cyan
neon veins that churn and flare on the kick drum.

- **Ports:** Shadertoy — [*Neon Plasma Storm* (4scGDH)](https://www.shadertoy.com/view/4scGDH) and the classic [*Plasma Waves* (ltXczj)](https://www.shadertoy.com/view/ltXczj) family.
- **Technique:** a seamless, tileable **sum-of-directional-sines** plasma baked
  through a mostly-dark synthwave palette (so the neon veins read against the
  void), then **three additive stages** scroll + `tcMod turb` it at different
  scales for the churn. The third stage's brightness is `rgbGen wave bass`, so it
  pumps with the music; the gl2 bloom makes the bright veins glow.

<!-- next entries appended here by the shader loop -->
