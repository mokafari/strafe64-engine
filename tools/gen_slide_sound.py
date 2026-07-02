#!/usr/bin/env python3
"""Generate sound/player/slide.wav — a seamless crouch-slide scrape loop.

Pure stdlib (wave + math + random, deterministic seed). Design: brown-noise
bed (ground rumble) + band-ish hiss (surface friction) + 16 Hz amplitude
flutter (texture passing under you), 0.8 s, crossfaded seamless, peak ~0.35
so it sits under music/SFX. Regenerate:  python3 tools/gen_slide_sound.py
"""
import math
import os
import random
import struct
import wave

RATE = 22050
DUR = 0.8
N = int(RATE * DUR)
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tools", "strafegen", "assets", "slide.wav")

rng = random.Random(64)

# brown noise (integrated white, leaky) = low rumble
brown, acc = [], 0.0
for _ in range(N):
    acc = 0.985 * acc + rng.uniform(-1, 1) * 0.12
    brown.append(acc)

# hiss: white noise through a crude one-pole band emphasis (~1-3 kHz)
hiss, lp1, lp2 = [], 0.0, 0.0
for _ in range(N):
    w = rng.uniform(-1, 1)
    lp1 += 0.55 * (w - lp1)        # low-pass ~3 kHz
    lp2 += 0.12 * (lp1 - lp2)      # low-pass ~700 Hz
    hiss.append(lp1 - lp2)         # band-pass-ish

samples = []
for i in range(N):
    t = i / RATE
    flutter = 0.8 + 0.2 * math.sin(2 * math.pi * 16.0 * t)   # surface texture
    s = (0.5 * brown[i] + 1.1 * hiss[i]) * flutter
    samples.append(s)

# crossfade the last 120 ms into the first 120 ms for a seamless loop
fade = int(0.12 * RATE)
for i in range(fade):
    a = i / fade
    samples[i] = samples[i] * a + samples[N - fade + i] * (1 - a)
samples = samples[: N - fade]

peak = max(abs(s) for s in samples)
gain = 0.35 / peak

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with wave.open(OUT, "wb") as f:
    f.setnchannels(1)
    f.setsampwidth(2)
    f.setframerate(RATE)
    f.writeframes(b"".join(
        struct.pack("<h", int(s * gain * 32767)) for s in samples))
print(f"wrote {OUT} ({len(samples)/RATE:.2f}s seamless loop)")
