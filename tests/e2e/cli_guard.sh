#!/usr/bin/env bash
#
# CLI boundary validation (rank 11): -M is a CSP dport (0-63; -1 = any). A value
# outside that range silently matches nothing, leaving the drop-log empty so a
# misconfigured run looks lossless -- the worst failure for a measurement instrument.
# The proxy must reject it fast with a clear error instead of running.
#
# Fails without the guard: an out-of-range -M sets match_dport and the proxy enters
# its infinite loop, so the command blocks and `timeout` kills it (exit 124) instead
# of exiting 1.
#
# Usage: cli_guard.sh <proxy>
set -uo pipefail

PROXY="$1"
s="tcp://127.0.0.1:6040"; p="tcp://127.0.0.1:7040"
rc=0

# Rejected: out-of-range dports must exit non-zero, fast, with "invalid".
for bad in 64 99 -2; do
  out="$(timeout 5 "$PROXY" -M "$bad" -s "$s" -p "$p" 2>&1)"
  ec=$?
  if [ "$ec" -ne 1 ]; then
    echo "FAIL: -M $bad exited $ec, expected 1 (124=timeout means it ran instead of rejecting)"
    rc=1
  elif ! echo "$out" | grep -qi "invalid"; then
    echo "FAIL: -M $bad exited 1 but printed no 'invalid' message: $out"
    rc=1
  else
    echo "ok: -M $bad rejected"
  fi
done

# Accepted: a boundary-valid dport must NOT be rejected (it proceeds to run, so a
# short timeout kills the running proxy -> exit 124, and never prints "invalid").
out="$(timeout 2 "$PROXY" -M 63 -s "$s" -p "$p" 2>&1)"
ec=$?
if echo "$out" | grep -qi "invalid"; then
  echo "FAIL: -M 63 is valid (dport 0-63) but was rejected: $out"
  rc=1
elif [ "$ec" -ne 124 ]; then
  echo "FAIL: -M 63 exited $ec, expected 124 (it should run until the timeout, not exit early)"
  rc=1
else
  echo "ok: -M 63 accepted"
fi

if [ "$rc" -eq 0 ]; then
  echo "CLI -M boundary validation OK"
fi
exit "$rc"
