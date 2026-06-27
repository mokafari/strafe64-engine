#!/bin/sh
# Interop test: prove the in-engine C verifier (qcommon/license.c + TweetNaCl)
# accepts exactly the keys the Python minter produces, and rejects tampered /
# forged ones. Run after `keygen.py init` + `keygen.py export-pubkey-c`.
#
#   ./run.sh
#
# Compiles the REAL license.c (copied into build/) against shim headers, then
# cross-checks tiers against Python's own verifier.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
KEYGEN="$HERE/.."
Q="$KEYGEN/../../engine/code/qcommon"
B="$HERE/build"
mkdir -p "$B"

# pull the real engine sources so we test what actually ships
cp "$Q/license.c" "$Q/tweetnacl.c" "$Q/tweetnacl.h" "$Q/license_pubkey.h" "$B/"

cc -O2 -DLICENSE_SELFTEST -I"$HERE" -I"$B" \
   "$B/license.c" "$B/tweetnacl.c" "$HERE/test_main.c" -o "$B/verify"

# mint vectors with Python (valid per tier + tampered + garbage + forged)
python3 "$HERE/make_vectors.py" "$KEYGEN" > "$B/vectors.txt"

# compare expected tiers (col 1) against the C verifier's output
"$B/verify" < "$B/vectors.txt" | awk '{print $1}' > "$B/got.txt"
paste "$B/expected.txt" "$B/got.txt" | awk '
  { want=$1; got=$2; ok=(want==got);
    printf "%-9s expected=%-4s got=%-4s\n", ok?"OK":"MISMATCH", want, got;
    if (ok) pass++; else fail++ }
  END { printf "\n%d ok, %d mismatch\n", pass, fail; exit (fail>0) }'
