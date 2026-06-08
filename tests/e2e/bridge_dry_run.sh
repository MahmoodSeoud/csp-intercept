#!/usr/bin/env bash
#
# bridge_dry_run.sh - no-root, no-CAN dry-run of the T4 loss injector (ci_inject_bridge).
#
# Proves the bridge+shim+oracle path end-to-end with the egress on ZMQ instead of can0, so
# it runs anywhere (the real run swaps only the egress to can:can0). Topology:
#
#   ci_gen --PUB--> [broker1] --> ci_inject_bridge --[T7 shim drop+log = ORACLE A]--> [broker2] --> csp_monitor (ORACLE B)
#   (the "uploader")  bus1            (bridges bus1->bus2)                                bus2        (ci_monitor_host)
#
# Two clean passthrough brokers (no -M => no loss) stand in for the two link segments; ALL
# the loss is the injector's CSP-aware shim, exactly as on the real bus. The two independent
# oracles must agree: no frame the injector logged as dropped may appear in the monitor's
# observed set, and the kept frames must be what the monitor saw.
#
# Mode: identity (BURST=0, default) is the reliable smoke test -- a deterministic-per-seq
# drop survives the harness's re-routing observer, so the partition holds. BURST>0
# (per-attempt GE) is NOT validated here: ci_monitor_host re-routes frames back onto the
# bus, looping a seq so it gets re-drawn -- a harness artifact the real can0 bus + passive
# csh monitor do not have. Per-attempt recovery is proven in drop_iface_host (T7); the
# combined path is validated on the flatsat.
#
# Usage: bridge_dry_run.sh <proxy> <ci_gen> <ci_monitor_host> <ci_inject_bridge>
set -uo pipefail

PROXY="$1"; GEN="$2"; MON="$3"; BRIDGE="$4"
N="${N:-500}"; LOSS="${LOSS:-0.3}"; SEED="${SEED:-1}"; DPORT=13; MTU="${MTU:-200}"; BURST="${BURST:-0}"
# bus1 = ingress (uploader side), bus2 = egress (satellite side). Disjoint from the other
# e2e tests' ports (6090/7090, 6100/7100, 6110/7110).
B1F=6101; B1B=7101      # broker1 frontend(XSUB)/backend(XPUB)
B2F=6102; B2B=7102      # broker2 frontend(XSUB)/backend(XPUB)

TMP="$(mktemp -d)"
drop="$TMP/drop.csv"; apm="$TMP/apm.csv"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$TMP"' EXIT

# 1) Two passthrough brokers (no -M => no injected loss; they are just the bus transport).
"$PROXY" -s "tcp://127.0.0.1:$B1F" -p "tcp://127.0.0.1:$B1B" >/dev/null 2>&1 &
"$PROXY" -s "tcp://127.0.0.1:$B2F" -p "tcp://127.0.0.1:$B2B" >/dev/null 2>&1 &
sleep 0.3

# 2) The injector: bridge bus1 -> [shim] -> bus2. Directional rx-filters kill the
#    broadcast self-echo: ingress receives only the uplink (ci_gen's dst=20), egress only
#    the downlink (dst=10, none here). ingress pub->broker1 XSUB ($B1F), sub->XPUB ($B1B).
"$BRIDGE" "zmq:tcp://127.0.0.1:$B1F,tcp://127.0.0.1:$B1B,20" \
          "zmq:tcp://127.0.0.1:$B2F,tcp://127.0.0.1:$B2B,10" \
          "$DPORT" "$MTU" 4 "$LOSS" "$BURST" "$SEED" "$drop" >"$TMP/bridge.log" 2>&1 &
sleep 0.4

# 3) Monitor (oracle B): join broker2 (egress bus), observe survivors via the real APM.
"$MON" 127.0.0.1 "$B2F" "$B2B" 19 "$DPORT" "$apm" 4000 >"$TMP/mon.log" 2>&1 &
monpid=$!
sleep 0.7

# 4) Uploader: deterministic RDP stream of N frames onto bus1 (broker1 frontend).
"$GEN" "$N" "tcp://127.0.0.1:$B1F" "$DPORT"
sleep 0.6

# 5) Tear down (monitor self-stops after run_ms; the injector flushes its drop-log on SIGTERM).
wait "$monpid" 2>/dev/null || true
kill $(jobs -p) 2>/dev/null || true
sleep 0.2

# --- Join (identical schema to two_oracle.sh) ---
awk -F, '$8==1{print $6}'            "$drop" 2>/dev/null | sort -n > "$TMP/dropped_idx"
awk -F, '$8==0{print $6}'            "$drop" 2>/dev/null | sort -n > "$TMP/kept_idx"
awk -F, '/^#/{next} $6==1{print $8}' "$apm"  2>/dev/null | sort -n > "$TMP/observed_idx"

echo "injector logged: $(grep -vc '^#' "$drop" 2>/dev/null || echo 0) decisions" \
     "(dropped $(wc -l < "$TMP/dropped_idx"), kept $(wc -l < "$TMP/kept_idx"))"
echo "monitor observed: $(wc -l < "$TMP/observed_idx") RDP frames on the egress bus"

awk '
  FNR==NR { dropped[$1]=1; nd++; next }
  { observed[$1]=1; no++ }
  END {
    if (nd == 0) { print "FAIL: injector dropped nothing -- check the bridge/shim"; exit 1 }
    if (no == 0) { print "FAIL: monitor observed nothing -- the bridge did not forward to the egress bus"; exit 1 }
    overlap = 0
    for (s in dropped) if (s in observed) overlap++
    printf "dropped-and-observed (must be 0): %d\n", overlap
    if (overlap > 0) { print "FAIL: a dropped frame reached the monitor -- oracles disagree"; exit 1 }
    print "VERDICT: TWO-ORACLE AGREEMENT through the bridge (no dropped frame observed)"
  }
' "$TMP/dropped_idx" "$TMP/observed_idx" || { echo "--- bridge.log ---"; cat "$TMP/bridge.log"; exit 1; }

comm_kept_obs=$(awk 'FNR==NR{a[$1]=1;next} ($1 in a){c++} END{print c+0}' "$TMP/kept_idx" "$TMP/observed_idx")
kept=$(wc -l < "$TMP/kept_idx" | tr -d ' ')
echo "kept frames forwarded through the bridge and observed: ${comm_kept_obs}/${kept}"
if [ "$kept" -lt 100 ]; then echo "FAIL: too few kept frames (${kept})"; cat "$TMP/bridge.log"; exit 1; fi
need=$(( kept * 90 / 100 ))
if [ "$comm_kept_obs" -lt "$need" ]; then
  echo "FAIL: bridge lost too many kept frames (${comm_kept_obs}/${kept} < 90%)"; cat "$TMP/bridge.log"; exit 1
fi

echo "BRIDGE DRY-RUN: PASS (ci_inject_bridge forwards + drops CSP-aware, two oracles agree)"
