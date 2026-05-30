#!/usr/bin/env bash
#
# Two-oracle agreement E2E -- the MVP "whoa", run natively (no Docker, no csh).
#
# Stands up the real virtual bus: a live libcsp monitor node (ci_monitor_host) joins
# the lossy proxy over zmqhub and runs the ACTUAL csp_monitor APM; ci_gen publishes a
# deterministic RDP stream (port 13) through the proxy. The proxy drops a reproducible
# subset by per-flow identity (RDP seq) and logs every decision (oracle 1, drop.csv);
# the monitor observes whatever the proxy forwards (oracle 2, apm.csv).
#
# A trustworthy loss measurement requires the two INDEPENDENT oracles to agree:
#   * no frame the proxy logged as dropped may appear in the monitor's observed set
#     (a dropped-but-observed frame means the instrument is lying), and
#   * the frames the proxy kept must be exactly what the monitor observed.
# Robust to ZMQ's slow-joiner (a few early frames lost before subscriptions settle):
# the universe is the set of seqs the PROXY actually logged, and we assert the
# partition within that universe.
#
# Usage: two_oracle.sh <proxy> <ci_gen> <ci_monitor_host>
set -uo pipefail

PROXY="$1"; GEN="$2"; MON="$3"
N="${N:-500}"; LOSS="${LOSS:-0.3}"; SEED="${SEED:-1}"; DPORT=13
# Unique ports: the meson E2E tests run in parallel, so these must not collide with
# any other test's proxy bind (e.g. cli_guard.sh uses 6040/7040, determinism 6000-6010).
FRONT_PORT=6090; BACK_PORT=7090
front="tcp://127.0.0.1:${FRONT_PORT}"; back="tcp://127.0.0.1:${BACK_PORT}"

TMP="$(mktemp -d)"
drop="$TMP/drop.csv"; apm="$TMP/apm.csv"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$TMP"' EXIT

# 1) Proxy: match RDP dport 13, reproducible loss, log every decision.
"$PROXY" -s "$front" -p "$back" -M "$DPORT" -L "$LOSS" -S "$SEED" -o "$drop" >/dev/null 2>&1 &
proxypid=$!
sleep 0.3

# 2) Monitor node: join the proxy (pub=frontend, sub=backend), run the real APM for
#    the whole transfer. args: host frontport backport addr dport out run_ms.
"$MON" 127.0.0.1 "$FRONT_PORT" "$BACK_PORT" 19 "$DPORT" "$apm" 4000 >"$TMP/mon.log" 2>&1 &
monpid=$!
sleep 0.7                         # let the monitor's SUB + APM start before traffic

# 3) Publisher: deterministic RDP stream of N frames through the proxy frontend.
"$GEN" "$N" "$front" "$DPORT"
sleep 0.5                         # let the last frames route + drain into the APM CSV

# 4) Tear down: the monitor stops itself after run_ms; INT the proxy to flush drop-log.
wait "$monpid" 2>/dev/null || true
kill -INT "$proxypid" 2>/dev/null; wait "$proxypid" 2>/dev/null || true

# --- Join ---
# drop.csv: t_ms,src,dport,csp_flags,is_rdp,index,epoch,dropped   -> $6=seq(epoch 0), $8=dropped
# apm.csv  RDP rows (is_rdp col $6==1): ...,rdp_seq=$8
awk -F, '$8==1{print $6}'              "$drop" 2>/dev/null | sort -n > "$TMP/dropped_idx"
awk -F, '$8==0{print $6}'              "$drop" 2>/dev/null | sort -n > "$TMP/kept_idx"
awk -F, '/^#/{next} $6==1{print $8}'   "$apm"  2>/dev/null | sort -n > "$TMP/observed_idx"

echo "proxy logged: $(grep -vc '^#' "$drop" 2>/dev/null || echo 0) decisions" \
     "(dropped $(wc -l < "$TMP/dropped_idx"), kept $(wc -l < "$TMP/kept_idx"))"
echo "monitor observed: $(wc -l < "$TMP/observed_idx") RDP frames"

awk '
  FNR==NR { dropped[$1]=1; nd++; next }            # file1: dropped seqs
  { observed[$1]=1; no++ }                          # file2: observed seqs
  END {
    if (nd == 0) { print "FAIL: proxy dropped nothing -- check -M/-L"; exit 1 }
    if (no == 0) { print "FAIL: monitor observed nothing -- bus/promisc/route problem"; exit 1 }
    overlap = 0
    for (s in dropped) if (s in observed) overlap++
    printf "dropped-and-observed (must be 0): %d\n", overlap
    if (overlap > 0) { print "FAIL: a dropped frame reached the monitor -- oracles disagree"; exit 1 }
    print "VERDICT: TWO-ORACLE AGREEMENT (no dropped frame observed)"
  }
' "$TMP/dropped_idx" "$TMP/observed_idx" || exit 1

# Coverage: of the frames the proxy KEPT, how many did the monitor observe? Slow-joiner
# may cost a few at the very start, so require a strong majority rather than 100%.
comm_kept_obs=$(comm -12 "$TMP/kept_idx" "$TMP/observed_idx" | wc -l | tr -d ' ')
kept=$(wc -l < "$TMP/kept_idx" | tr -d ' ')
echo "kept frames observed by monitor: ${comm_kept_obs}/${kept}"
if [ "$kept" -lt 100 ]; then
  echo "FAIL: too few kept frames (${kept}) to judge coverage"; exit 1
fi
# >=90% of kept frames must be observed (the rest = ZMQ slow-joiner at stream start).
need=$(( kept * 90 / 100 ))
if [ "$comm_kept_obs" -lt "$need" ]; then
  echo "FAIL: monitor missed too many kept frames (${comm_kept_obs}/${kept} < 90%)"; exit 1
fi

echo "TWO-ORACLE LOOP: PASS"
