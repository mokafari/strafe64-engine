# STRAFE 64 license keys

Ed25519-signed license keys for selling STRAFE 64. The **generator** here mints
keys; the **engine** verifies them offline with an embedded public key. No
server, no phone-home — a key is valid because its signature checks out against
the public key, and only the holder of the private key (you, on this machine)
can produce a signature that verifies.

## One-time setup

```sh
python3 -m pip install pynacl          # crypto dependency
python3 keygen.py init                 # creates keys/ (private + public)
python3 keygen.py export-pubkey-c      # writes engine/.../license_pubkey.h
```

Then rebuild the engine (`./scripts/build.sh`) so the new public key is baked in.

> **Back up `keys/strafe64_private.key` offline.** If you lose it you can't mint
> more valid keys; if it leaks, anyone can. It is git-ignored and `chmod 600`.
> Regenerating the keypair invalidates every key you've already sold.

## Selling keys

```sh
# mint 100 standard keys for a sale, write to a file
python3 keygen.py mint --tier standard -n 100 --out drop-2026-07.txt

# mint a single key tagged with the buyer (logged to ledger.csv)
python3 keygen.py mint --tier deluxe --buyer alice@example.com
```

Every minted key is appended to `ledger.csv` (serial, tier, issue date, buyer,
note, timestamp) so you have a record of everything you've sold. Hand the buyer
the `S64-…` string; they paste it into the game.

## Supporting buyers

```sh
python3 keygen.py verify S64-XXXXXX-XXXXXX-...
```

Prints `VALID` plus the tier/serial/issue date, or `INVALID` with the reason.
Use this to confirm a key from a support ticket is one you actually sold (and
cross-check the serial against `ledger.csv`).

## Tiers

`standard`, `deluxe`, `soundtrack`, `press`, `dev`. The tier travels inside the
key and is reported by the engine, so you can gate content by tier later. Add
tiers in `license_format.py` (keep existing byte values stable).

## How it fits together

```
keygen.py  --mint-->  S64-… key  --paste-->  engine verifies with
   ^                                          license_public_key[32]
   |                                                  ^
keys/strafe64_private.key  --export-pubkey-c-->  license_pubkey.h
```

`license_format.py` is the wire-format contract (payload layout, base32). The C
verifier in `engine/code/qcommon/license.c` must match it byte-for-byte — if you
change the format, bump `FORMAT_VERSION` and update both sides.
