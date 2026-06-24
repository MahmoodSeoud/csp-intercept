#!/usr/bin/env python3
"""parse_flight_dtp.py - extract DTP download sessions from a real csh pass log.

One row per dtp_client (resume) session in a recorded ground-station console log:
the bytes the session aimed for, bytes received, missing-segment count, last seq,
the client's RETURNED STATUS, duration, throughput, whether it bailed on a no-data
timeout, and the ground's range-rate + Doppler (from the `drun` wrapper line that
precedes the session).

The thesis-relevant pair is (status, missing): the client returns status 0
(success) even when missing segments remain - the RQ3 silent-incompleteness
mechanism, observed on real flight hardware. This is the DOWNLINK direction
(dtp_client pulling from the satellite's dipp@5423), the worse of the two links.
"""
import csv
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]")


def parse(path):
    rows, cur, sec, rr, dop = [], None, None, None, None
    with open(path, errors="replace") as fh:
        for raw in fh:
            line = ANSI.sub("", raw).replace("\r", "").rstrip("\n")
            m = re.search(r"Range rate: ([-+0-9.]+) km/s, Doppler@100M: ([-+0-9.]+) Hz", line)
            if m:
                rr, dop = m.group(1), m.group(2)
                continue
            m = re.search(r"Setting session total bytes to (\d+)", line)
            if m:
                cur = {"total": int(m.group(1)), "missing": 0, "bailed": 0,
                       "range_rate_kms": rr, "doppler_hz": dop}
                sec = None
                continue
            if cur is None:
                continue
            if "No data received for" in line and "bailing out" in line:
                cur["bailed"] = 1
            if "Missing segments:" in line:
                sec = "missing"
                continue
            if "Received segments:" in line:
                sec = "recv"
                continue
            if sec == "missing" and line.strip().startswith("Segment #"):
                cur["missing"] += 1
                continue
            m = re.search(r"Received (\d+) bytes, last seq: (-?\d+), status: (-?\d+)", line)
            if m:
                cur.update(received=int(m.group(1)), last_seq=int(m.group(2)),
                           status=int(m.group(3)))
                sec = None
                continue
            m = re.search(r"Session duration: ([0-9.]+) s, avg throughput: (\d+) KB/sec", line)
            if m and "received" in cur:
                cur.update(dur_s=float(m.group(1)), kbps=int(m.group(2)))
                rows.append(cur)
                cur = None
    return rows


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: parse_flight_dtp.py LOG [out.csv]")
    rows = parse(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else "flight_dtp.csv"
    cols = ["idx", "total", "received", "missing", "last_seq", "status", "bailed",
            "dur_s", "kbps", "range_rate_kms", "doppler_hz"]
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for i, r in enumerate(rows, 1):
            w.writerow([i] + [r.get(c) for c in cols[1:]])

    n = len(rows)
    if not n:
        print(f"no DTP sessions found in {sys.argv[1]}")
        return
    status0 = sum(1 for r in rows if r.get("status") == 0)
    incomplete = sum(1 for r in rows if r.get("missing", 0) > 0)
    silent = sum(1 for r in rows if r.get("status") == 0 and r.get("missing", 0) > 0)
    bailed = sum(1 for r in rows if r.get("bailed"))
    print(f"{n} DTP sessions -> {out}")
    print(f"  status==0 (reported success): {status0}/{n}")
    print(f"  incomplete (missing>0):       {incomplete}/{n}")
    print(f"  bailed on no-data timeout:    {bailed}/{n}")
    print(f"  SILENT (status==0 AND missing>0): {silent}/{n}  <- RQ3 mechanism, flight hardware")
    kbps = [r["kbps"] for r in rows]
    print(f"  throughput: {min(kbps)}-{max(kbps)} KB/s; range-rate spans "
          f"{min(float(r['range_rate_kms']) for r in rows if r['range_rate_kms'])} .. "
          f"{max(float(r['range_rate_kms']) for r in rows if r['range_rate_kms'])} km/s")


if __name__ == "__main__":
    main()
