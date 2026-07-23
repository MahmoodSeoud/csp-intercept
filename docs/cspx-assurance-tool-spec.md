# cspx — CSP link-assurance tool (spec)

> SUPERSEDED (2026-07-23) by `docs/csp-debugger-design.md`. This spec framed the
> tool as a standalone verdict-emitter; office-hours reframed it as a gdb-style
> CSP debugger APM where the operator (not the tool) draws the conclusion. Kept
> for the reuse map and test strategy, which still apply. Read the new doc first.

Status: SUPERSEDED · Owner: Mahmood · Immediate target: Tuesday demo
Working name `cspx` is a placeholder (see Open Decisions).

## 1. Problem

We have excellent research *primitives* — a deterministic CSP loss injector, a
passive CSP sniffer, and integrity/liveness checks — but no *product*. Operating
them today requires tribal knowledge: 12-positional-arg binaries, hand-wired csh
sessions, and raw per-packet CSVs that only the author can read. That gap is why
"a good package" is not yet a tool someone else (a supervisor, a customer, or an
AI agent) can run and act on.

The failure we keep proving is always the same shape, across every transfer path:
under a lossy link, a procedure **reports success while the result is wrong or
absent**, with no error. A file arrives corrupt but "DELIVERED"; a resume
false-completes; a payload-access procedure never spawns its session because a
command packet was silently dropped. Nobody in CSP tooling turns that into an
answer. That answer is the product.

## 2. What cspx is

One command that takes a **procedure under test** + a **degraded link profile**,
runs it on the flatsat, and emits a **verdict**: did it work, where did it break,
why, and how to fix it — as human text and as JSON. Chaos tools (Toxiproxy,
netem) inject the fault and stop. cspx adds the assurance layer on top.

Two first-class operators, by design:
- a **human** (supervisor/customer/ops lead) reading a 6-line verdict, and
- an **AI agent** consuming `--json` and driving it in a loop.

## 3. The verdict (the whole point)

Every run classifies into exactly one of three outcomes:

| Verdict | Meaning | Why it matters |
|---|---|---|
| `PASS` | check passed | link survived |
| `FAIL_LOUD` | procedure reported failure AND check failed | safe failure — operator knows |
| `FALSE_COMPLETE` | procedure reported **success** but check failed | the dangerous one — silent corruption / phantom completion |

`FALSE_COMPLETE` is the crown jewel. It is the class of bug the flight team hit on
orbit and the thing no existing tool surfaces.

Rendered form (human):

```
SCENARIO  payload-access-under-loss   loss=5% burst=4 rate=4800bps seed=1
VERDICT   FALSE_COMPLETE — procedure reported done, session never came up
WHERE     op 2/4: long parameter write to node 5421 — packet dropped, never retried
PROOF     readback of the param shows the prior value — the write did not land
CAUSE     best-effort delivery; CAN reassembly is all-or-nothing; no retransmit
FIX       confirm-and-retry the procedure (~1.05 attempts @5%) | or reliable transport
ARTIFACTS capture.csv · report.html
```

JSON form (agent):

```json
{
  "scenario": "payload-access-under-loss",
  "link": { "loss": 0.05, "burst": 4, "rate_bps": 4800, "seed": 1 },
  "verdict": "FALSE_COMPLETE",
  "where":   { "op": "param-write", "index": "2/4", "node": 5421 },
  "proof":   { "type": "readback", "expected": "<intended>", "got": "<prior>" },
  "cause_id": "drop-no-retry",   "cause": "best-effort delivery; CFP all-or-nothing; no retransmit",
  "fix_id":   "confirm-retry",   "fix":   "confirm-and-retry the procedure",
  "counters": { "injected": 1500, "dropped": 147, "delivered": 1353 },
  "artifacts": { "capture": "runs/…/capture.csv", "report": "runs/…/report.html" }
}
```

Exit codes (for agents/CI): `0`=PASS, `2`=FALSE_COMPLETE, `3`=FAIL_LOUD,
`1`=harness error. Distinct so a caller branches without parsing.

## 4. Architecture

cspx is an **orchestrator + interpreter**. It does not reimplement the primitives;
it drives them and adds the verdict layer. Boring by default — a single Python
package, because the value-add is glue, CLI, JSON, and reporting, none of it
hot-path. The C binaries stay as-is.

```
                    cspx  (Python CLI: orchestrate + interpret)
                      │
   ┌──────────┬───────┴───────┬─────────────┬──────────┐
   │ run      │ sweep         │ explain     │ report   │   subcommands
   ▼          ▼               ▼             ▼          ▼
 one cell   run × (loss,seed) re-interpret  render HTML  (--json on all)
   │
   │  SCENARIO EXECUTOR (per cell)
   │  ┌───────────────────────────────────────────────┐
   ├─▶│ 1. health-gate target node + DIPP              │  safety
   ├─▶│ 2. bring up link degrader  (ci_inject_bridge)  │  ← existing
   ├─▶│ 3. start dissector         (csp_monitor APM)   │  ← existing
   ├─▶│ 4. run the procedure       (csh script / arm)  │  ← existing
   ├─▶│ 5. run success check       (hash | liveness)   │  ← existing
   ├─▶│ 6. tear down + RESET to benign                 │  safety (mandatory)
   └─▶│ 7. collect drop.csv + monitor.csv + check      │
      └───────────────────────┬───────────────────────┘
                              ▼
                     VERDICT ENGINE  (new — the value)
             correlate drop-log × dissector × check × readback
                              │
                     ┌────────┴────────┐
                     ▼                 ▼
              text verdict        JSON + report.html
```

### 4.1 Reuse map (Step 0)

| Need | Existing piece | New? |
|---|---|---|
| degrade the link | `tests/e2e/ci_inject_bridge` | reuse |
| observe the wire | `apm/csp_monitor` (`libcsh_csp_monitor.so`) | reuse |
| integrity check | csh `verify` (sha256/crc) | reuse |
| liveness check | csh `ping <node>` | reuse |
| decisive proof | csh `get <param> <node>` | reuse |
| drive the procedure | csh + a `.csh` per arm | reuse |
| **orchestrate + classify + render** | — | **build** |

New code is one Python package (~600–1000 lines). No new C, no new infra, one
innovation token spent (the verdict engine), everything else boring.

### 4.2 Scenario model (declarative, Toxiproxy-shaped)

A scenario is a small file — the tribal wiring turned into data:

```yaml
name: payload-access-under-loss
procedure: { kind: csh, script: <untracked path> }   # or a built-in arm: rdp|dtp-push|svu
target:    { node: 5421, port: <param port> }        # what the degrader drops
link:      { loss: 0.05, burst: 4, rate_bps: 4800, seed: 1 }   # the "toxics"
check:     { kind: liveness, node: 5425 }            # or { kind: integrity, sha256: <hex> }
readback:  { param: <name>, node: 5421 }             # optional decisive proof
reset:     always                                     # safety; runs even on failure
guard:     flatsat-only                               # refuse if target looks like flight
```

cspx compiles this into the `ci_inject_bridge` args + the csh command sequence.
The sensitive payload-access scenario file stays **untracked/gitignored**; cspx
core is generic and committed. Committed output stays transport-level.

**Executor decision (locked, eng-review D2 = A): wrap the proven scripts.** For
the MVP, cspx does NOT reimplement csh orchestration. It shells out to the
existing `*-point` / `*-sweep` drivers (which already own degrader bring-up,
4800bps pacing, and a `RESULT` line) plus **one** new payload-access driver
modeled on `scripts/rdp-repro-5431`, and parses their `RESULT` line into the
verdict + JSON. New Python is ~200 lines of front + interpret, not a hardware
driver. A native Python executor is a post-Tuesday refactor once the verdict
layer is proven, at which point the scripts become its backend and are retired
(one orchestration path, not two).

### 4.3 Verdict engine

Pure function: `(drop_log, monitor_log, check_result, readback, file_diff) → verdict`.
No hardware, no side effects → fully unit-testable.

- **Classify**: `check_passed` → PASS. else if procedure `claimed` success →
  `FALSE_COMPLETE`. else → `FAIL_LOUD`.
- **Localize (WHERE)**: for the MVP payload-access case, WHERE comes from the
  **readback + drop count** ("param shows the prior value → the write did not
  land; N packets dropped on the target port"), NOT packet-level dissector
  correlation — the monitor can't decode param traffic yet, so block-index
  localization is deferred to the full vision (dissector learns param/CFP). For
  file arms, WHERE is the byte range of the received-file diff.
- **Explain (CAUSE/FIX)**: a small rule table `pattern → cause_id → fix_id`, seeded
  with the known modes:

  | pattern | cause_id | fix_id |
  |---|---|---|
  | claimed done, hash mismatch, no integrity in transport | `no-integrity` | `integrity-gate` |
  | resume claimed done, high-index zero-holes | `resume-skips-gap` | `verified-resume` |
  | command dropped, not retransmitted, readback = prior | `drop-no-retry` | `confirm-retry` |

  Extensible — new modes append rows. Unknown pattern → `cause_id: unknown` with the
  raw evidence attached (never a fabricated cause). The table lives in a data file
  (`rules.yaml`), not inline code, so adding a failure mode is data + a test, not a
  code change (eng-review Q2).

## 5. Safety (non-negotiable — the procedure can be root injection)

- `reset: always` — restore the benign value after **every** run, including failures.
- **Crash-safe reset (eng-review A3):** reset must survive cspx itself dying mid-run.
  The executor wraps every run in try/finally + a SIGINT/SIGTERM handler that always
  issues the reset before exit, and ships a standalone idempotent `cspx reset
  <scenario>` for manual recovery. Leaving an injected root param on the box is the
  worst-case blast radius; this closes it.
- Health-gate the target node + DIPP before each cell; skip (don't pile on) if wedged.
- `guard: flatsat-only` — an allowlist of test bus/node identities; refuse otherwise.
- Deterministic `seed`; all-or-nothing reassembly means loss causes *omission*, not a
  mangled root command (documented assumption, re-verified at p=0).
- Never commit/describe the payload-access mechanism; scenario file untracked.

## 6. Operator journeys

Human:
```
cspx run scenarios/payload-access.yaml --loss 0.05
# → the 6-line verdict, exit 2
```

Agent (loop until it breaks, then report):
```
for L in 0 0.02 0.05 0.10; do cspx run <scn> --loss $L --json; done
# parse verdict/where/cause/fix; stop at first FALSE_COMPLETE; write the report
```

Sweep + report:
```
cspx sweep <scn> --loss 0,0.02,0.05,0.10,0.20 --seeds 3 --report out.html
```

Re-interpret an old capture (no hardware):
```
cspx explain captures/rdp_sweep.csv       # runs the verdict engine over recorded data
```

## 7. Tuesday MVP (the slice we build first)

In scope for Tuesday:
1. `cspx run <scenario>` for the **payload-access** scenario — steps 1–7 of the
   executor, with mandatory reset + health-gate.
2. The **verdict engine**: 3-way classification + the 3 seed rules + WHERE from the
   drop-log.
3. **Text verdict + `--json`** + exit codes.
4. One validated **p=0 dry-run** proving the harness and the liveness check.

Deferred (full vision, not Tuesday): `sweep` polish, `explain` subcommand, HTML
report auto-gen, multi-protocol dissection in the monitor (param/CFP), a scenario
library, the frame-level degrader (fidelity upgrade).

`explain` is cheap and high-value because it runs against captures you already
have — strong candidate to pull forward if p=0 lands early.

## 8. Test strategy

- **Verdict engine**: unit tests with fixture `(drop, monitor, check)` triples →
  assert verdict + cause_id + fix_id. Cover all 3 outcomes and every rule row.
- **Golden-file**: feed **real recorded CSVs** (`captures/rdp_sweep.csv`,
  `svu_sweep.csv`, DTP-push runs) → assert the engine classifies them the way we
  know they should (DTP-push rows with `claimed=DELIVERED, sha=MISMATCH` →
  `FALSE_COMPLETE`). Validates the interpreter against ground truth for free.
- **Scenario parsing**: unit tests incl. malformed/missing fields.
- **Orchestration**: a `--dry-run` mode (print the plan, touch no hardware) unit-
  tested; one integration test at loss=0 on the flatsat.
- **Safety**: test that `reset` runs on the failure path (inject a check failure,
  assert the reset command was issued).

## 9. Performance / operator ergonomics (eng-review P1)

Not a Tuesday blocker, designed in now: at 4800bps a cell is minutes and a sweep is
~30 of them, so cspx **streams each cell's verdict as it lands** (never buffers the
whole sweep) and supports `--resume` to skip completed cells. `monitor.csv` is
stream-parsed, not loaded whole.

## 10. Open decisions

Resolved in eng-review:
- **D-C / D-D (executor)** → RESOLVED: wrap the proven scripts for the MVP; cspx owns
  invoking them but not the csh orchestration itself (see §4.2).

Still open (not Tuesday-blocking):
- **D-A** Name: `cspx` vs `csp-assure` vs `linkcheck` vs other.
- **D-B** Scenario format: YAML (readable, needs a dep) vs JSON (stdlib) vs TOML.
- **D-E** Run artifacts location `runs/<id>/…`; use an explicit run-id or monotonic
  counter (no wall-clock in the determinism path).
```

## GSTACK REVIEW REPORT

| Area | Finding | Resolution |
|---|---|---|
| Architecture | A1 executor strategy under-committed | RESOLVED: wrap proven scripts (D2=A), §4.2 |
| Architecture | A2 WHERE over-promised for payload case | RESOLVED: readback-based WHERE for MVP, §4.3 |
| Architecture | A3 reset not crash-safe | RESOLVED: try/finally + signal handler + `cspx reset`, §5 |
| Design | Q1 two orchestration paths risk | Accepted: cspx is single front, scripts retire post-MVP |
| Design | Q2 cause→fix rules inline | RESOLVED: `rules.yaml` data file, §4.3 |
| Tests | T1 golden-file needs committed fixtures | Open: vendor 2-3 trimmed real captures |
| Tests | T2 reset-on-failure untested | RESOLVED: explicit test, §8 |
| Perf | P1 sweep is minutes/cell | RESOLVED: per-cell streaming + `--resume`, §9 |

Status: REVIEWED. Complexity gate: not triggered (one Python package, ~200 LOC MVP,
no new services, one innovation token).

VERDICT: APPROVED to build the Tuesday MVP (§7). Executor = wrap scripts. Build order:
(1) verdict engine + golden tests against real captures, (2) `cspx run` wrapping one
payload-access driver, (3) `--json` + exit codes, (4) p=0 dry-run on flatsat.

NO UNRESOLVED DECISIONS
