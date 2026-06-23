#!/usr/bin/env python3
"""Headline thesis figure: completion vs injected loss, three arms.

Reads the two sweep CSVs produced on the live can0 flatsat and renders the
3-arm comparison (dipp / satDeploy-naive / satDeploy-smart) plus the cost of
completion for the smart arm. Pure on-disk data; no live bus needed.

  captures/dipp_sweep.csv      -> Arm A (fire-and-forget baseline)
  captures/satdeploy_sweep.csv -> Arm B (smart) + Arm C (naive)

Completion is a proportion (k of n seeds), so panel (a) shows Wilson 95%
confidence intervals as error bars; with small n the intervals are wide, which
is the honest way to present k/n (and visibly reflects the per-arm n: dipp and
smart n=5, naive n=3).

Output: figures/completion_vs_loss.{png,pdf}
"""
import csv
import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CAP = os.path.join(ROOT, "captures")
FIGDIR = os.path.join(ROOT, "figures")


def wilson(k, n, z=1.96):
    """Wilson score 95% CI for a binomial proportion. Returns (p, lo, hi)."""
    if n == 0:
        return 0.0, 0.0, 0.0
    p = k / n
    denom = 1.0 + z * z / n
    center = (p + z * z / (2 * n)) / denom
    half = (z * ((p * (1 - p) / n + z * z / (4 * n * n)) ** 0.5)) / denom
    return p, max(0.0, center - half), min(1.0, center + half)


def load_dipp():
    """dipp completion per loss = [k complete, n seeds]; complete = kept==sent."""
    byloss = defaultdict(lambda: [0, 0])
    with open(os.path.join(CAP, "dipp_sweep.csv")) as f:
        for r in csv.DictReader(f):
            if r["status"] != "ok":
                continue
            byloss[float(r["loss"])][1] += 1
            if int(r["kept"]) == int(r["sent"]):
                byloss[float(r["loss"])][0] += 1
    return byloss


def load_satdeploy():
    """smart/naive completion per loss = [k DEPLOYED, n]; also smart passes + overhead."""
    comp = {"smart": defaultdict(lambda: [0, 0]), "naive": defaultdict(lambda: [0, 0])}
    passes = defaultdict(list)
    overhead = defaultdict(list)
    with open(os.path.join(CAP, "satdeploy_sweep.csv")) as f:
        for r in csv.DictReader(f):
            if r["status"] != "ok":
                continue
            arm, loss = r["arm"], float(r["loss"])
            comp[arm][loss][1] += 1
            if r["result"] == "DEPLOYED":
                comp[arm][loss][0] += 1
            if arm == "smart":
                passes[loss].append(int(r["passes"]))
                overhead[loss].append(float(r["overhead_ratio"]))
    pass_avg = {l: sum(v) / len(v) for l, v in passes.items()}
    ovh_avg = {l: sum(v) / len(v) for l, v in overhead.items()}
    return comp, pass_avg, ovh_avg


def ci_series(byloss):
    """dict loss->[k,n]  ->  (xs%, ps, yerr_lo, yerr_hi) for ax.errorbar."""
    xs = sorted(byloss)
    ps, lo, hi = [], [], []
    for x in xs:
        k, n = byloss[x]
        p, l, h = wilson(k, n)
        ps.append(p); lo.append(p - l); hi.append(h - p)
    return [x * 100 for x in xs], ps, [lo, hi]


def main():
    os.makedirs(FIGDIR, exist_ok=True)
    dipp = load_dipp()
    sat, passes, overhead = load_satdeploy()

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))

    # Panel (a): completion probability vs loss, with Wilson 95% CIs
    x, y, e = ci_series(dipp)
    ax1.errorbar(x, y, yerr=e, fmt="o-", color="#c0392b", lw=2, ms=6, capsize=3,
                 label="dipp (fire-and-forget)")
    x, y, e = ci_series(sat["naive"])
    ax1.errorbar(x, y, yerr=e, fmt="s--", color="#e67e22", lw=2, ms=6, capsize=3,
                 label="satDeploy-naive (recovery off)")
    x, y, e = ci_series(sat["smart"])
    ax1.errorbar(x, y, yerr=e, fmt="^-", color="#27ae60", lw=2.5, ms=7, capsize=3,
                 label="satDeploy-smart (retry+resume)")
    ax1.set_xlabel("Injected per-fragment loss (%)")
    ax1.set_ylabel("Completion probability")
    ax1.set_ylim(-0.05, 1.12)
    ax1.set_title("(a) File completion vs loss")
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc="center right", fontsize=9)
    ax1.text(0.02, 0.02, "error bars: Wilson 95% CI  (n=5 dipp/smart, n=3 naive)",
             transform=ax1.transAxes, fontsize=7.5, color="#555", va="bottom")

    # Panel (b): the cost of completion (smart only)
    xs = sorted(passes)
    xp = [x * 100 for x in xs]
    ax2.plot(xp, [passes[x] for x in xs], "^-", color="#27ae60", lw=2.5, ms=7,
             label="passes to complete")
    ax2.set_xlabel("Injected per-fragment loss (%)")
    ax2.set_ylabel("Passes to DEPLOYED", color="#27ae60")
    ax2.tick_params(axis="y", labelcolor="#27ae60")
    ax2.set_ylim(0, max(passes.values()) + 0.6)
    ax2.set_title("(b) Cost of completion — satDeploy-smart")
    ax2.grid(True, alpha=0.3)

    ax2b = ax2.twinx()
    ax2b.plot(xp, [overhead[x] for x in xs], "d:", color="#2980b9", lw=2, ms=6,
              label="bytes-on-wire overhead")
    ax2b.set_ylabel("Overhead ratio (sent / 1 clean push)", color="#2980b9")
    ax2b.tick_params(axis="y", labelcolor="#2980b9")
    ax2b.set_ylim(0.95, max(overhead.values()) + 0.1)

    lines = ax2.get_lines() + ax2b.get_lines()
    ax2.legend(lines, [l.get_label() for l in lines], loc="upper left", fontsize=9)

    fig.suptitle("Loss-resilient upload on the DISCO2 flatsat (can0, MTU 256, 256 KiB payload)",
                 fontsize=11, y=1.02)
    fig.tight_layout()
    for ext in ("png", "pdf"):
        out = os.path.join(FIGDIR, f"completion_vs_loss.{ext}")
        fig.savefig(out, dpi=160, bbox_inches="tight")
        print("wrote", out)


if __name__ == "__main__":
    main()
