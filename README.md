# csp-intercept

A CSP-aware monitor and fault-injection instrument for the DISCO2 link.
Measures and deliberately degrades Cubesat Space Protocol (CSP) traffic so the
RDP-vs-DTP behaviour over a slow/lossy UHF link can be characterised reproducibly.

Design doc: `~/.gstack/projects/csp-intercept/mahmood-unknown-design-20260529-073029.md`
Test plan: `~/.gstack/projects/csp-intercept/mahmood-unknown-eng-review-test-plan-20260529-073029.md`

## Status

| Component | State |
|-----------|-------|
| `lib/` shared parse + rule + measurement | **done, 85 checks passing** |
| `apm/` promisc monitor (CSH APM) | **done** (headless start/stop E2E) |
| `proxy/` lossy zmqproxy (vendored + extended) | **done** (determinism + forwarding + bound-guard E2E) |
| two-oracle agreement (proxy drop-log vs live APM) | **done** (native E2E, RDP + DTP) |
| CI | **done, green** (ubuntu: `lib` + `frontends` jobs) |

The full E2E suite is **8/8 green**. The two-oracle loop -- the core claim that the
proxy's injected drops and the APM's observed loss agree exactly -- is proven on a
synthetic stream for both protocol paths: RDP (port 13) and the DTP bulk data path
(port 8). What's left is field work, not core logic: an on-target run against a real
uploader/DISCO2 traffic. See `TODOS.md`.

## What's here

`lib/` is the one source of truth (linked later by both the monitor APM and the
lossy proxy). Pure C, no libcsp dependency in the parse layer, so it is trivially
unit-testable. Every constant is pinned to verified libcsp/libdtp source (see the
header comments for file:line provenance).

- `ci_prng.h`  - splitmix64; deterministic per-index draws (reproducibility gate).
- `ci_rdp.{h,c}` - parse the 5-byte big-endian RDP trailer (`data[len-5]`),
  mask flags `& 0x0F` (high nibble is an anti-dedup counter).
- `ci_dtp.{h,c}` - parse the DTP data packet (uint32 LE byte-offset header on
  port 8, connectionless/unreliable; fragment = offset/(mtu-4)).
- `ci_rule.{h,c}` - the drop rule: match by port / RDP-SYN, decide drop/keep
  deterministically per packet index (or from a recorded replay vector); also the
  per-flow reproducibility key (RDP seq + wrap epoch / DTP fragment index).
- `ci_meas.{h,c}` - loss/dup/reorder sequence tracking, observed-at-tap RTT pairing,
  and the `MEASUREMENT_SUSPECT` flag (so instrument loss is not mistaken for link loss).

## What this studies

RDP and DTP take fundamentally different reliability approaches over CSP. The thesis
characterises that contrast over a slow, lossy UHF link: which transport degrades
gracefully, and how the SatDeploy uploader should be tuned as a result. This repo is
the instrument that produces those measurements. Protocol details and analysis live
in the (local) design doc, not here.

## Build & test

The pure `lib/` builds and tests natively (no dependencies):

```sh
meson setup build
meson test -C build
# or, dependency-free (85 checks):
cc -std=c11 -Wall -Wextra -Werror -I lib lib/ci_rdp.c lib/ci_dtp.c lib/ci_rule.c \
   lib/ci_meas.c tests/test_lib.c -o /tmp/ci_test && /tmp/ci_test
```

The front-ends (proxy + APM + their E2E tests) are **Linux-only** -- libcsp/CSH do
not build on a macOS host. On a Linux host they build **natively** (no Docker needed);
this is the fastest local loop and runs the full E2E suite:

```sh
git submodule update --init --recursive     # vendored CSH deps (csp/slash/param/apm_csh)
meson setup build -Dfrontends=true
meson compile -C build
meson test -C build --print-errorlogs       # 7/7: lib + proxy + APM + two-oracle
```

Native deps: `libzmq3-dev`, `libsocketcan-dev` (`libbsd-dev` on CI), `pkg-config`,
`python3`, meson, ninja, a C toolchain.

On a macOS host (or to mirror CI exactly) build in a container instead:

```sh
scripts/lbuild          # docker: compile + run the front-end test suite
scripts/lbuild clean    # drop the incremental build volume first
```

The two-oracle agreement test (`tests/e2e/two_oracle.sh` driving `ci_monitor_host`, a
real libcsp node that joins the lossy proxy and runs the actual `csp_monitor` APM) runs
as part of `meson test` -- it needs no csh and no external `upload_gs-server`. The full
DTP-on-port-8 loop with a real uploader (`scripts/two-oracle-loop`) does need Docker plus
the external CSH/uploader trees.

## Next

The instrument core is done and the two-oracle loop is proven natively on a synthetic
RDP stream. The remaining work is integration and the field result:

1. A DTP-on-port-8 two-oracle variant (the bulk path is parsed + unit-tested but not yet
   exercised end-to-end in the green suite).
2. An on-target run: a real `upload_gs-server` -> `zmqproxy-lossy` -> `dtp_client`
   transfer with the monitor APM draining, then join the proxy drop-vector against the
   APM CSV by flow identity (`scripts/two-oracle-loop`, needs Docker + the external trees).
3. A two-ended timestamped PCAP capture of a real DISCO2 pass.

See `TODOS.md`.
