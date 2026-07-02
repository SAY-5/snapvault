#!/usr/bin/env bash
# End-to-end snapvault demo on a synthesized mock dataset and simulated nodes.
#
#   1. synthesize a mock dataset (with a duplicated file to show dedup)
#   2. C++ engine: take a snapshot (prints dedup stats)
#   3. edit one file and take a second snapshot (prints incremental stats)
#   4. Go layer: distribute chunks across 5 nodes with replication factor 3
#   5. fail a node, then parallel-restore the snapshot with verification
#   6. compare the restored tree to the original byte-for-byte (sha compare)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SVCORE="$ROOT/core/build/svcore"
SNAPVAULT="$ROOT/bin/snapvault"
DEMO="$ROOT/demo"
DATA="$DEMO/dataset"
STORE="$DEMO/store"
OUT="$DEMO/restored"

rm -rf "$DEMO"
mkdir -p "$DATA/logs" "$DATA/config"

echo "==> 1. synthesizing mock dataset"
# A large-ish file and an identical copy (dedup), plus small config files.
seq 1 4000 | awk '{print "record " $1 " payload data line"}' > "$DATA/logs/app.log"
cp "$DATA/logs/app.log" "$DATA/logs/app.log.bak"   # identical -> deduped
printf 'name = snapvault\nnodes = 5\nreplicas = 3\n' > "$DATA/config/settings.ini"
head -c 6000 /dev/zero | tr '\0' 'Z' > "$DATA/config/blob.dat"
echo "    dataset:"
( cd "$DATA" && find . -type f | sort | sed 's/^/      /' )

echo
echo "==> 2. C++ engine: first snapshot (content-addressed, dedup)"
"$SVCORE" snapshot v1 "$DATA" --store "$STORE"

echo
echo "==> 3. C++ engine: edit one file, take an incremental snapshot"
printf 'name = snapvault\nnodes = 7\nreplicas = 3\nchanged = true\n' > "$DATA/config/settings.ini"
"$SVCORE" snapshot v2 "$DATA" --store "$STORE"

echo
echo "==> 4. Go layer: distribute v2 across simulated nodes with replication"
"$SNAPVAULT" put v2 --store "$STORE" --nodes 5 --replicas 3

echo
echo "==> 5. simulate a node failure, then parallel restore with verification"
"$SNAPVAULT" fail-node 2 --store "$STORE"
"$SNAPVAULT" status --store "$STORE"
echo "    restoring from surviving replicas..."
"$SNAPVAULT" restore v2 "$OUT" --store "$STORE" --parallel 4

echo
echo "==> 6. integrity check: restored tree vs original (byte-for-byte)"
gen_hashes() {
  ( cd "$1" && find . -type f | sort | while read -r f; do
      if command -v sha256sum >/dev/null 2>&1; then
        printf '%s  ' "$f"; sha256sum "$f" | awk '{print $1}'
      else
        printf '%s  ' "$f"; shasum -a 256 "$f" | awk '{print $1}'
      fi
    done )
}
ORIG_HASHES="$(gen_hashes "$DATA")"
REST_HASHES="$(gen_hashes "$OUT")"

if [[ "$ORIG_HASHES" == "$REST_HASHES" ]]; then
  echo "    NODE FAILURE SURVIVED and INTEGRITY VERIFIED: restored tree matches original byte-for-byte"
  echo
  echo "    per-file sha256 of restored tree:"
  echo "$REST_HASHES" | sed 's/^/      /'
else
  echo "    INTEGRITY MISMATCH between original and restored tree" >&2
  diff <(echo "$ORIG_HASHES") <(echo "$REST_HASHES") >&2 || true
  exit 1
fi
