#!/usr/bin/env python3
"""Headline thesis figure: completion vs injected loss, three arms.

Reads the two sweep CSVs produced on the live can0 flatsat and renders the
3-arm comparison (dipp / satDeploy-naive / satDeploy-smart) plus the cost of
completion for the smart arm. Pure on-disk data; no live bus needed.

  scripts/dipp_sweep.csv      -> Arm A (fire-and-forget baseline)
  scripts/satdeploy_sweep.csv -> Arm B (smart) + Arm C (naive)

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


def load_dipp():
    """dipp completion = fraction of seeds delivering every fragment (kept==sent)."""
    byloss = defaultdict(list)
    with open(os.path.join(CAP, "dipp_sweep.csv")) as f:
        for r in csv.DictReader(f):
            if r["status"] != "ok":
                continue
            complete = int(r["kept"]) == int(r["sent"])
            byloss[float(r["loss"])].append(1.0 if complete else 0.0)
    return {l: sum(v) / len(v) for l, v in byloss.items()}


def load_satdeploy():
    """smart/naive completion = fraction DEPLOYED; also smart passes + overhead."""
    comp = {"smart": defaultdict(list), "naive": defaultdict(list)}
    passes = defaultdict(list)
    overhead = defaultdict(list)
    with open(os.path.join(CAP, "satdeploy_sweep.csv")) as f:
        for r in csv.DictReader(f):
            if r["status"] != "ok":
                continue
            arm, loss = r["arm"], float(r["loss"])
            comp[arm][loss].append(1.0 if r["result"] == "DEPLOYED" else 0.0)
            if arm == "smart":
                passes[loss].append(int(r["passes"]))
                overhead[loss].append(float(r["overhead_ratio"]))
    frac = {a: {l: sum(v) / len(v) for l, v in d.items()} for a, d in comp.items()}
    pass_avg = {l: sum(v) / len(v) for l, v in passes.items()}
    ovh_avg = {l: sum(v) / len(v) for l, v in overhead.items()}
    return frac, pass_avg, ovh_avg


def series(d):
    xs = sorted(d)
    return [x * 100 for x in xs], [d[x] for x in xs]


def main():
    os.makedirs(FIGDIR, exist_ok=True)
    dipp = load_dipp()
    sat, passes, overhead = load_satdeploy()

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))

    # Panel (a): completion probability vs loss
    x, y = series(dipp)
    ax1.plot(x, y, "o-", color="#c0392b", label="dipp (fire-and-forget)", lw=2, ms=6)
    x, y = series(sat["naive"])
    ax1.plot(x, y, "s--", color="#e67e22", label="satDeploy-naive (recovery off)", lw=2, ms=6)
    x, y = series(sat["smart"])
    ax1.plot(x, y, "^-", color="#27ae60", label="satDeploy-smart (retry+resume)", lw=2.5, ms=7)
    ax1.set_xlabel("Injected per-fragment loss (%)")
    ax1.set_ylabel("Completion probability")
    ax1.set_ylim(-0.05, 1.08)
    ax1.set_title("(a) File completion vs loss")
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc="center right", fontsize=9)

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
