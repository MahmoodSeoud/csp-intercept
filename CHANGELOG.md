# Changelog

All notable changes to csp-intercept are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version
is declared in `meson.build` (currently `0.1.0`).

This is a research instrument, so "notable" means anything that changes a
measured result, the way a result is produced, or the way a result is reported.

## [Unreleased]

### Added
- **Headline evidence under version control.** `captures/*` is gitignored, but
  the result tables and written findings are negated back in: `dipp_sweep.csv`,
  `satdeploy_sweep.csv`, `rq3_corruption.csv`, `rawdtp_sweep.csv`, `rdp_sweep.csv`,
  and `h2_rawdtp_finding.md` now track. Reproducible scratch (oracle traces, can0
  snapshots, debug runs) stays out.
- **RDP/CSH upload arm** (`scripts/rdp-point`, `scripts/rdp-sweep`): the RQ3
  integrity differential against the DTP arms. Drives the deployed CSH `upload`
  path (CSP port 14, RDP + per-packet CRC32) through the loss injector
  (`dport=14`, keyed on RDP sequence number), with a zeros pre-fill and dual
  verification (mechanism `crc32` + external `download`-back `sha256`). Result
  (`rdp_sweep.csv`, FlatSat, 2-30% forward loss): 0 silent-corruption cells — the
  path delivers verified-intact or fails loudly, never reports success over a
  corrupt binary, where the DTP data plane silently corrupts.
- **UHF uplink pacing** across the sweep drivers (`upload-point`, `dipp-sweep`,
  `satdeploy-sweep`, `rawdtp-point`, `rawdtp-sweep`): each forwarded CSP packet is
  delayed by its serialization time at `RATE_BPS` (default 4800), and all run
  timeouts derive from the computed transmit time. Forward-path only — no
  half-duplex turnaround model yet.
- **Raw-DTP drivers** (`scripts/rawdtp-point`, `scripts/rawdtp-sweep`) behind the
  H2 finding: a bare libdtp resume reports DELIVERED but writes a corrupt file
  under loss.
- **Calibration figure** (`scripts/plot_calibration.py`, `figures/calibration.*`)
  showing the injector drops what it claims (worst error 1.66 pp, under the 2 pp
  bound).
- **satguard** report layer over the capture CSVs (`satguard/`).
- **Test-rig diagram** (`diagrams/`, excalidraw + mermaid + png/svg).

### Changed
- **Link rate corrected to 4800 bit/s net.** `RATE_BPS` default flipped from 9600
  to 4800 in all five paced drivers, and `rate_bps` is stamped into the sweep CSV
  schema (old rows backfilled 9600) so 9600/4800 runs never mix silently. The
  DISCO-2 UHF radio runs at 4800 bps, confirmed by flight telemetry (rx_baud /
  tx_baud = 4800 bps; GMSK + Reed-Solomon(223,255) FEC); operators throttle DTP
  to ~1 KB/s in practice. The on-board CAN bus is *not* the bottleneck — it is
  deliberately bypassed for bulk data. Completion/corruption results are
  loss-driven and unchanged; only RQ4 cost-in-passes scales with rate (256 KiB
  ≈ 7.3 min line-rate at 4800).
- Headline plot (`scripts/plot_headline.py`) reads from `captures/` and renders
  Wilson 95% confidence intervals on the completion panel — the honest way to
  show k/n with small n (n=5 dipp/smart, n=3 naive).

### Documentation
- `README.md` rewritten: architecture, the three oracles, the findings table,
  how a human drives it, CSV schemas, and limits.
- `LICENSE` (MIT), `CONTRIBUTING.md`, `CHANGELOG.md` (this file).

## [0.1.0]

### Added
- Initial fault-injection instrument: in-path CSP loss injector
  (`ci_inject_bridge`) with per-attempt Gilbert-Elliott burst loss, the lossy
  zmqproxy front-end, and the CSP monitor APM (wire-monitor oracle).
- Overhead-aware DTP fragment index (reads satDeploy 8-byte frames).
- 3-arm loss-sweep drivers (dipp + satDeploy naive/smart) and the headline
  completion-vs-loss figure.
- Pure zero-dependency `lib/` + test suite; front-ends gated behind
  `-Dfrontends=true`.
