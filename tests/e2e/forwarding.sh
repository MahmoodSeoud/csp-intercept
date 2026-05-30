#!/usr/bin/env bash
#
# XSUB/XPUB forwarding regression (eng review CRITICAL regression). Adding per-frame
# drop logic to zmq_proxy_lossy must NOT break the backend->frontend (items[1])
# subscription path -- if it does, no published frame ever reaches a subscriber and
# the whole virtual bus silently blackholes. Here: a subscriber on the XPUB backend,
# a publisher on the XSUB frontend, proxy at loss=0; assert the subscriber receives
# the frames. A broken items[1] -> subscription never propagates -> 0 received.
#
# Usage: forwarding.sh <proxy> <ci_gen> <ci_sub>
set -uo pipefail

PROXY="$1"; GEN="$2"; SUB="$3"
front="tcp://127.0.0.1:6020"; back="tcp://127.0.0.1:7020"
subout="$(mktemp)"
trap 'kill $(jobs -p) 2>/dev/null; rm -f "$subout"' EXIT

"$SUB" "$back" > "$subout" 2>/dev/null &
subpid=$!
sleep 0.3
# loss=0, no -M: pure pass-through, every frame must be forwarded.
"$PROXY" -s "$front" -p "$back" -L 0 >/dev/null 2>&1 &
proxypid=$!
sleep 0.3
"$GEN" 500 "$front" 13
sleep 0.5                                  # let the subscriber drain in-flight frames
kill -TERM "$subpid" 2>/dev/null; wait "$subpid" 2>/dev/null || true
kill -INT  "$proxypid" 2>/dev/null; wait "$proxypid" 2>/dev/null || true

count="$(tail -1 "$subout" 2>/dev/null)"
count="${count:-0}"
echo "subscriber received: ${count}/500"
if [ "${count}" -ge 400 ]; then
  echo "XSUB/XPUB forwarding OK"
else
  echo "FAIL: forwarding regression -- only ${count}/500 frames reached the subscriber"
  exit 1
fi
