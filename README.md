# csp-intercept

A measuring instrument for the DISCO2 radio link.

It does two things to CSP traffic (the protocol the satellite speaks):

1. **Watches** it — records every RDP/DTP packet that goes by, into a CSV.
2. **Breaks** it on purpose — drops a known, repeatable fraction of packets, so you
   can measure how RDP and DTP cope with a lossy link.

The point of the thesis: compare **RDP vs DTP** under controlled packet loss, and use
that to tune how the satellite uploader is configured.

**The one check that matters:** the part that drops packets and the part that watches
packets must agree — every packet is either dropped *or* seen, never both, never
neither. When they agree, the measurement is trustworthy. We call this the
*two-oracle agreement*, and the test suite + the CAN bench both prove it.

---

## 👉 Start here

**[docs/HOWTO.md](docs/HOWTO.md)** — the only page you need to *run* anything. It has
the three commands (run the tests / watch a CAN bus / run the loss bench) and explains
what each one shows you.

---

## Status — what works

| Part | State |
|------|-------|
| Watching (the monitor) | done — works over ZMQ, CAN, and KISS |
| Breaking (fault injection) | done — ZMQ proxy + in-path CAN/KISS shim |
| Two-oracle agreement | done — proven for RDP and DTP |
| Test suite | **9/9 green** |
| Real flatsat CAN bus | bench passes (`scripts/can0-bench`) |

**What's left** (see [TODOS.md](TODOS.md)): measure during a *real* DTP transfer on the
flatsat, and capture a real satellite pass. The instrument is done; the field
measurements aren't.

---

## The pieces

| Folder | What it is |
|--------|-----------|
| `lib/` | the brain — parses CSP/RDP/DTP, decides drops, computes loss. Pure C, unit-tested. |
| `apm/` | the **monitor** — a plugin you load into the satellite shell (csh) to watch a bus |
| `proxy/` | the **ZMQ loss injector** — a lossy broker for virtual/lab buses |
| `inject/` | the **CAN/KISS loss injector** — drops packets in-path on a real radio link |
| `tests/e2e/` | the 9-test suite (drivers + traffic generators) |
| `scripts/can0-bench` | run the whole loss measurement on the real flatsat CAN bus |

## Deeper detail (only if you need it)

- [docs/can-kiss-injection.md](docs/can-kiss-injection.md) — how CAN/KISS loss injection
  works, and which transports the monitor sees.
- [docs/dtp-metric.md](docs/dtp-metric.md) — why DTP loss is measured the way it is.

## Build (for developers)

```sh
git submodule update --init --recursive
meson setup build -Dfrontends=true
meson compile -C build
meson test -C build --print-errorlogs      # expect 9/9
```

Linux only. Native deps: `libzmq3-dev`, `libsocketcan-dev`, `pkg-config`, `python3`,
meson, ninja, a C toolchain.
