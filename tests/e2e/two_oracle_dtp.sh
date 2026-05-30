#!/usr/bin/env bash
#
# Two-oracle agreement E2E for the DTP bulk path (port 8) -- the DTP counterpart of
# two_oracle.sh, run natively (no Docker, no csh).
#
# Same virtual bus, but the traffic is a DTP data stream instead of RDP: ci_gen_dtp
# publishes port-8 frames whose CSP payload begins with a little-endian uint32 byte
# offset (the libdtp data-plane header). The proxy matches dport 8, drops a
# reproducible subset keyed by the per-flow fragment index (offset/(mtu-4)), and logs
# every decision (oracle 1, drop.csv). The monitor node runs the real csp_monitor APM
# in DTP mode (-d 8) and records the fragment index of every frame it observes
# (oracle 2, apm.csv).
#
# A trustworthy DTP-loss measurement requires the two independent oracles to agree:
# no fragment the proxy logged as dropped may appear in the monitor's observed set.
# Robust to ZMQ's slow-joiner (a few early frames lost before subscriptions settle):
# the universe is the set of fragments the PROXY actually logged.
#
# Usage: two_oracle_dtp.sh <proxy> <ci_gen_dtp> <ci_monitor_host>
set -uo pipefail

PROXY="$1"; GEN="$2"; MON="$3"
N="${N:-500}"; LOSS="${LOSS:-0.3}"; SEED="${SEED:-1}"; DPORT=8; MTU="${MTU:-200}"
# Unique ports: meson runs E2E tests in parallel, so these must not collide with any
# other test's proxy bind (two_oracle.sh 6090/7090, cli_guard 6040/7040, determinism
# 6000-6010, forwarding 6020).
FRONT_PORT=6100; BACK_PORT=7100
front="tcp://127.0.0.1:${FRONT_PORT}"; back="tcp://127.0.0.1:${BACK_PORT}"

TMP="$(mktemp -d)"
drop="$TMP/drop.csv"; apm="$TMP/apm.csv"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$TMP"' EXIT

# 1) Proxy: match DTP dport 8, MTU for the fragment-index key, reproducible loss, log.
"$PROXY" -s "$front" -p "$back" -M "$DPORT" -m "$MTU" -L "$LOSS" -S "$SEED" -o "$drop" >/dev/null 2>&1 &
proxypid=$!
sleep 0.3

# 2) Monitor node: join the proxy (pub=frontend, sub=backend), run the real APM on
#    dport 8 for the whole transfer. args: host frontport backport addr dport out run_ms.
#    The APM's DTP fragment index uses its default MTU (200), matching -m above.
"$MON" 127.0.0.1 "$FRONT_PORT" "$BACK_PORT" 19 "$DPORT" "$apm" 4000 >"$TMP/mon.log" 2>&1 &
monpid=$!
sleep 0.7                         # let the monitor's SUB + APM start before traffic

# 3) Publisher: deterministic DTP stream of N fragments through the proxy frontend.
"$GEN" "$N" "$front" "$MTU"
sleep 0.5                         # let the last frames route + drain into the APM CSV

# 4) Tear down: the monitor stops itself after run_ms; INT the proxy to flush drop-log.
wait "$monpid" 2>/dev/null || true
kill -INT "$proxypid" 2>/dev/null; wait "$proxypid" 2>/dev/null || true

# --- Join ---
# drop.csv: t_ms,src,dport,csp_flags,is_rdp,index,epoch,dropped  -> $6=frag, $8=dropped
# apm.csv DTP rows (is_rdp col $6==0): ...,dtp_offset=$10,dtp_frag=$11
awk -F, '$8==1{print $6}'              "$drop" 2>/dev/null | sort -n > "$TMP/dropped_idx"
awk -F, '$8==0{print $6}'              "$drop" 2>/dev/null | sort -n > "$TMP/kept_idx"
awk -F, '/^#/{next} $6==0{print $11}'  "$apm"  2>/dev/null | sort -n > "$TMP/observed_idx"

echo "proxy logged: $(grep -vc '^#' "$drop" 2>/dev/null || echo 0) decisions" \
     "(dropped $(wc -l < "$TMP/dropped_idx"), kept $(wc -l < "$TMP/kept_idx"))"
echo "monitor observed: $(wc -l < "$TMP/observed_idx") DTP fragments"

awk '
  FNR==NR { dropped[$1]=1; nd++; next }            # file1: dropped frags
  { observed[$1]=1; no++ }                          # file2: observed frags
  END {
    if (nd == 0) { print "FAIL: proxy dropped nothing -- check -M/-L"; exit 1 }
    if (no == 0) { print "FAIL: monitor observed nothing -- bus/promisc/route problem"; exit 1 }
    overlap = 0
    for (s in dropped) if (s in observed) overlap++
    printf "dropped-and-observed (must be 0): %d\n", overlap
    if (overlap > 0) { print "FAIL: a dropped fragment reached the monitor -- oracles disagree"; exit 1 }
    print "VERDICT: TWO-ORACLE AGREEMENT (no dropped fragment observed)"
  }
' "$TMP/dropped_idx" "$TMP/observed_idx" || exit 1

# Coverage: of the fragments the proxy KEPT, how many did the monitor observe? Slow-joiner
# may cost a few at the very start, so require a strong majority rather than 100%.
comm_kept_obs=$(comm -12 "$TMP/kept_idx" "$TMP/observed_idx" | wc -l | tr -d ' ')
kept=$(wc -l < "$TMP/kept_idx" | tr -d ' ')
echo "kept fragments observed by monitor: ${comm_kept_obs}/${kept}"
if [ "$kept" -lt 100 ]; then
  echo "FAIL: too few kept fragments (${kept}) to judge coverage"; exit 1
fi
need=$(( kept * 90 / 100 ))
if [ "$comm_kept_obs" -lt "$need" ]; then
  echo "FAIL: monitor missed too many kept fragments (${comm_kept_obs}/${kept} < 90%)"; exit 1
fi

echo "TWO-ORACLE DTP LOOP: PASS"
