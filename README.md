# csp-intercept

A CSP-aware monitor and fault-injection instrument for the DISCO2 link.
Measures and deliberately degrades Cubesat Space Protocol (CSP) traffic so the
RDP-vs-DTP behaviour over a slow/lossy UHF link can be characterised reproducibly.

Design doc: `~/.gstack/projects/csp-intercept/mahmood-unknown-design-20260529-073029.md`
Test plan: `~/.gstack/projects/csp-intercept/mahmood-unknown-eng-review-test-plan-20260529-073029.md`

## Status

| Component | State |
|-----------|-------|
| `lib/` shared parse + rule + PRNG | **done, 37 tests passing** |
| `apm/` promisc monitor (CSH APM) | not started (needs libcsp/CSH submodules) |
| `proxy/` lossy zmqproxy (vendored + extended) | not started |
| CI | not started |

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
  deterministically per packet index (or from a recorded replay vector).

## What this studies

RDP and DTP take fundamentally different reliability approaches over CSP. The thesis
characterises that contrast over a slow, lossy UHF link: which transport degrades
gracefully, and how the SatDeploy uploader should be tuned as a result. This repo is
the instrument that produces those measurements. Protocol details and analysis live
in the (local) design doc, not here.

## Build & test

```sh
meson setup build
meson test -C build
# or, dependency-free:
cc -std=c11 -Wall -Wextra -Werror -I lib lib/ci_rdp.c lib/ci_dtp.c lib/ci_rule.c \
   tests/test_lib.c -o /tmp/ci_test && /tmp/ci_test
```

## Next

Vendor `libcsp`/`slash`/`libapm_csh` + CSH's `src/zmqproxy.c` (`zmq_proxy_lossy`),
then build the monitor APM and the lossy proxy against `lib/`. See `TODOS.md` and
the design doc Next Steps.
