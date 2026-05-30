#!/usr/bin/env bash
#
# Determinism E2E: run the lossy proxy twice with the SAME seed over an identical
# CSP stream, and assert the drop-decision vector (index -> dropped) is identical
# for every RDP seq seen in both runs. Because the proxy keys on the RDP seq (not
# arrival order), this proves the reproducibility gate. Robust to ZMQ's slow-joiner
# dropping a few early frames: we compare only the intersection of seen indices.
#
# Usage: determinism.sh <proxy> <ci_gen> <ci_sub>
set -uo pipefail

PROXY="$1"; GEN="$2"; SUB="$3"
TMP="$(mktemp -d)"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$TMP"' EXIT

run_once() {  # $1=drop-log  $2=frontend-port  $3=backend-port
  local log="$1" fp="$2" bp="$3"
  local front="tcp://127.0.0.1:${fp}" back="tcp://127.0.0.1:${bp}"
  "$SUB" "$back" &
  local subpid=$!
  sleep 0.3
  "$PROXY" -s "$front" -p "$back" -M 13 -L 0.3 -S 1 -o "$log" >/dev/null 2>&1 &
  local proxypid=$!
  sleep 0.3
  "$GEN" 500 "$front" 13
  sleep 0.3
  kill -INT "$proxypid" 2>/dev/null; wait "$proxypid" 2>/dev/null || true
  kill "$subpid" 2>/dev/null; wait "$subpid" 2>/dev/null || true
}

# Different ports per run so a lingering bind never collides.
run_once "$TMP/log1.csv" 6000 7000
run_once "$TMP/log2.csv" 6010 7010

echo "log1 rows: $(grep -vc '^#' "$TMP/log1.csv" 2>/dev/null || echo 0)"
echo "log2 rows: $(grep -vc '^#' "$TMP/log2.csv" 2>/dev/null || echo 0)"

# Compare index(col6) -> dropped(col8) on the intersection.
awk -F, '
  /^#/ || /^[[:space:]]*$/ { next }
  FNR==NR { a[$6]=$8; next }
  { b[$6]=$8 }
  END {
    common=0; mism=0; drops=0;
    for (i in a) if (i in b) { common++; if (a[i] != b[i]) mism++; drops += a[i]; }
    if (common < 200) { printf "FAIL: only %d common indices (proxy/ZMQ flow problem?)\n", common; exit 1; }
    if (mism > 0)     { printf "FAIL: NON-DETERMINISTIC -- %d/%d indices differ\n", mism, common; exit 1; }
    printf "determinism OK: %d common indices, identical decisions, %d drops (~%.0f%%)\n", common, drops, 100.0*drops/common;
  }
' "$TMP/log1.csv" "$TMP/log2.csv"
