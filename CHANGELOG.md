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
  `satdeploy_sweep.csv`, `rq3_corruption.csv`, `rawdtp_sweep.csv`, and
  `h2_rawdtp_finding.md` now track. Reproducible scratch (oracle traces, can0
  snapshots, debug runs) stays out.
- **9.6 kbit/s uplink pacing** across the sweep drivers (`upload-point`,
  `dipp-sweep`, `satdeploy-sweep`): each forwarded CSP packet is delayed by its
  serialization time at `RATE_BPS` (default 9600), and all run timeouts derive
  from the computed transmit time. Forward-path only — no half-duplex turnaround
  model yet.
- **Raw-DTP drivers** (`scripts/rawdtp-point`, `scripts/rawdtp-sweep`) behind the
  H2 finding: a bare libdtp resume reports DELIVERED but writes a corrupt file
  under loss.
- **Calibration figure** (`scripts/plot_calibration.py`, `figures/calibration.*`)
  showing the injector drops what it claims (worst error 1.66 pp, under the 2 pp
  bound).
- **satguard** report layer over the capture CSVs (`satguard/`).
- **Test-rig diagram** (`diagrams/`, excalidraw + mermaid + png/svg).

### Changed
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
