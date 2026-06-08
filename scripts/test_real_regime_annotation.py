#!/usr/bin/env python3
"""
Tests for real_regime_annotation. Runs under pytest OR standalone:
    python3 scripts/test_real_regime_annotation.py
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import real_regime_annotation as R


def test_positive_delta_monotonic():
    assert R.positive_delta([1, 3, 7]) == 6


def test_positive_delta_reset_safe():
    # counter resets 8 -> 2; only positive increments count: (3) + (2) = 5
    assert R.positive_delta([5, 8, 2, 4]) == 5


def test_positive_delta_degenerate():
    assert R.positive_delta([5]) == 0
    assert R.positive_delta([]) == 0


def test_percentile_basic():
    assert R.percentile([10, 20, 30], 50) == 20
    assert R.percentile([10, 20, 30], 10) == 12
    assert R.percentile([10, 20, 30], 90) == 28
    assert R.percentile([5], 50) == 5
    assert R.percentile([], 50) is None


def test_detect_blackouts_regular_cadence():
    med, gaps = R.detect_blackouts([0, 2, 4, 6, 8], gap_factor=5)
    assert med == 2 and gaps == []


def test_detect_blackouts_finds_gap():
    # 2s cadence then an 88s gap -> threshold max(5*2,2)=10 -> the 88s gap flagged
    med, gaps = R.detect_blackouts([0, 2, 4, 92, 94], gap_factor=5)
    assert med == 2 and gaps == [88]


def test_detect_blackouts_too_few():
    assert R.detect_blackouts([0, 2], gap_factor=5) == (None, [])


def _rows(*triples):
    # (metric, value, epoch_ms) on node 4032 unless metric starts with '!'
    out = []
    for metric, value, epoch in triples:
        node = "5382" if metric.startswith("!") else "4032"
        out.append({"metric": metric.lstrip("!"), "value": str(value),
                    "node": node, "epoch_ms": str(epoch)})
    return out


def test_session_profile_basic():
    # tx_bytes cumulative 0->1000 over 0..10s, regular 2s cadence, then no gap
    rows = _rows(
        ("tx_bytes", 0, 1000), ("tx_bytes", 250, 3000), ("tx_bytes", 500, 5000),
        ("tx_bytes", 1000, 11000),
        ("!ch_current", 5, 1000),   # other node -> ignored
        ("rx_bytes", "bad", 3000),  # non-numeric -> skipped
    )
    p = R.session_profile(rows, node="4032", gap_factor=5)
    assert p["n_samples"] == 4          # 4 distinct epochs on node 4032
    assert p["span_s"] == 10.0
    assert p["tx_throughput_Bps"] == 100.0   # 1000 B over 10 s
    assert p["n_blackouts"] == 0


def test_session_profile_blackout_and_reset():
    rows = _rows(
        ("tx_bytes", 0, 1000), ("tx_bytes", 100, 3000),
        ("tx_bytes", 50, 95000),   # link came back after an 92s gap; counter reset
        ("tx_bytes", 150, 97000),
    )
    p = R.session_profile(rows, node="4032", gap_factor=5)
    assert p["n_blackouts"] == 1
    assert p["longest_blackout_s"] == 92.0
    # positive deltas: 100 (0->100) + 100 (50->150) = 200 over 96s span
    assert p["tx_throughput_Bps"] == round(200 / 96.0, 1)


def test_session_profile_no_node_rows():
    assert R.session_profile(_rows(("!ch_current", 5, 1000)), "4032", 5) is None


def test_build_end_to_end():
    csv_text = (
        "source_file,epoch_ms,metric,node,labels,value\n"
        's1.log,1000,tx_bytes,4032,"node=""4032""",0\n'
        's1.log,3000,tx_bytes,4032,"node=""4032""",300\n'
        's1.log,5000,tx_bytes,4032,"node=""4032""",600\n'
        's2.log,1000,tx_bytes,4032,"node=""4032""",0\n'
        's2.log,2000,tx_bytes,4032,"node=""4032""",100\n'
        's2.log,3000,tx_bytes,4032,"node=""4032""",200\n'
    )
    f = tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False)
    f.write(csv_text)
    f.close()
    try:
        out = R.build(f.name, node="4032", gap_factor=5)
        assert out["n_sessions"] == 2
        assert out["aggregate"]["tx_throughput_Bps"]["p50"] is not None
    finally:
        os.unlink(f.name)


def test_build_empty_csv_no_crash():
    f = tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False)
    f.write("source_file,epoch_ms,metric,node,labels,value\n")
    f.close()
    try:
        out = R.build(f.name, node="4032", gap_factor=5)
        assert out["n_sessions"] == 0
        assert out["aggregate"]["tx_throughput_Bps"]["max"] is None
    finally:
        os.unlink(f.name)


def test_build_active_classification():
    csv_text = (
        "source_file,epoch_ms,metric,node,labels,value\n"
        # active: tx_count advances
        'a.log,1000,tx_count,4032,"node=""4032""",0\n'
        'a.log,3000,tx_count,4032,"node=""4032""",10\n'
        'a.log,5000,tx_count,4032,"node=""4032""",20\n'
        # idle: tx_count flat at 0
        'b.log,1000,tx_count,4032,"node=""4032""",0\n'
        'b.log,3000,tx_count,4032,"node=""4032""",0\n'
    )
    f = tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False)
    f.write(csv_text)
    f.close()
    try:
        out = R.build(f.name, node="4032", gap_factor=5)
        assert out["n_sessions"] == 2
        assert out["n_active_sessions"] == 1
    finally:
        os.unlink(f.name)


if __name__ == "__main__":
    fns = [v for k, v in sorted(globals().items())
           if k.startswith("test_") and callable(v)]
    failed = 0
    for fn in fns:
        try:
            fn()
            print(f"PASS {fn.__name__}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {e}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"ERROR {fn.__name__}: {e!r}")
    print(f"\n{len(fns) - failed}/{len(fns)} passed")
    sys.exit(1 if failed else 0)
