#!/usr/bin/env python3
"""
parse_prometheus_telemetry.py - extract structured `prometheus add` telemetry from
DISCO ops session logs into a CSV.

Source lines look like this (CRLF-terminated, otherwise clean ASCII - no ANSI):

    prometheus add rx_count{node="4032"} 40 1780401727073
    prometheus add ch_current{node="5382", idx="0"} 5 1780401727073
                   ^metric   ^labels                ^value ^epoch_ms

We parse the structured token stream (metric / labels / value / epoch_ms) directly.
This is deliberately NOT the rendered csh param table (`120:4032 rx_freq = ... Hz`):
scraping that table is what produced field-misalignment artifacts (an rx_freq
magnitude landing in an rx_count column). Here metric and value are distinct
captured groups, so that whole artifact class cannot occur.

Output CSV columns: source_file,epoch_ms,metric,node,labels,value
  - value is kept as the RAW string (may be int, float, or negative) - no coercion
  - labels keeps the full `{...}` interior so no label is silently dropped

Default extracts a curated link / CSP-health metric set (DEFAULT_METRICS). Pass
--metrics a,b,c to override, or --all to extract every metric. Streams line by line
so the 558 MB log is never loaded into memory.

Role: feeds real_regime_annotation (a plausibility annotation of the loss regime
real passes occupy - throughput / uptime, NOT rx_err-as-loss). It is NOT the loss
model; the loss model is a parametric burst-loss sweep. See
~/.gstack/projects/MahmoodSeoud-csp-intercept/mseo-master-plan-measurement-20260607.md
and the pivot note in memory (thesis-spine-differential-testing).
"""
import argparse
import csv
import glob
import os
import re
import sys

# `prometheus add <metric>{<labels>} <value> <epoch_ms>`. Anchored on a >=10-digit
# epoch at end-of-line so the value token (which may itself be many digits, e.g.
# rx_freq) can never be mistaken for the timestamp.
LINE_RE = re.compile(
    r'prometheus add ([A-Za-z_][A-Za-z0-9_]*)\{([^}]*)\}\s+(\S+)\s+(\d{10,})\s*$'
)
NODE_RE = re.compile(r'node="?(\d+)"?')
ANSI_RE = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]')

# Loss / link / CSP-health metrics relevant to the loss-regime annotation. Verified
# present in the corpus (node 4032 radio counters + CSP buffer/conn counters).
DEFAULT_METRICS = {
    "rx_err", "rx_count", "ber", "rx_bytes", "tx_count", "tx_bytes",
    "rx_bytes_corrected", "rx_freq", "rx_rssi", "csp_buf_out", "csp_conn_out",
}


def _clean(line):
    """Strip any ANSI escapes + CR/LF. Defensive: the prometheus lines are clean
    in the samples seen, but other lines in these logs are ANSI-laden."""
    return ANSI_RE.sub("", line).replace("\r", "").rstrip("\n")


def parse_line(line):
    """Return a dict for a prometheus telemetry line, or None if it is not one."""
    m = LINE_RE.search(_clean(line))
    if not m:
        return None
    metric, labels, value, epoch = m.group(1), m.group(2), m.group(3), m.group(4)
    nm = NODE_RE.search(labels)
    return {
        "metric": metric,
        "node": nm.group(1) if nm else "",
        "labels": labels.strip(),
        "value": value,
        "epoch_ms": epoch,
    }


def iter_rows(path, metrics):
    """Stream one file, yielding row dicts for matching metrics.
    metrics=None keeps every metric. Never loads the whole file."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "prometheus add " not in line:   # cheap prefilter before regex
                continue
            row = parse_line(line)
            if row is None:
                continue
            if metrics is None or row["metric"] in metrics:
                yield row


def main(argv=None):
    ap = argparse.ArgumentParser(description="Extract prometheus telemetry -> CSV")
    ap.add_argument("logdir", help="directory of *.log files (e.g. ~/thesis/log)")
    ap.add_argument("-o", "--out", default="telemetry.csv")
    ap.add_argument("--metrics", help="comma-separated metric allowlist (overrides default)")
    ap.add_argument("--all", action="store_true", help="extract every metric")
    args = ap.parse_args(argv)

    if args.all:
        metrics = None
    elif args.metrics:
        metrics = {m.strip() for m in args.metrics.split(",") if m.strip()}
    else:
        metrics = set(DEFAULT_METRICS)

    files = sorted(glob.glob(os.path.join(os.path.expanduser(args.logdir), "*.log")))
    n_files = n_rows = 0
    with open(args.out, "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(["source_file", "epoch_ms", "metric", "node", "labels", "value"])
        for path in files:
            n_files += 1
            base = os.path.basename(path)
            for row in iter_rows(path, metrics):
                w.writerow([base, row["epoch_ms"], row["metric"], row["node"],
                            row["labels"], row["value"]])
                n_rows += 1
    print(f"parsed {n_files} files -> {n_rows} rows -> {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
