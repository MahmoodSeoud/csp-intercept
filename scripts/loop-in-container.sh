#!/usr/bin/env bash
#
# loop-in-container.sh - runs INSIDE the csp-loop container (see docker/Dockerfile.loop,
# launched by scripts/two-oracle-loop). Builds the four components from the read-only
# /src mounts, runs a real DTP transfer through the lossy proxy with the csp_monitor APM
# observing, and asserts the two oracles agree on exactly which fragments were lost.
#
# Sources (mounted ro): /src/csp-intercept, /src/csh, /src/upload_gs-server, /src/dipp-apm
# Tunables (env): LOSS SEED MTU PAYLOAD GS_ADDR CSH_ADDR TIMEOUT SKIP_BUILD
set -uo pipefail

LOSS="${LOSS:-0.1}"; SEED="${SEED:-1}"; MTU="${MTU:-200}"; PAYLOAD="${PAYLOAD:-0}"
GS_ADDR="${GS_ADDR:-5424}"; CSH_ADDR="${CSH_ADDR:-19}"; TIMEOUT="${TIMEOUT:-5}"
SKIP_BUILD="${SKIP_BUILD:-0}"

CI=/build/ci
PROXY=$CI/proxy/zmqproxy-lossy
CSH=/build/csh/builddir/csh
APMDIR=$HOME/.local/lib/csh
JOIN=/src/csp-intercept/scripts/oracle_join.awk
FAIL=0

build() {
  echo ">> [1/4] front-ends (zmqproxy-lossy + csp_monitor APM)"
  meson setup "$CI" /src/csp-intercept -Dfrontends=true >/tmp/b_ci.log 2>&1 \
    && meson compile -C "$CI" >>/tmp/b_ci.log 2>&1 || { echo "front-ends build FAILED"; tail -25 /tmp/b_ci.log; return 1; }

  echo ">> [2/4] csh (build only the csh target; spacebridge needs CAN we lack)"
  rm -rf /build/csh && cp -r /src/csh /build/csh && cd /build/csh && rm -rf builddir
  meson setup . builddir -Dprefix="$HOME/.local" -Dlibdir=lib/csh >/tmp/b_csh.log 2>&1 \
    && ninja -C builddir csh >>/tmp/b_csh.log 2>&1 || { echo "csh build FAILED"; tail -25 /tmp/b_csh.log; return 1; }

  echo ">> [3/4] dtp_client APM (build only that target; ippc needs JPEG-XL we lack)"
  rm -rf /build/dipp && cp -r /src/dipp-apm /build/dipp && cd /build/dipp && rm -rf builddir
  meson setup builddir --prefix="$HOME/.local" --libdir="$HOME/.local/lib/csh" >/tmp/b_dipp.log 2>&1 \
    && ninja -C builddir libcsh_dtp_client.so >>/tmp/b_dipp.log 2>&1 || { echo "dipp-apm build FAILED"; tail -25 /tmp/b_dipp.log; return 1; }

  echo ">> [4/4] upload_gs-server (real DTP server) + stage APMs"
  mkdir -p "$APMDIR"
  cp /build/dipp/builddir/libcsh_dtp_client.so "$APMDIR/"
  cp "$CI/apm/libcsh_csp_monitor.so"           "$APMDIR/"
  rm -rf /build/gs && cp -r /src/upload_gs-server /build/gs && cd /build/gs && rm -rf builddir
  meson setup builddir >/tmp/b_gs.log 2>&1 \
    && ninja -C builddir >>/tmp/b_gs.log 2>&1 || { echo "gs-server build FAILED"; tail -25 /tmp/b_gs.log; return 1; }
  [ -f /build/gs/file.bin ] || { echo "gs-server has no file.bin payload"; return 1; }
}

# run <loss> <tag>  -> writes /tmp/drop_<tag>.csv (if loss>0), /tmp/apm_<tag>.csv, /tmp/csh_<tag>.log
run() {
  local loss="$1" tag="$2"
  local drop="/tmp/drop_$tag.csv" apm="/tmp/apm_$tag.csv"
  rm -f "$drop" "$apm"
  local mflags=""
  [ "$loss" != "0" ] && mflags="-M 8 -m $MTU -L $loss -S $SEED -o $drop"
  cd /build/gs
  $PROXY -s tcp://0.0.0.0:6000 -p tcp://0.0.0.0:7000 $mflags >"/tmp/proxy_$tag.log" 2>&1 &
  local px=$!
  sleep 1
  ./builddir/upload_gs-server -z localhost -a "$GS_ADDR" >"/tmp/gs_$tag.log" 2>&1 &
  local gs=$!
  sleep 2
  cat > "/build/xfer_$tag.csh" <<EOF
csp init
csp add zmq -d $CSH_ADDR localhost
apm load
sleep 1500
csp_monitor start -d 8 -m $MTU -o $apm
dtp_client -n $GS_ADDR -i $PAYLOAD -m $MTU -T $TIMEOUT
sleep 10000
csp_monitor stop
EOF
  # csh needs a pty (slash_create -> tcgetattr); drive it under script(1).
  script -qec "$CSH -i /build/xfer_$tag.csh" /dev/null </dev/null >"/tmp/csh_$tag.log" 2>&1
  kill "$px" "$gs" 2>/dev/null
  pkill -9 -f zmqproxy-lossy 2>/dev/null; pkill -9 -f upload_gs-server 2>/dev/null; pkill -9 -x csh 2>/dev/null
  sleep 1
}

frag_total() {  # derive sent fragment count from the session byte count (independent of the oracles)
  local log="$1" bytes
  bytes=$(grep -a 'total bytes to' "$log" | grep -oE '[0-9]+' | tail -1)
  [ -n "$bytes" ] || { echo 0; return; }
  echo $(( (bytes + (MTU - 4) - 1) / (MTU - 4) ))
}

[ "$SKIP_BUILD" = "1" ] || { build || exit 1; }

echo
echo "=== p=0 sanity (clean transfer, every fragment must be observed) ==="
run 0 p0
TOTAL=$(frag_total /tmp/csh_p0.log)
OBS0=$(grep -av '^#' /tmp/apm_p0.csv 2>/dev/null | grep -c .)
echo "session -> $TOTAL fragments; APM observed $OBS0"
if [ "$TOTAL" -gt 0 ] && [ "$OBS0" = "$TOTAL" ]; then
  echo "p=0 OK"
else
  echo "p=0 FAIL: observed $OBS0 != $TOTAL"; FAIL=1
fi

echo
echo "=== p=$LOSS agreement (drop-log vs APM must partition the fragments) ==="
run "$LOSS" pL
TOTAL=$(frag_total /tmp/csh_pL.log)
awk -F, '$8==1{print $6}' /tmp/drop_pL.csv 2>/dev/null > /tmp/dropped_idx
grep -av '^#' /tmp/apm_pL.csv 2>/dev/null | awk -F, '{print $11}' > /tmp/observed_idx
awk -v total="$TOTAL" -f "$JOIN" /tmp/dropped_idx /tmp/observed_idx || FAIL=1

echo
if [ "$FAIL" = 0 ]; then
  echo "TWO-ORACLE LOOP: PASS"
else
  echo "TWO-ORACLE LOOP: FAIL"
fi
exit "$FAIL"
