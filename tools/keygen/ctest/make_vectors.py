"""Emit license-key test vectors for the C interop test (ctest/run.sh).

Prints one key per line to stdout and writes the matching expected tiers (one
per line, -1 == should be rejected) to build/expected.txt.
"""
import base64
import datetime as dt
import secrets
import sys
from pathlib import Path

keygen_dir = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(keygen_dir))
import license_format as fmt  # noqa: E402
from nacl.signing import SigningKey  # noqa: E402

sk = SigningKey(base64.b64decode((keygen_dir / "keys" / "strafe64_private.key").read_text().strip()))
issue = dt.date(2026, 6, 25)
keys, expected = [], []


def add(tier, key):
    keys.append(key)
    expected.append(tier)


# one valid key per tier
for name, val in fmt.TIERS.items():
    p = fmt.Payload(tier=val, serial=secrets.randbits(48), issue=issue)
    add(val, fmt.format_key(bytes(sk.sign(p.pack()))))

# tampered: flip the final char of a valid key
good = keys[0]
add(-1, good[:-1] + ("Z" if good[-1] != "Z" else "Y"))

# structurally garbage
add(-1, "S64-NOTAKEY")

# forged: signed by a different keypair
other = SigningKey.generate()
p = fmt.Payload(tier=0, serial=1, issue=issue)
add(-1, fmt.format_key(bytes(other.sign(p.pack()))))

build = Path(__file__).resolve().parent / "build"
build.mkdir(exist_ok=True)
(build / "expected.txt").write_text("\n".join(str(t) for t in expected) + "\n")
sys.stdout.write("\n".join(keys) + "\n")
