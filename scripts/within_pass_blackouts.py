#!/usr/bin/env python3
"""
within_pass_blackouts.py - WHERE in a pass does the link drop?

Answers the advisor's question (problem-statement review, RQ1 / Method): the
recorded passes show blackouts, but are they uniformly spread or do they cluster
mid-pass, where the satellite's relative motion is highest? This EXTENDS the
regime annotation (real_regime_annotation.py, which counts blackouts per session)
with their POSITION within the pass, normalized 0 (first sample ~ AOS) .. 1 (last
~ LOS) and histogrammed.

Two corrections from the eng review:

  * Pass clustering (F5). The telemetry has CONCURRENT logs of the same pass (two
    operator transcripts of one contact, e.g. both ending 13:13:55). Aggregating
    per source_file double-counts them. We cluster source files into passes by a
    >20-minute start-time gap - the SAME windowing the dataset uses to turn 361
    logs into ~239 windows - and merge each cluster's samples onto one timeline
    before detecting blackouts. Merging is conservative: a gap is a blackout only
    if NO concurrent log sampled during it.

  * The Doppler proxy was REMOVED (F2). rx_freq in this telemetry is not a usable
    Doppler curve (6-7 distinct values per session, GHz-scale jumps), so a
    max-rate position was meaningless. Doppler geometry is left to the live
    full-pass captures (experiment plan A2), where rx_freq units are known.

Honest about scope: after clustering, the node-4032 telemetry collapses to a
HANDFUL of real passes, so this is a SUGGESTIVE characterization, not a
statistical claim - consistent with the thesis's "thin where it matters" stance.
The session span is a proxy for the geometric AOS->LOS pass; the ground logs only
while it tracks the satellite, which may truncate the pass edges (a question for
the meeting, A5).

Input: captures/telemetry.csv (from parse_prometheus_telemetry.py).
"""
import argparse
import csv
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _regime_common import blackout_threshold, percentile  # noqa: E402

# >20-minute start-time gap = a new pass. Matches the dataset's window clustering.
PASS_GAP_S = 1200.0


def blackout_positions(epochs_s, gap_factor=5.0):
    """Sorted seconds for ONE pass -> list of {onset_pos, dur_s}, one per
    blackout. Onset = the sample just BEFORE the gap (last contact before the
    drop), as a 0..1 fraction of the pass span. Same threshold as
    real_regime_annotation via the shared blackout_threshold()."""
    if len(epochs_s) < 3:
        return []
    start, end = epochs_s[0], epochs_s[-1]
    span = end - start
    if span <= 0:
        return []
    med, thresh = blackout_threshold(epochs_s, gap_factor)
    if med is None:
        return []
    out = []
    for a, b in zip(epochs_s, epochs_s[1:]):
        if b - a > thresh:
            out.append({"onset_pos": (a - start) / span, "dur_s": round(b - a, 1)})
    return out


def cluster_passes(sessions, gap_s=PASS_GAP_S):
    """sessions: list of (source_file, sorted_epochs_s). Group files whose
    start-times fall within gap_s of the running cluster start into one pass, and
    merge their sample epochs (union). Returns list of {files, epochs_s}."""
    items = sorted(sessions, key=lambda s: s[1][0])  # by start time
    clusters = []
    last_start = None
    for fname, eps in items:
        # Split where the gap to the PREVIOUS log's start exceeds gap_s (the
        # dataset's "group start-times at a >20-min gap"); consecutive starts
        # within gap_s chain into one pass.
        if clusters and eps[0] - last_start <= gap_s:
            clusters[-1]["files"].append(fname)
            clusters[-1]["epochs"].update(eps)
        else:
            clusters.append({"files": [fname], "epochs": set(eps)})
        last_start = eps[0]
    return [{"files": c["files"], "epochs_s": sorted(c["epochs"])} for c in clusters]


def third(pos):
    return "early" if pos < 1 / 3 else ("mid" if pos < 2 / 3 else "late")


def build(csv_in, node, gap_factor):
    by_file = defaultdict(set)
    with open(csv_in) as fh:
        for r in csv.DictReader(fh):
            if r["node"] != node:
                continue
            try:
                by_file[r["source_file"]].add(int(r["epoch_ms"]) / 1000.0)
            except (ValueError, KeyError):
                continue

    sessions = [(f, sorted(eps)) for f, eps in by_file.items() if len(eps) >= 3]
    passes = cluster_passes(sessions)

    all_blackouts = []
    per_pass = []
    for p in passes:
        bo = blackout_positions(p["epochs_s"], gap_factor)
        if not bo:
            continue
        all_blackouts.extend(bo)
        per_pass.append({
            "files": p["files"],
            "n_files": len(p["files"]),
            "n_samples": len(p["epochs_s"]),
            "n_blackouts": len(bo),
            "onset_thirds": [third(b["onset_pos"]) for b in bo],
        })

    thirds = {"early": 0, "mid": 0, "late": 0}
    deciles = [0] * 10
    for b in all_blackouts:
        thirds[third(b["onset_pos"])] += 1
        deciles[min(9, int(b["onset_pos"] * 10))] += 1

    return {
        "input": csv_in,
        "node": node,
        "gap_factor": gap_factor,
        "pass_gap_s": PASS_GAP_S,
        "n_sessions_raw": len(sessions),
        "n_passes_clustered": len(passes),
        "n_passes_with_blackouts": len(per_pass),
        "n_blackouts_total": len(all_blackouts),
        "onset_thirds": thirds,
        "onset_deciles": deciles,
        "onset_pos_p50": round(percentile([b["onset_pos"] for b in all_blackouts], 50), 3)
        if all_blackouts else None,
        "per_pass": per_pass,
        "note": ("Blackout POSITION within a pass (0=AOS..1=LOS). Suggestive only: "
                 "after >20min clustering the node-4032 telemetry is a handful of "
                 "passes. Doppler proxy removed (rx_freq not a usable curve). Span "
                 "is a proxy for the geometric pass and may truncate the edges."),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description="Within-pass blackout position (clustered passes)")
    ap.add_argument("csv_in", help="telemetry.csv from parse_prometheus_telemetry.py")
    ap.add_argument("-o", "--out", default="within_pass.json")
    ap.add_argument("--node", default="4032", help="radio node (default 4032)")
    ap.add_argument("--gap-factor", type=float, default=5.0)
    args = ap.parse_args(argv)

    res = build(args.csv_in, args.node, args.gap_factor)
    with open(args.out, "w") as f:
        json.dump(res, f, indent=2)

    t = res["onset_thirds"]
    tot = res["n_blackouts_total"]
    print(f"within-pass blackouts (node {res['node']}): {tot} blackouts over "
          f"{res['n_passes_with_blackouts']} clustered passes "
          f"({res['n_sessions_raw']} raw logs)")
    if tot:
        for k in ("early", "mid", "late"):
            bar = "#" * int(round(40 * t[k] / tot))
            print(f"  {k:5s} {t[k]:3d} ({100*t[k]/tot:4.0f}%) {bar}")
        print(f"  onset position p50 = {res['onset_pos_p50']}  (0=AOS .. 1=LOS)")
    print(f"  -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
