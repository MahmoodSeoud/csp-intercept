# csp-intercept

**A fault-injection instrument for satellite file upload.** It sits in the path of a real
CSP file transfer, drops a known, repeatable fraction of packets, paces the link to the real
radio rate, and then checks whether the file actually arrived intact — independently of what
the software claims. It exists to answer one question that mission software hides:

> When the uplink is lossy, does your upload path deliver the file correctly, or does it
> report success while silently delivering a corrupt file?

CSP = CubeSat Space Protocol (`libcsp`). DTP = a bulk-transfer library on top of CSP
(`libdtp`). CSH = the CSP shell used to drive transfers. The lab CAN bus stands in for the
9.6 kbit/s UHF radio.

---

## 👉 Start here

**[docs/HOWTO.md](docs/HOWTO.md)** — the shortest path to *running* something: the unit tests,
a live bus watch, and the loss bench. Read this first if you just want to drive it.

---

## What it found

Run on the real DISCO-2 flatsat, the instrument produced these (data in `captures/`,
figures in `figures/`, report layer in the sibling `../satguard/`):

| Result | Evidence | Number |
|--------|----------|--------|
| **Calibration** — the injector drops exactly what it claims | `figures/calibration.*` | worst error 1.66 pp (under the 2 pp bound) |
| **H1** — fire-and-forget upload collapses under loss | `captures/dipp_sweep.csv` | completion 100% at 0% loss, **0% at any loss ≥2%** |
| **RQ3 (headline)** — flight software silently accepts corruption | `captures/rq3_corruption.csv` | **25/25** lossy uploads delivered a corrupt file reported as success; **0/5** at zero loss |
| **RQ4** — the cost of a real fix (sha256 verify-retry) | `captures/satdeploy_sweep.csv` | completes 0–30% loss at **≤1.7×** bytes |
| **H2** — libdtp resume also false-completes | `captures/rawdtp_sweep.csv` | **8/8** lossy transfers reported DELIVERED but failed checksum |

The throughline: mission software's "success" signal is unreliable under loss; only an
end-to-end checksum tells the truth, and this instrument is what measures it.

---

## How it works — the three oracles

Every measurement is cross-checked by three independent views, so a result is trustworthy
rather than anecdotal:

- **Oracle A (drop log)** — the injector records every fragment it dropped.
- **Oracle B (wire monitor)** — a promiscuous monitor records every fragment that actually
  crossed the bus.
- **Oracle C (sha256)** — the delivered file is hashed against the original. This is the only
  authority on *delivery*; A and B validate the *injection*.

The load-bearing invariant: every fragment is either dropped **or** observed, never both
(`max(dropped ∩ observed) = 0`). The test suite and the CAN bench both prove it.

---

## The pieces

| Folder / file | What it is |
|---------------|-----------|
| `lib/` | the brain — parses CSP/RDP/DTP, decides drops, computes loss, Gilbert-Elliott burst model. Pure C, unit-tested. |
| `apm/` | the **monitor** — a plugin loaded into the CSP shell (csh) to watch a bus (oracle B). |
| `proxy/` | the **ZMQ loss injector** — a lossy broker for virtual/lab buses. |
| `inject/` | the **CAN/KISS loss injector** — drops packets in-path on a real radio link. |
| `tests/e2e/ci_inject_bridge.c` | the **bridge injector** — ingress (zmq/can) → drop shim → egress, with the drop log. |
| `scripts/` | the run drivers (one transfer, and full sweeps). See the table below. |
| `captures/` | result CSVs and finding notes. |
| `figures/` | regenerable figures (`scripts/plot_*.py`). |
| `satguard/` | the report layer: turns the capture CSVs into a customer-ready audit report. |

---

## How a human uses it

### 0. Build (once)

```sh
git submodule update --init --recursive
meson setup build -Dfrontends=true
meson compile -C build
meson test -C build --print-errorlogs        # expect 11/11 green
```

Linux only. Native deps: `libzmq3-dev`, `libsocketcan-dev`, `pkg-config`, `python3`, meson,
ninja, a C toolchain.

### 1. Run one instrumented transfer

You normally do **not** call the injector by hand — the drivers wire it up. But this is the
raw interface so you know what is happening underneath:

```
ci_inject_bridge <in> <out> <dport> <mtu> <overhead> <loss> <burst> <seed> <drop.csv|-> [src_addr] [pace_us]
```

| arg | meaning |
|-----|---------|
| `in` / `out` | ingress / egress, e.g. `zmq:tcp://127.0.0.1:6000,tcp://127.0.0.1:7000,5426` and `can:can0` |
| `dport` | CSP dest port to act on (8 = DTP data plane) |
| `mtu` / `overhead` | fragment size and protocol header size, so the drop is fragment-accurate |
| `loss` | drop probability, 0.0–1.0 |
| `burst` | Gilbert-Elliott mean burst length (1 = independent loss) |
| `seed` | makes the drop set **deterministic and replayable** across runs/arms |
| `drop.csv` | oracle A output |

### 2. Sweep across loss levels

The drivers run a full grid (loss × seed), one real upload per cell, and append one row per
run to a CSV. They are **resumable** — an already-recorded `(loss, seed)` is skipped.

```sh
# Deployed fire-and-forget upload (Arm A):
LOSSES="0 0.02 0.05 0.10 0.20 0.30" SEEDS="1 2 3 4 5" scripts/dipp-sweep

# satDeploy verify-retry vs naive control (Arm B), agent running on the board as 5427:
scripts/satdeploy-sweep smart

# Raw DTP resume (H2):
scripts/rawdtp-sweep
```

Each paced pass is ~4 min (9.6 kbit/s, 1041 fragments); a full sweep is 1–3 h. See the
operational notes in [docs/HOWTO.md](docs/HOWTO.md) and `HANDOFF.md` before running live.

### 3. Read the result

The CSV columns (real schemas):

```
dipp_sweep.csv      arm,loss,seed,label,sent,kept,dropped,realized,observedB,drop_obs,kept_obs,deliv_frac,status
satdeploy_sweep.csv arm,loss,seed,label,passes,total_injected,total_dropped,overhead_ratio,result,status
rawdtp_sweep.csv    arm,loss,seed,label,passes,total_injected,total_dropped,overhead_ratio,result,sha256_verdict,status
rq3_corruption.csv  loss,seed,label,bytes,delivered_sha256,matches_original,client_reported_success,corrupt_but_accepted
```

Regenerate the figures with no bench needed:

```sh
python3 scripts/plot_headline.py        # completion-vs-loss
python3 scripts/plot_calibration.py     # injector calibration
```

### 4. Turn it into an audit report

```sh
python3 satguard/satguard.py audit --captures captures/ --target "Your mission"
```

That emits a per-mechanism VULNERABLE / VERIFIED verdict and an HTML report.

---

## Scripts reference

| Script | What it does |
|--------|--------------|
| `scripts/can0-bench` | one-command two-oracle bench on the real flatsat CAN bus |
| `scripts/upload-point` | one fully-instrumented loss point of the deployed-upload arm |
| `scripts/dipp-sweep` | Arm A (deployed fire-and-forget upload) loss sweep |
| `scripts/satdeploy-sweep` | Arm B (satDeploy smart/naive) loss sweep; agent on board as 5427 |
| `scripts/rawdtp-point` / `rawdtp-sweep` | H2 raw-DTP-resume arm (receiver host-side on can0) |
| `scripts/sweep` | parametric burst-loss curve over the two-oracle bench |
| `scripts/restart-upload-client` | respawn the deployed upload_client (it exits after each transfer) |
| `scripts/deploy-agent` | push the patched ARM agent to the payload board at loss=0 |

---

## What you gain from using it

- **A go/no-go on your own stack:** does it silently accept a corrupt file under realistic loss?
- **A trustworthy number, not a guess:** calibrated to under 2 pp, three oracles agree.
- **A fair A/B between mechanisms:** the same seed replays a byte-identical drop pattern across
  two uploaders, so comparisons are clean.
- **The cost of a fix:** does adding integrity checking help, and what does it cost in passes/bytes?
- **Evidence for a decision:** which uploader to fly, whether to add a checksum, how big a pass budget.

---

## Honest limits

- It is a **bench instrument**: it needs the upload stack reachable on a CAN bus (or loopback),
  a sender, and the broker running. It is not yet a one-click tool an operator runs in the cloud.
- Forward-path loss only; adversarial return-path loss and half-duplex RTT cost are future work.
- The raw-DTP arm receiver runs host-side (x86), not on the ARM payload — disclosed where it matters.
- Single mission, single bus, one payload size (256 KiB) and MTU (256): external validity is limited.

See `HANDOFF.md` for the full operational reality and `TODOS.md` for open work.

## Deeper detail

- [docs/HOWTO.md](docs/HOWTO.md) — run the tests / watch a bus / run the bench.
- [docs/can-kiss-injection.md](docs/can-kiss-injection.md) — how CAN/KISS loss injection works.
- [docs/dtp-metric.md](docs/dtp-metric.md) — why DTP loss is measured the way it is.

## Build status

Test suite: **11/11 green** (`meson test -C build`). Instrument validated on the real flatsat;
the empirical findings above are measured, not simulated.
