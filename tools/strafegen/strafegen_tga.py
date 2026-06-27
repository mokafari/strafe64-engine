"""strafegen_tga — the shared 32-bit TGA writer.

Single home for the procedural-texture pixel writer used across strafegen
(strafegen_textures, strafegen_gfx). Was copy-pasted in two files; now one copy.
"""
import struct


def _tga32(w, h, px):
    """Uncompressed 32-bit TGA. px is a flat list of (r,g,b) rows top→bottom."""
    hdr = struct.pack("<BBBHHBHHHHBB",
                      0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)  # bottom-up, 8 alpha
    out = bytearray(hdr)
    for y in range(h - 1, -1, -1):
        row = px[y * w:(y + 1) * w]
        for r, g, b in row:
            out += bytes((b, g, r, 255))
    return bytes(out)


def _clamp8(v):
    return 0 if v < 0 else (255 if v > 255 else int(v))


