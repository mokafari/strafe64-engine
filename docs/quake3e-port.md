# Quake3e — feature wishlist & port-direction study

What we'd want from [ec-/Quake3e](https://github.com/ec-/Quake3e), and the decision
of **which way to port**: pull their changes into our tree (cherry-pick), or move our
code onto their engine.

Our base: **ioquake3 + rend2** (`renderergl2`), OpenArena-flavoured. Our value is
concentrated in (a) a large *gamecode* mod that rides the **standard Q3 VM ABI**, and
(b) a stack of *engine-side* mods — rend2 DoF, OpenAL time-pitch audio, slow-mo clock,
Ed25519 license gate, CMake build, and a working **WASM/browser** target.

---

## 1. Features we want from quake3e

Ranked by value **to STRAFE 64 specifically**, with a portability rating.

| # | Feature | Why we want it | Cherry-pickable into our tree? |
|---|---------|----------------|--------------------------------|
| 1 | **Raw mouse input** (auto, bypasses DirectInput) + high/stable FPS | Our thesis is "speed = accuracy"; lower input-to-photon latency is a real *feel* win for air-strafe/aim | **Yes** — bounded to input/SDL layer |
| 2 | **Server-side DoS protection** + reworked Zone allocator ("no OOM") + reduced memory | Needed before LATTICE / sword duels go online or we sell a paid build with servers | **Yes** — bounded to `sv_*` / `qcommon` zone |
| 3 | **`video-pipe` FFmpeg encoding** | We have a heavy MCP capture/cinematic/demo-render workflow; native FFmpeg = higher-quality clips straight from engine | **Yes** — bounded to `cl_avi.c` / console cmd |
| 4 | **Optimized renderer + VBO caching + merged lightmaps + reversed depth buffer** | Big FPS headroom for our DoF/bloom/baked stack; reversed-Z kills z-fighting on large strafegen maps | **No** — welded into `renderer/` + `renderervk/`; not separable from a renderer swap |
| 5 | **Vulkan renderer** | Modern backend, longevity | **No** — and we don't use it (our art = rend2) |
| 6 | Modern platform plumbing (SDL2 backend, Intel iGPU FBO fixes, per-window gamma) | Stop hand-carrying ioq3 platform patches | Partial — SDL2 backend is large; small fixes are pickable |
| 7 | Reworked QVM (faster VM) | — | N/A for us — **we ship native dylibs, not QVM** |

**Takeaway:** the features that are *cleanly cherry-pickable* (1, 2, 3) are also most of
the ones we actually care about. The features that are **not** separable (4, 5) are the
ones that would require adopting quake3e wholesale — and they sit in renderers we either
don't use (Vulkan) or that compete with our own rend2 work.

---

## 2. The two directions

### A. Port **us → them** (move our mods onto quake3e)

Adopt quake3e as the engine; re-apply our ~1,700 lines of engine-side patches on top.

**We gain:** every quake3e feature wholesale (incl. #4/#5 renderer perf).

**New costs discovered during this study:**
- **Audio is a rewrite, not a re-apply.** quake3e has **no `snd_openal.c`** — it uses the
  classic DMA mixer (`snd_dma.c`/`snd_mix.c`) + SDL audio. Our time-pitch varispeed is
  `AL_PITCH`-based and our music playlist hooks the OpenAL loop point. Both must be
  re-implemented against the DMA mixer (resample-rate pitch). Our notes already flag the
  DMA fallback as never built.
- **Different platform layout.** No `sys/` dir; platform code lives in `unix/`, `win32/`,
  `sdl/`. Our `sys_unix`/`sys_win32` patches don't map 1:1.
- **Lose the build + WASM.** quake3e uses Make/MSVC with vendored libs (`libsdl`,
  `libjpeg`, `libogg`, `libvorbis`, `libcurl`) and **no CMake, no emscripten target**.
  Our CMake tree, the browser demo path, and the MCP `engine_rebuild` hook all need
  re-targeting from scratch.
- **rend2 is quake3e's least-maintained subsystem.** Our DoF re-applies *into* `renderer2`,
  which they call unmaintained — so we'd live permanently in their worst-supported code,
  inheriting a merge cost on every upstream pull.

**What stays easy:** the gamecode (cgame/game/ui/botlib) — no custom syscalls, standard
ABI, runs on quake3e basically as-is. The Ed25519 license (`license.c`) is self-contained.
The slow-mo clock (`Com_ModifyMsec`) is ~20 lines. The MCP `com_pipefile` layer is stock
ioq3 and present in quake3e.

### B. Port **them → us** (cherry-pick their features into our tree)

Keep our engine, our build, our WASM, our rend2+DoF, our OpenAL audio. Import the
**bounded** features (#1, #2, #3) feature-by-feature.

**We gain:** the input-latency, netcode-hardening, and FFmpeg-capture wins — i.e. most of
what we actually want.

**We forgo:** the renderer perf gains (#4) and Vulkan (#5), because those are not
separable from quake3e's renderers. To get them we'd be importing `renderer/` +
`renderervk/` wholesale and rewiring — which *is* direction A by another name, and
abandons rend2 anyway.

**Costs:** each cherry-pick is a normal feature port (read their impl, adapt to our older
qcommon/client). GPL-on-GPL, so license-clean. No audio rewrite, no build loss.

---

## 3. Recommendation

**Default to direction B (them → us / cherry-pick).** Our value is locked into rend2,
OpenAL time-pitch, a CMake build and a browser target — exactly the four things direction
A forces us to rewrite or abandon. The features we most want (raw input, DoS hardening,
FFmpeg capture) are cleanly liftable into our tree without touching any of that.

**Choose direction A (us → them) only if** the goal becomes *adopting quake3e's
Vulkan/optimized renderer* — e.g. a hardware-broad commercial release where the renderer
perf and longevity outweigh redoing audio + build + WASM and re-homing our art on a
maintained Vulkan path. In that scenario we'd also re-evaluate whether rend2/DoF survives
or gets reimplemented on their renderer.

**Decision rule:** showcase / single-player / browser-demo trajectory → **B**. Online,
hardware-broad, renderer-modernization trajectory → reconsider **A**.

---

## 4. Cherry-pick plan (direction B)

| Feature | Source in quake3e | Lands in our tree | Est. |
|---------|-------------------|-------------------|------|
| Raw mouse input | `sdl/` input + `cl_input.c` | our SDL/input + `cl_input.c` | 1–2 d |
| Zone allocator rework | `qcommon/common.c` (Z_*) | `qcommon/common.c` | 1–2 d |
| Server DoS protection | `server/sv_*.c` | our `server/` | 1–2 d |
| `video-pipe` FFmpeg | `cl_avi.c` + console cmd | `client/cl_avi.c` | 1–2 d |
| (optional) reversed-Z / VBO | renderer-wide | **not advised** — renderer swap | — |

Each is independent; do them as separate, individually-verifiable patches.

## 5. Open questions
- Do we ever want their renderer? If yes, the calculus flips toward A and rend2's future
  is on the table.
- Is the paid build going **online**? That promotes the DoS-hardening cherry-pick (#2)
  from "nice" to "required" and is the strongest single reason to engage with quake3e at all.
- WASM: confirmed quake3e has no emscripten target — our browser demo is a reason to stay
  on our own engine regardless of direction.
