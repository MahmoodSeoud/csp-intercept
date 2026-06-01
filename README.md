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
| `inject/` in-path drop shim (CAN/KISS injection) | **done** (deterministic drop + leak-free E2E) |
| two-oracle agreement (proxy drop-log vs live APM) | **done** (native E2E, RDP + DTP) |
| CI | **done, green** (ubuntu: `lib` + `frontends` jobs) |

The full E2E suite is **9/9 green**. The two-oracle loop -- the core claim that the
injected drops and the APM's observed loss agree exactly -- is proven on a synthetic
stream for both protocol paths: RDP (port 13) and the DTP bulk data path (port 8). What's
left is field work, not core logic: an on-target run against a real uploader/DISCO2
traffic. See `TODOS.md`.

### Transport coverage

The monitor APM is transport-agnostic: `csp_route_work` clones every routed packet into
the promiscuous queue regardless of which interface it arrived on, and `lib/` parses the
CSP packet after the interface reassembles it. So **monitoring works over ZMQ, CAN, and
KISS** -- you only enable promiscuous mode on filtered transports.

| Transport | Monitor sees all traffic | Fault injection |
|-----------|--------------------------|-----------------|
| ZMQ (zmqhub) | with `csp add zmq -p` (else address-filtered) | `proxy/zmqproxy-lossy` (XSUB/XPUB broker) |
| CAN (socketcan) | in promiscuous mode (`can_mask=0x0000`) | `inject/` in-path drop shim |
| KISS (serial) | always (no address filter) | `inject/` in-path drop shim |

Injection differs by medium. ZMQ has a broker you can make lossy in the middle; a real
CAN/KISS link does not, so loss must be injected **in-path** on a node's own transmit.
`inject/ci_drop_iface` is a `csp_iface_t` shim whose nexthop applies the same
deterministic, per-flow-keyed drop rule as the proxy, then delegates kept frames to the
real downstream interface (CAN/KISS/...). Because both injectors key on the same protocol
identity (RDP seq + wrap epoch / DTP fragment index, shared in `lib/ci_rule`), the proxy
drop-log and the shim drop-log are interchangeable oracle A.

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
  per-flow reproducibility key + tracker (RDP seq + wrap epoch / DTP fragment index),
  shared by both the proxy and the in-path shim.
- `ci_meas.{h,c}` - loss/dup/reorder sequence tracking, observed-at-tap RTT pairing,
  and the `MEASUREMENT_SUSPECT` flag (so instrument loss is not mistaken for link loss).

`inject/ci_drop_iface.{c,h}` is the in-path drop shim: a `csp_iface_t` for CAN/KISS
fault injection (where the ZMQ proxy cannot reach). See `docs/can-kiss-injection.md`.

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
not build on a macOS host. On a Linux host they build natively and run the full E2E
suite (9/9: lib + proxy + APM + RDP/DTP two-oracle + in-path drop shim):

```sh
git submodule update --init --recursive     # vendored CSH deps (csp/slash/param/apm_csh)
meson setup build -Dfrontends=true
meson compile -C build
meson test -C build --print-errorlogs
```

Native deps: `libzmq3-dev`, `libsocketcan-dev` (`libbsd-dev` on CI), `pkg-config`,
`python3`, meson, ninja, a C toolchain.

See **`docs/HOWTO.md`** for the three things you actually run (test suite, monitor a CAN
bus from csh, run the two-oracle bench) and what each one tells you.

## Next

The instrument is proven natively on synthetic RDP + DTP streams, and the two-oracle
bench passes on the real flatsat CAN bus (`scripts/can0-bench`). The remaining work is
the field result:

1. Monitor / inject during a **real DTP transfer** on the flatsat (a genuine
   `dtp_client` upload) so the numbers come from the real software, not a synthetic stream.
2. A two-ended timestamped PCAP capture of a real DISCO2 pass.

See `TODOS.md`.
