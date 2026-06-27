"""STRAFE 64 license-key wire format (v1).

This module is the single source of truth for how a license key is laid out
on the wire. The in-engine C verifier (engine/code/qcommon/license.c) MUST
agree with this byte-for-byte, so any change here is a breaking change that
requires bumping FORMAT_VERSION and updating both sides.

Key anatomy
-----------
    payload (12 bytes, big-endian fields)
        offset  size  field
        0       1     version   format version, == FORMAT_VERSION
        1       1     product   product id, == PRODUCT_ID
        2       1     tier      license tier (see TIERS)
        3       1     flags     reserved, currently 0
        4       2     issue     days since EPOCH (uint16, big-endian)
        6       6     serial    48-bit serial (big-endian)

    signed blob = ed25519_signature(64) || payload(12)   = 76 bytes
        This is the libsodium / TweetNaCl "combined" crypto_sign form
        (signature first, then message), which crypto_sign_open verifies
        and from which it recovers the 12-byte payload.

    key string = "S64-" + crockford_base32(signed blob), dash-grouped

The signature is computed over the *payload only*; the signed blob carries
both so the engine can recover and check the payload offline with nothing
but the embedded public key.
"""

from __future__ import annotations

import datetime as _dt
import struct
from dataclasses import dataclass

FORMAT_VERSION = 1
PRODUCT_ID = 0x64  # 'S64'
PAYLOAD_LEN = 12
SIG_LEN = 64
BLOB_LEN = SIG_LEN + PAYLOAD_LEN  # 76

# Day 0 of the issue-date field. Keep this fixed forever.
EPOCH = _dt.date(2020, 1, 1)

KEY_PREFIX = "S64"
GROUP_SIZE = 6  # characters between dashes in the rendered key

# Named license tiers. The byte value is what travels in the key.
TIERS = {
    "standard": 0,
    "deluxe": 1,
    "soundtrack": 2,
    "press": 200,
    "dev": 255,
}
TIERS_BY_VALUE = {v: k for k, v in TIERS.items()}

# Crockford base32: no I, L, O, U. Decode is case-insensitive and tolerant of
# the usual hand-transcription confusions.
_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
_DECODE = {c: i for i, c in enumerate(_ALPHABET)}
_DECODE.update({"O": 0, "I": 1, "L": 1})  # common look-alikes


@dataclass(frozen=True)
class Payload:
    tier: int
    serial: int
    issue: _dt.date
    version: int = FORMAT_VERSION
    product: int = PRODUCT_ID
    flags: int = 0

    @property
    def tier_name(self) -> str:
        return TIERS_BY_VALUE.get(self.tier, f"tier{self.tier}")

    def pack(self) -> bytes:
        issue_days = (self.issue - EPOCH).days
        if not (0 <= issue_days <= 0xFFFF):
            raise ValueError(f"issue date {self.issue} out of representable range")
        if not (0 <= self.serial <= 0xFFFFFFFFFFFF):
            raise ValueError("serial exceeds 48 bits")
        for name, val in (("version", self.version), ("product", self.product),
                          ("tier", self.tier), ("flags", self.flags)):
            if not (0 <= val <= 0xFF):
                raise ValueError(f"{name} must fit in one byte")
        return struct.pack(
            ">BBBBH",
            self.version, self.product, self.tier, self.flags, issue_days,
        ) + self.serial.to_bytes(6, "big")

    @classmethod
    def unpack(cls, data: bytes) -> "Payload":
        if len(data) != PAYLOAD_LEN:
            raise ValueError(f"payload must be {PAYLOAD_LEN} bytes, got {len(data)}")
        version, product, tier, flags, issue_days = struct.unpack(">BBBBH", data[:6])
        serial = int.from_bytes(data[6:], "big")
        return cls(
            tier=tier, serial=serial, issue=EPOCH + _dt.timedelta(days=issue_days),
            version=version, product=product, flags=flags,
        )


def b32encode(data: bytes) -> str:
    """Crockford base32, no padding, MSB-first bit packing."""
    bits = int.from_bytes(data, "big")
    nchars = (len(data) * 8 + 4) // 5
    out = []
    for i in range(nchars):
        shift = (nchars - 1 - i) * 5
        out.append(_ALPHABET[(bits >> shift) & 0x1F])
    return "".join(out)


def b32decode(text: str, nbytes: int) -> bytes:
    """Inverse of b32encode. `nbytes` is the expected decoded length."""
    bits = 0
    nchars = 0
    for ch in text:
        if ch not in _DECODE:
            raise ValueError(f"illegal base32 character {ch!r}")
        bits = (bits << 5) | _DECODE[ch]
        nchars += 1
    # The encoder packs MSB-first with the pad bits landing in the high bits
    # of the first char (it never left-shifts the value), so the reconstructed
    # integer already equals the original -- no shift on the way back.
    expected_chars = (nbytes * 8 + 4) // 5
    if nchars != expected_chars:
        raise ValueError(f"expected {expected_chars} base32 chars, got {nchars}")
    if bits >= (1 << (nbytes * 8)):
        raise ValueError("base32 payload has non-zero pad bits")
    return bits.to_bytes(nbytes, "big")


def normalize(key: str) -> str:
    """Strip formatting and the prefix, uppercase, ready for decoding."""
    cleaned = "".join(c for c in key.upper() if c.isalnum())
    if cleaned.startswith(KEY_PREFIX):
        cleaned = cleaned[len(KEY_PREFIX):]
    return cleaned


def format_key(blob: bytes) -> str:
    """Render a signed blob as the dash-grouped, prefixed key string."""
    body = b32encode(blob)
    groups = [body[i:i + GROUP_SIZE] for i in range(0, len(body), GROUP_SIZE)]
    return KEY_PREFIX + "-" + "-".join(groups)


def parse_key(key: str) -> bytes:
    """Recover the raw signed blob from a key string. Does NOT verify the sig."""
    return b32decode(normalize(key), BLOB_LEN)
