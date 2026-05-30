# Localhost two-oracle loop (MVP)

The instrument's core deliverable: a real DTP transfer through the lossy proxy, with
two independent oracles agreeing on exactly which fragments were lost.

- **Oracle A (drop-log):** the proxy deterministically drops a fraction of port-8
  fragments and logs each decision keyed by DTP fragment index.
- **Oracle B (APM CSV):** `csp_monitor` runs promiscuously in the csh that hosts the
  client and records every port-8 fragment that actually arrived.

Agreement = the dropped set and the observed set partition the sent fragments exactly:
every fragment is dropped XOR observed, none in both, none in neither.

**Verified 2026-05-30** (file.bin = 399280 B, mtu 200 -> 2038 fragments):
- p=0: client received all 399280 B, clean completion; APM observed 2038/2038, loss 0.
- p=0.1, seed 1: proxy dropped 225, kept 1813; APM observed exactly 1813; join verdict
  `PERFECT TWO-ORACLE AGREEMENT` (covered_exactly_once=2038, in_both=0, in_neither=0).

## Environment

Everything runs in ONE arm64 `ubuntu:24.04` container (no satellite, no board): the CSP
network is virtual over ZMQ. Build the image from `docker/Dockerfile.loop` (it adds the
deps csh/dipp-apm need beyond the front-ends image: meson>=1.6 via pip, protobuf-c,
brotli, yaml, libcurl).

Mount three source trees read-only and build into a writable copy (never clobber the
host): `/src/csp-intercept`, `/src/csh` (`~/projects/csh`), `/src/upload_gs-server` and
`/src/dipp-apm` (`~/DISCOSAT/...`).

## Build (in the container)

```sh
# 1. front-ends: zmqproxy-lossy + libcsh_csp_monitor.so  (proven path)
meson setup /build/ci /src/csp-intercept -Dfrontends=true && meson compile -C /build/ci

# 2. csh (the shell host). meson>=1.6 required (pip install). spacebridge needs CAN we
#    don't have -- build ONLY the csh target.
cp -r /src/csh /build/csh && cd /build/csh && rm -rf builddir \
  && meson setup . builddir -Dprefix=$HOME/.local -Dlibdir=lib/csh \
  && ninja -C builddir csh

# 3. dtp_client APM. dipp-apm's ippc APM needs JPEG-XL we don't have -- build ONLY
#    libcsh_dtp_client.so, then stage both APMs where csh discovers them.
cp -r /src/dipp-apm /build/dipp-apm && cd /build/dipp-apm && rm -rf builddir \
  && meson setup builddir --prefix=$HOME/.local --libdir=$HOME/.local/lib/csh \
  && ninja -C builddir libcsh_dtp_client.so
mkdir -p $HOME/.local/lib/csh
cp /build/dipp-apm/builddir/libcsh_dtp_client.so $HOME/.local/lib/csh/
cp /build/ci/apm/libcsh_csp_monitor.so          $HOME/.local/lib/csh/

# 4. upload_gs-server (real DTP server). Needs file.bin in CWD or it never sends.
cp -r /src/upload_gs-server /build/gs && cd /build/gs && rm -rf builddir \
  && meson setup builddir && ninja -C builddir   # file.bin ships in the source tree
```

## Run a loss run

NON-OBVIOUS, learned the hard way:
- **DTP is ports 7 (RDP meta) + 8 (bulk), NOT 13.** 13 is the separate DIPP RPC. Monitor
  and proxy-match on `8` for a DTP transfer (`-d 8` / `-M 8`).
- **mtu must match** across proxy (`-m`), monitor (`-m`), and client (`-m`) or fragment
  indices diverge between the oracles. Use 200 everywhere.
- **csh needs a pty** (`slash_create` does `tcgetattr`); drive it under `script -qec ... /dev/null`.
- Drive csh with `csh -i <init.csh>`; commands run line by line, async workers
  (`csp_monitor`, `dtp_client`) return immediately, so `sleep <ms>` between them.

```sh
# proxy: deterministic 10% loss on port-8 bulk + drop-log oracle
/build/ci/proxy/zmqproxy-lossy -s tcp://0.0.0.0:6000 -p tcp://0.0.0.0:7000 \
    -M 8 -m 200 -L 0.1 -S 1 -o /tmp/drop.csv &
# real DTP server as CSP node 5424 (run from the dir holding file.bin)
( cd /build/gs && ./builddir/upload_gs-server -z localhost -a 5424 ) &

# csh: node 19, load both APMs, monitor port 8, pull payload 0 from node 5424
cat > /build/xfer.csh <<'EOF'
csp init
csp add zmq -d 19 localhost
apm load
sleep 1500
csp_monitor start -d 8 -m 200 -o /tmp/apm.csv
dtp_client -n 5424 -i 0 -m 200 -T 5
sleep 10000
csp_monitor stop
EOF
script -qec '/build/csh/builddir/csh -i /build/xfer.csh' /dev/null </dev/null
```

## Join the oracles

`scripts/oracle_join.awk` partitions the sent fragments by dropped (drop-log col6 where
col8==1) vs observed (APM csv col11):

```sh
awk -F, '$8==1{print $6}' /tmp/drop.csv > /tmp/dropped_idx
grep -av '^#' /tmp/apm.csv | awk -F, '{print $11}' > /tmp/observed_idx
awk -v total=2038 -f scripts/oracle_join.awk /tmp/dropped_idx /tmp/observed_idx
```

## Known caveats (carry forward)

- At p>0 the client cannot cleanly complete (TODOS#5: it counts received packets, so any
  loss -> idle-timeout with a partial payload). Expected; the oracles still record the loss.
- The APM `#WINDOW inferred_loss` column is RDP-seq based and stays 0 for DTP (port 8); the
  DTP oracle is the observed-fragment count, not that column.
- `total` (sent fragment count) is currently passed in; derive it from the session
  `total bytes / (mtu-4)` when scripting this into a committed harness.
