"""Unit tests for within_pass_blackouts.py (eng-review F3: the within-pass
analysis code ships with coverage). Covers the pure functions: blackout
position, pass clustering (F5), thirds binning, and the build() integration over
a synthetic telemetry CSV."""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import within_pass_blackouts as W  # noqa: E402
from _regime_common import percentile  # noqa: E402


def test_percentile():
    assert percentile([], 50) is None
    assert percentile([5], 50) == 5.0
    assert percentile([1, 2, 3, 4], 50) == 2.5


def test_blackout_positions_basic():
    # 1s cadence (median delta 1, threshold max(5*1,2)=5), one 7s gap after t=3.
    eps = [0, 1, 2, 3, 10, 11, 12]
    bo = W.blackout_positions(eps)
    assert len(bo) == 1
    # onset = last contact before the gap (t=3), span 12 -> 0.25
    assert abs(bo[0]["onset_pos"] - 0.25) < 1e-9
    assert bo[0]["dur_s"] == 7.0


def test_blackout_positions_edges():
    assert W.blackout_positions([0, 1]) == []          # <3 samples
    assert W.blackout_positions([5, 5, 5]) == []       # span 0
    assert W.blackout_positions([0, 1, 2, 3, 4]) == []  # no gap over threshold


def test_blackout_onset_strictly_below_one():
    # onset is the sample BEFORE a gap, so it can never land at position 1.0.
    eps = [0, 1, 2, 3, 4, 100]  # gap at the very end
    bo = W.blackout_positions(eps)
    assert len(bo) == 1
    assert bo[0]["onset_pos"] < 1.0


def test_cluster_passes_merges_concurrent_logs():
    # Two logs starting 60s apart = one pass (the F5 double-count case); a third
    # starting 2000s later = a separate pass.
    sessions = [
        ("logA", [1000.0, 1001.0, 1002.0]),
        ("logB", [1060.0, 1061.0]),
        ("logC", [3100.0, 3101.0, 3102.0]),
    ]
    clusters = W.cluster_passes(sessions, gap_s=1200.0)
    assert len(clusters) == 2
    assert sorted(clusters[0]["files"]) == ["logA", "logB"]
    # merged + sorted union of both logs' epochs
    assert clusters[0]["epochs_s"] == [1000.0, 1001.0, 1002.0, 1060.0, 1061.0]
    assert clusters[1]["files"] == ["logC"]


def test_cluster_passes_chains_consecutive_starts():
    # Consecutive starts each within gap_s chain into ONE pass even if the first
    # and last are >gap_s apart (consecutive-gap rule, matches the dataset).
    sessions = [
        ("a", [0.0, 1.0, 2.0]),
        ("b", [1000.0, 1001.0]),
        ("c", [2000.0, 2001.0]),
    ]
    clusters = W.cluster_passes(sessions, gap_s=1200.0)
    assert len(clusters) == 1
    assert sorted(clusters[0]["files"]) == ["a", "b", "c"]


def test_third_boundaries():
    assert W.third(0.0) == "early"
    assert W.third(0.33) == "early"
    assert W.third(1 / 3) == "mid"     # boundary belongs to mid
    assert W.third(0.5) == "mid"
    assert W.third(2 / 3) == "late"    # boundary belongs to late
    assert W.third(0.99) == "late"


def _write_csv(path, rows):
    with open(path, "w") as f:
        f.write("source_file,epoch_ms,metric,node,labels,value\n")
        for src, ms, node in rows:
            f.write(f"{src},{ms},rx_count,{node},{{}},1\n")


def test_build_clusters_and_counts():
    # One pass split across two concurrent logs, with a single blackout; plus a
    # second pass far later. Build should report 2 clustered passes from 3 logs.
    rows = []
    # pass 1, log X: 1s cadence 0..4s then a 10s gap then resume (one blackout)
    base = 1_000_000
    for t in [0, 1000, 2000, 3000, 4000, 14000, 15000, 16000]:
        rows.append(("logX", base + t, "4032"))
    # pass 1, log Y: concurrent, starts 60s in
    for t in [60000, 61000, 62000]:
        rows.append(("logY", base + t, "4032"))
    # pass 2, far later (>20 min)
    base2 = base + 3_000_000
    for t in [0, 1000, 2000, 12000, 13000]:
        rows.append(("logZ", base2 + t, "4032"))
    # a different node, must be ignored
    rows.append(("logX", base + 500, "9999"))

    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as tf:
        path = tf.name
    try:
        _write_csv(path, rows)
        res = W.build(path, node="4032", gap_factor=5.0)
        assert res["n_sessions_raw"] == 3        # logX, logY, logZ (9999 ignored)
        assert res["n_passes_clustered"] == 2    # X+Y merged, Z separate
        assert res["n_blackouts_total"] >= 1
        s = res["onset_thirds"]
        assert s["early"] + s["mid"] + s["late"] == res["n_blackouts_total"]
    finally:
        os.unlink(path)
