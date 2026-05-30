#!/usr/bin/env bash
#
# Frame-size bound-guard regression (TODOS#4). The proxy copies a wire-controlled
# datalen into a single reused csp_packet_t; without an upper-bound check an oversized
# frame overflows the heap the moment any non-CSH publisher / replay / tc-netem feeds
# it a non-conforming frame. Here a publisher on the XSUB frontend sends conforming
# RDP frames interleaved with oversized (>2054-byte) frames through the deterministic
# path (-M 13); assert the proxy SURVIVES, counts the oversized frames as malformed,
# keeps forwarding every conforming frame, and never logs/drops a malformed one.
#
# Fails without the guard: the malformed counter stays 0 (assert fails) and/or the
# unchecked memcpy smashes the heap and the proxy never prints its summary.
#
# Usage: bound_guard.sh <proxy> <ci_oversize> <ci_sub>
set -uo pipefail

PROXY="$1"; GEN="$2"; SUB="$3"
front="tcp://127.0.0.1:6030"; back="tcp://127.0.0.1:7030"
CONFORMING=200
OVERSIZE=4
subout="$(mktemp)"; proxyout="$(mktemp)"; droplog="$(mktemp)"
trap 'kill $(jobs -p) 2>/dev/null; rm -f "$subout" "$proxyout" "$droplog"' EXIT

"$SUB" "$back" > "$subout" 2>/dev/null &
subpid=$!
sleep 0.3
# Deterministic path (-M 13) at loss=0: every conforming frame is kept+forwarded;
# the oversized frames must be rejected at the memcpy guard, not crash the proxy.
"$PROXY" -s "$front" -p "$back" -M 13 -S 12345 -L 0 -o "$droplog" > "$proxyout" 2>&1 &
proxypid=$!
sleep 0.3
"$GEN" "$CONFORMING" "$OVERSIZE" "$front" 13
sleep 0.5                                  # let the subscriber drain in-flight frames
kill -TERM "$subpid" 2>/dev/null; wait "$subpid" 2>/dev/null || true
kill -INT  "$proxypid" 2>/dev/null; wait "$proxypid" 2>/dev/null || true

# The summary line proves the proxy reached clean shutdown (did not crash on the
# oversized memcpy). Its absence is itself a failure.
summary="$(grep 'zmqproxy-lossy' "$proxyout" 2>/dev/null | tail -1)"
if [ -z "$summary" ]; then
  echo "FAIL: proxy printed no summary -- likely crashed on the oversized frame"
  echo "--- proxy output ---"; cat "$proxyout"
  exit 1
fi
echo "proxy summary: $summary"

field() { echo "$summary" | grep -oE "$1=[0-9]+" | head -1 | cut -d= -f2; }
kept="$(field kept)";       kept="${kept:-X}"
dropped="$(field dropped)"; dropped="${dropped:-X}"
malformed="$(field malformed)"; malformed="${malformed:-X}"
subcount="$(tail -1 "$subout" 2>/dev/null)"; subcount="${subcount:-0}"
case "$subcount" in *[!0-9]*) subcount=0 ;; esac

echo "kept=$kept dropped=$dropped malformed=$malformed | subscriber received=$subcount/$CONFORMING"

rc=0
if [ "$malformed" != "$OVERSIZE" ]; then
  echo "FAIL: malformed=$malformed, expected $OVERSIZE (the bound guard did not reject the oversized frames)"
  rc=1
fi
if [ "$kept" != "$CONFORMING" ]; then
  echo "FAIL: kept=$kept, expected $CONFORMING (the guard must not over-drop conforming frames)"
  rc=1
fi
if [ "$dropped" != "0" ]; then
  echo "FAIL: dropped=$dropped, expected 0 (loss=0; no conforming frame should be lost)"
  rc=1
fi
# A malformed frame must never be forwarded: the subscriber sees only conforming ones.
if [ "$subcount" -lt "$CONFORMING" ]; then
  echo "FAIL: subscriber got $subcount < $CONFORMING (forwarding regressed)"
  rc=1
fi
if [ "$subcount" -gt "$CONFORMING" ]; then
  echo "FAIL: subscriber got $subcount > $CONFORMING (a malformed frame was forwarded)"
  rc=1
fi
# The drop-log oracle must contain only conforming, non-dropped frames (no malformed).
logrows="$(grep -c -v '^#' "$droplog" 2>/dev/null || echo 0)"
if [ "$logrows" -ne "$CONFORMING" ]; then
  echo "FAIL: drop-log has $logrows data rows, expected $CONFORMING (malformed frames must not be logged)"
  rc=1
fi

if [ "$rc" -eq 0 ]; then
  echo "frame-size bound guard OK ($OVERSIZE oversized frames rejected, $CONFORMING forwarded, proxy survived)"
fi
exit "$rc"
