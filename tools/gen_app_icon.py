#!/usr/bin/env python3
"""Generate the STRAFE 64 macOS app icon (engine/misc/macos/strafe64.icns).

NERV terminal look: near-black rounded plate, amber top rail, S64 wordmark in
the game's own Share Tech Mono (fonts/strafe64.ttf), faint grid. Reproducible:
    python3 tools/gen_app_icon.py
Requires PIL + macOS iconutil/sips.
"""
import os
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# assets/ is gitignored (absent in worktrees) — fall back to the canonical checkout
TTF_CANDIDATES = [
    os.path.join(ROOT, "assets/openarena/baseoa/fonts/strafe64.ttf"),
    os.path.expanduser("~/strafe64-engine/assets/openarena/baseoa/fonts/strafe64.ttf"),
]
TTF = next((t for t in TTF_CANDIDATES if os.path.isfile(t)), TTF_CANDIDATES[0])
OUT = os.path.join(ROOT, "engine/misc/macos/strafe64.icns")

S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# plate — macOS-style rounded square with a small margin
m = 64
r = 180
plate = (3, 6, 8, 255)
d.rounded_rectangle((m, m, S - m, S - m), radius=r, fill=plate)

# faint grid
grid = (18, 34, 30, 255)
for x in range(m, S - m, 64):
    d.line((x, m + 24, x, S - m - 24), fill=grid, width=2)
for y in range(m, S - m, 64):
    d.line((m + 24, y, S - m - 24, y), fill=grid, width=2)

# amber top rail (inset to respect the rounding)
amber = (255, 153, 15, 255)
d.rectangle((m + r // 2, m + 26, S - m - r // 2, m + 44), fill=amber)

# wordmark
font_big = ImageFont.truetype(TTF, 430)
font_small = ImageFont.truetype(TTF, 96)

def center_text(y, text, font, fill):
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=font)
    d.text(((S - (x1 - x0)) / 2 - x0, y - y0), text, font=font, fill=fill)

# chromatic split like the menu wordmark: cyan/red ghosts behind the amber
center_text(300 - 10, "S64", font_big, (50, 220, 255, 140))
center_text(300 + 10, "S64", font_big, (255, 40, 50, 140))
center_text(300, "S64", font_big, amber)
center_text(800, "STRAFE // NERV", font_small, (140, 107, 51, 255))

# emit iconset -> icns
with tempfile.TemporaryDirectory() as td:
    iconset = os.path.join(td, "strafe64.iconset")
    os.makedirs(iconset)
    base = os.path.join(td, "icon_1024.png")
    img.save(base)
    sizes = [16, 32, 64, 128, 256, 512]
    for sz in sizes:
        for scale in (1, 2):
            px = sz * scale
            name = f"icon_{sz}x{sz}" + ("@2x" if scale == 2 else "") + ".png"
            img.resize((px, px), Image.LANCZOS).save(os.path.join(iconset, name))
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", OUT], check=True)
print(f"wrote {OUT}")
