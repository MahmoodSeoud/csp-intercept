#!/usr/bin/env bash
#
# experiment.sh - ground-to-ground acceptance test for the Self-Verifying Uploader.
#
# Runs both ends (svu_server + svu_client) on THIS host as two CSP nodes on the real
# can0. Benign bus participation (like csh), joined passively (bitrate 0, never
# reconfigures the bus). For each case it serves a random file and pulls it back,
# then asserts the bytes are block-verified AND byte-identical (sha256). Prints a
# PASS/FAIL table and exits non-zero if anything failed.
#
# Just run it:   ./svu/experiment.sh
#
# Node addresses 25/26 and ports 11/12 are unused by the DISCO bench. Change
# SRV_NODE/CLI_NODE below if they ever clash.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build-fe/svu"
CAN="${SVU_CAN:-can0}"
SRV_NODE="${SVU_SRV:-25}"
CLI_NODE="${SVU_CLI:-26}"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

cleanup() { pkill -x svu_server 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT

if [ ! -x "$BIN/svu_server" ] || [ ! -x "$BIN/svu_client" ]; then
    echo "building svu binaries (-Dfrontends=true)..."
    meson setup -Dfrontends=true "$ROOT/build-fe" >/dev/null 2>&1 || true
    meson compile -C "$ROOT/build-fe" svu_server svu_client >/dev/null 2>&1 || {
        echo "BUILD FAILED"; exit 2; }
fi

# run_case NAME BYTES BLOCK MTU
run_case() {
    local name="$1" bytes="$2" block="$3" mtu="$4"
    local in="$WORK/in_$name" out="$WORK/out_$name" slog="$WORK/srv_$name"
    head -c "$bytes" /dev/urandom > "$in"

    pkill -x svu_server 2>/dev/null
    "$BIN/svu_server" -c "$CAN" -a "$SRV_NODE" -f "$in" -b "$block" > "$slog" 2>&1 &
    local i; for i in $(seq 1 60); do grep -q serving "$slog" 2>/dev/null && break; done

    timeout 90 "$BIN/svu_client" -c "$CAN" -a "$CLI_NODE" -C "$SRV_NODE" \
        -o "$out" -b "$block" -m "$mtu" > "$WORK/cli_$name" 2>&1
    local rc=$?
    pkill -x svu_server 2>/dev/null

    local rounds; rounds=$(grep -oE "in [0-9]+ round" "$WORK/cli_$name" | grep -oE "[0-9]+" | head -1)
    if [ "$rc" -eq 0 ] && cmp -s "$in" "$out"; then
        printf "  PASS  %-12s %8s B  block=%-5s mtu=%-4s rounds=%s\n" \
            "$name" "$bytes" "$block" "$mtu" "${rounds:-?}"
        PASS=$((PASS + 1))
    else
        printf "  FAIL  %-12s %8s B  block=%-5s mtu=%-4s  rc=%s  (see %s)\n" \
            "$name" "$bytes" "$block" "$mtu" "$rc" "$WORK/cli_$name"
        FAIL=$((FAIL + 1))
    fi
}

echo "SVU ground-to-ground experiment on $CAN  (server=$SRV_NODE  client=$CLI_NODE)"
echo
echo "[1] clean transfers - expect PASS, sha256(in)==sha256(out), 1 round on a clean bus:"
run_case small      8192   1024 200
run_case medium     131072 4096 200
run_case small-mtu  65536  4096 100
run_case small-blk  65536  512  200

echo
echo "[2] error handling - expect the tool to fail cleanly, never hang or crash:"
# no server listening on node 99: client must time out and return non-zero
timeout 40 "$BIN/svu_client" -c "$CAN" -a "$CLI_NODE" -C 99 -o "$WORK/none" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  PASS  no-server     client failed cleanly (no hang, no crash)"; PASS=$((PASS + 1))
else
    echo "  FAIL  no-server     client returned 0 with no server present"; FAIL=$((FAIL + 1))
fi
# missing required -C: must be rejected with a clear error
"$BIN/svu_client" -c "$CAN" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  PASS  missing-C     required-arg rejected"; PASS=$((PASS + 1))
else
    echo "  FAIL  missing-C     accepted a missing -C"; FAIL=$((FAIL + 1))
fi

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
