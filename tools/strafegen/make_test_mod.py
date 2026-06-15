#!/usr/bin/env python3
"""Generate a tiny but valid 4-channel ProTracker .mod for testing the
STRAFE 64 tracker-music codec. Not jungle — just a looping bass arp so
you can confirm libopenmpt decodes and the engine streams it. Drop real
jungle .it/.xm/.s3m tunes in baseoa/music/ and `music music/<file>` them.
"""
import struct, sys

# ProTracker period table (Amiga), octaves 1-3, notes C..B
PERIODS = {
    "C-1": 856, "D-1": 762, "E-1": 678, "F-1": 640, "G-1": 570, "A-1": 508, "B-1": 453,
    "C-2": 428, "D-2": 381, "E-2": 339, "F-2": 320, "G-2": 285, "A-2": 254, "B-2": 226,
    "C-3": 214, "D-3": 190, "E-3": 169, "F-3": 160, "G-3": 142, "A-3": 127, "B-3": 113,
}

def sample_data():
    # 64-byte signed-8-bit sawtooth, loops over its whole length
    return bytes(((i * 4 - 128) & 0xFF) for i in range(64))

def cell(sample, period, effect=0, param=0):
    b0 = (sample & 0xF0) | ((period >> 8) & 0x0F)
    b1 = period & 0xFF
    b2 = ((sample << 4) & 0xF0) | (effect & 0x0F)
    b3 = param & 0xFF
    return bytes((b0, b1, b2, b3))

EMPTY = cell(0, 0)

def build():
    out = bytearray()
    out += b"STRAFE64 TESTMOD\0\0\0\0"          # 20-byte title

    smp = sample_data()
    # sample 1: our saw; samples 2..31: empty
    name = b"saw".ljust(22, b"\0")
    length_words = len(smp) // 2
    out += name
    out += struct.pack(">H", length_words)       # length in words, BE
    out += bytes((0,))                            # finetune
    out += bytes((48,))                           # volume (0-64)
    out += struct.pack(">H", 0)                   # repeat start (words)
    out += struct.pack(">H", length_words)        # repeat length (words) -> loops
    for _ in range(30):
        out += b"\0" * 22
        out += struct.pack(">H", 0) + bytes((0, 0)) + struct.pack(">H", 0) + struct.pack(">H", 1)

    out += bytes((1,))     # song length = 1 pattern in order
    out += bytes((127,))   # historical restart byte
    order = bytearray(128)
    order[0] = 0
    out += bytes(order)
    out += b"M.K."         # 31-sample / 4-channel signature

    # one 64-row, 4-channel pattern: a bass arpeggio on channel 0
    riff = ["C-2", "E-2", "G-2", "C-3"]
    pattern = bytearray()
    for row in range(64):
        for ch in range(4):
            if ch == 0 and row % 4 == 0:
                note = riff[(row // 4) % len(riff)]
                pattern += cell(1, PERIODS[note])
            else:
                pattern += EMPTY
    assert len(pattern) == 64 * 4 * 4
    out += pattern
    out += smp             # sample data
    return bytes(out)

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "test_tracker.mod"
    with open(path, "wb") as f:
        f.write(build())
    print("wrote", path)
