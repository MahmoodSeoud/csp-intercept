#!/usr/bin/env python3
"""
real_regime_annotation.py - summarize the real-pass operating regime from the
telemetry CSV (output of parse_prometheus_telemetry.py) into regime.json.

This is a PLAUSIBILITY ANNOTATION for the loss sweep, NOT the loss model. It uses
throughput, uptime, and intermittency (blackout structure) - never rx_err-as-loss
(rx_err is a CRC-noise counter; see the pivot in
mseo-master-plan-measurement-20260607.md). The blackout structure also informs the
multi-pass resume schedules (TODOS#6): real sessions have link blackouts, which is
exactly when satDeploy's cross-pass resume earns its keep.

Per session (one source-file log, approximately one ops/pass session) on the radio
node (default 4032 = ground TTC radio):
  - active span and sample cadence
  - blackouts: gaps in the telemetry stream > gap_factor x median sample interval
  - uplink activity/throughput from tx_count / tx_bytes (cumulative; reset-safe)
  - downlink from rx_count / rx_bytes

Aggregate -> percentiles across sessions. NOTE: tx_* is the GROUND's uplink-send
volume and rate (the upload direction), it is not a delivery or loss measure.
"""
import argparse
import csv
import json
import os
import sys
from collections import defaultdict

# Shared "what is a blackout" definition + percentile (DRY with
# within_pass_blackouts.py, eng-review F1). scripts/ is sys.path[0] when run as a
# script; insert it explicitly so an `import real_regime_annotation` from another
# cwd still resolves the sibling module.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _regime_common import detect_blackouts, percentile  # noqa: E402


def positive_delta(values):
    """Sum of positive consecutive diffs - reset/wrap-safe total increase of a
    cumulative counter. [] or single -> 0."""
    total = 0.0
    for a, b in zip(values, values[1:]):
        if b > a:
            total += b - a
    return total


def _inc(series, metric):
    d = series.get(metric)
    if not d:
        return 0.0
    return positive_delta([d[e] for e in sorted(d)])


def session_profile(rows, node, gap_factor):
    """rows: telemetry rows for ONE source_file. Profile dict for `node`, or None."""
    series = defaultdict(dict)   # metric -> {epoch_ms: float}
    epochs = set()
    for r in rows:
        if r["node"] != node:
            continue
        try:
            v = float(r["value"])
            e = int(r["epoch_ms"])
        except (ValueError, KeyError):
            continue
        series[r["metric"]][e] = v
        epochs.add(e)
    if not epochs:
        return None

    ep_s = [e / 1000.0 for e in sorted(epochs)]
    span_s = ep_s[-1] - ep_s[0]
    med, gaps = detect_blackouts(ep_s, gap_factor)
    txc = _inc(series, "tx_count")
    rxc = _inc(series, "rx_count")

    def thru(metric):
        inc = _inc(series, metric)
        return round(inc / span_s, 1) if span_s > 0 and metric in series else None

    return {
        "n_samples": len(epochs),
        "span_s": round(span_s, 1),
        "median_interval_s": round(med, 2) if med is not None else None,
        "n_blackouts": len(gaps),
        "longest_blackout_s": round(max(gaps), 1) if gaps else 0.0,
        "total_blackout_s": round(sum(gaps), 1) if gaps else 0.0,
        "tx_count_inc": txc,
        "rx_count_inc": rxc,
        "active": (txc > 0 or rxc > 0),
        "tx_throughput_Bps": thru("tx_bytes"),
        "rx_throughput_Bps": thru("rx_bytes"),
    }


def aggregate(profiles, key):
    xs = [p[key] for p in profiles if p.get(key) is not None]
    return {"p10": percentile(xs, 10), "p50": percentile(xs, 50),
            "p90": percentile(xs, 90), "max": max(xs) if xs else None}


def build(csv_in, node, gap_factor):
    by_file = defaultdict(list)
    with open(csv_in) as fh:
        for r in csv.DictReader(fh):
            by_file[r["source_file"]].append(r)
    profiles = []
    for fname, rows in sorted(by_file.items()):
        p = session_profile(rows, node, gap_factor)
        if p:
            p["session"] = fname
            profiles.append(p)
    keys = ("tx_throughput_Bps", "rx_throughput_Bps", "n_blackouts",
            "longest_blackout_s", "span_s")
    active = [p for p in profiles if p.get("active")]
    return {
        "input": csv_in,
        "node": node,
        "gap_factor": gap_factor,
        "n_sessions": len(profiles),
        "n_active_sessions": len(active),
        "aggregate": {k: aggregate(profiles, k) for k in keys},
        "aggregate_active": {k: aggregate(active, k) for k in keys},
        "per_session": profiles,
        "note": ("Plausibility annotation (throughput/uptime/intermittency), NOT a "
                 "loss rate. tx_* is ground uplink-send volume/rate, not delivery. "
                 "See pivot in mseo-master-plan-measurement-20260607.md."),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description="Summarize real-pass regime -> regime.json")
    ap.add_argument("csv_in", help="telemetry.csv from parse_prometheus_telemetry.py")
    ap.add_argument("-o", "--out", default="regime.json")
    ap.add_argument("--node", default="4032", help="radio node (default 4032)")
    ap.add_argument("--gap-factor", type=float, default=5.0,
                    help="blackout = gap > this x median sample interval (default 5)")
    args = ap.parse_args(argv)
    out = build(args.csv_in, args.node, args.gap_factor)
    with open(args.out, "w") as f:
        json.dump(out, f, indent=2)
    print(f"{out['n_sessions']} sessions -> {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
