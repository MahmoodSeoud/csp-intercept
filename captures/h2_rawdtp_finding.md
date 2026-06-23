# H2 / raw-DTP arm: DTP cross-session resume FALSE-COMPLETES under loss

Status: ISOLATED + CHARACTERIZED as a genuine, deployment-faithful bug (2026-06-21). Reproducible,
deterministic, independent of the injector.

## Sweep result (captures/rawdtp_sweep.csv, 10/10 cells, paced 9.6 kbit/s)
| loss | n | DTP result | sha256 truth | verdict |
|------|---|-----------|--------------|---------|
| 0    | 2 | DELIVERED | MATCH        | correct (control) |
| 0.05 | 2 | DELIVERED | MISMATCH     | false-complete |
| 0.10 | 2 | DELIVERED | MISMATCH     | false-complete |
| 0.20 | 2 | DELIVERED | MISMATCH     | false-complete |
| 0.30 | 2 | DELIVERED | MISMATCH     | false-complete |

**8/8 lossy transfers: raw DTP resume reports DELIVERED but delivers a corrupt file. 2/2 zero-loss:
correct.** Overhead 1.05x->1.29x (1 resume pass) as loss rises — note it "completes" in just ~2
passes BECAUSE it abandons the high-index gaps rather than recovering them. The CSV schema extends
satdeploy_sweep.csv with a sha256_verdict column: the finding IS the result-vs-sha256 divergence
(satDeploy has no such divergence — it verifies sha256 internally and retries until correct).

## Claim
libdtp/dipp-apm cross-session resume (`dtp_client -r`, the same `dtp_client_main` + `session_hooks`
+ `dtp_session_meta.bin` round-trip the repo's resume uses) does NOT reliably complete a transfer
after loss. It reports `Missing: 0` / `Received 262144 bytes, status 0` (DELIVERED) while silently
leaving high-index fragments undelivered as zero-holes. Right size on disk, wrong bytes. Caught only
by an external sha256 oracle.

## Evidence (loss=0.10, seed=1)
Driver: `scripts/rawdtp-point`. Receiver host-side on can0, paced 9.6 kbit/s. Loss on DTP data
plane (port 8) only.

- Pass 1 (loss=0.10): injected 1041 frags, dropped 93 -> `Missing: 23436 bytes` (= 93 x 252). Correct.
- Pass 2 (resume, **loss=0, dropped 0**): re-requested frags 0..788 only (110 frags), all arrived,
  then reported `Missing: 0` / `Received 262144 bytes, last seq: 786, status: 0` = DELIVERED.
- Received file: 262144 bytes (right size), sha256 != original. **5028 bytes differ, ALL zero-holes
  (0 garbage), in 21 fragment slots: 787,788,888-892,915-922,938-942,1040** — every hole is ABOVE
  the seq-786 truncation point. The resume never re-requested them.

The resume pass dropped NOTHING (loss=0), so the injector is exonerated: a loss-free resume pass
should fill every hole; it didn't. The fault is in DTP's resume logic.

## Mechanism (root-cause clue, not yet pinned to a line)
Pass-2 resume handshake logs `dtp_protocol.c:37: Setting session total bytes to <garbage>` —
**3505817776** here, **2191071592** in an earlier run. Differs run-to-run = an UNINITIALIZED field
read on the resume path. The garbage total truncates the session's notion of its own length to
~seq 786, so DTP re-requests only the 0..788 prefix and declares success, abandoning all missing
fragments above the truncation. `session_hooks.c` on_serialize/on_deserialize field ORDER matches
(checked), so the corruption is in the resume handshake / session reconstruction, not a trivial
meta field-order mismatch.

## Thesis implication
- **H2 ("satDeploy does not materially improve completion over raw DTP") is REFUTED** — and in the
  strongest way: raw DTP's *reported* completion is false. satDeploy's sha256-verified retry is
  exactly what is required to get *real* completion. The checksum is not redundant.
- **Generalizes RQ3**: silent corruption is not unique to DIPP's fire-and-forget (no recovery).
  DTP's *resume* (the obvious fix) ALSO silently corrupts. Both are caught only by the external
  oracle = the instrument's reason to exist.
- Candidate thesis contribution #3 (a real DTP resume bug), if root-caused to the line.

## Reproduce
`KEEP_WORK=1 RATE_BPS=9600 MAX_PASSES=3 RESUME_LOSS=0 scripts/rawdtp-point 0.10 1 iso_L0.10_s1`
Evidence: `captures/iso_L0.10_s1_received.bin` (corrupt), `captures/h2_iso_L0.10_s1_resume_pass2.log`.
Caveat: NOT root-caused to a source line; the deployed DIPP receiver runs fire-and-forget (resume
OFF), so this is a bug in the resume CAPABILITY (what you'd enable to fix fire-and-forget), exercised
through its real API host-side.
