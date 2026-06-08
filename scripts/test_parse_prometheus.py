#!/usr/bin/env python3
"""
Tests for parse_prometheus_telemetry. Runs under pytest OR standalone:
    python3 scripts/test_parse_prometheus.py
    python3 -m pytest scripts/test_parse_prometheus.py
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_prometheus_telemetry as P


def test_wellformed_line():
    r = P.parse_line('prometheus add rx_count{node="4032"} 40 1780401727073\r\n')
    assert r == {"metric": "rx_count", "node": "4032", "labels": 'node="4032"',
                 "value": "40", "epoch_ms": "1780401727073"}


def test_extra_labels_idx():
    r = P.parse_line('prometheus add ch_current{node="5382", idx="0"} 5 1780401727073\r\n')
    assert r["metric"] == "ch_current"
    assert r["node"] == "5382"
    assert 'idx="0"' in r["labels"]
    assert r["value"] == "5"


def test_trailing_cr_handled():
    r = P.parse_line('prometheus add ber{node="4032"} 0.000000 1780401727073\r')
    assert r["value"] == "0.000000"
    assert r["epoch_ms"] == "1780401727073"


def test_leading_ansi_stripped():
    r = P.parse_line('\x1b[33mprometheus add rx_err{node="4032"} 195 1780401727074\x1b[0m\r\n')
    assert r is not None and r["metric"] == "rx_err" and r["value"] == "195"


def test_negative_value_preserved():
    r = P.parse_line('prometheus add rx_bytes_corrected{node="4032"} -4 1780401727073')
    assert r["value"] == "-4"


def test_big_value_not_confused_with_epoch():
    # rx_freq magnitude (9 digits) must stay the value; the 13-digit epoch stays epoch.
    r = P.parse_line('prometheus add rx_freq{node="4032"} 437075461 1780401727073')
    assert r["value"] == "437075461"
    assert r["epoch_ms"] == "1780401727073"


def test_non_prometheus_skipped():
    assert P.parse_line("csh # info") is None
    # the rendered csh param table is NOT a prometheus line -> must be skipped
    assert P.parse_line("120:4032  rx_freq              = 437075461 Hz") is None


def test_malformed_missing_epoch_skipped():
    assert P.parse_line('prometheus add rx_count{node="4032"} 40') is None


def test_no_node_label():
    r = P.parse_line('prometheus add stdbuf_out{} 3 1780401727073')
    assert r is not None and r["node"] == ""


def _write_log(content):
    f = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    f.write(content)
    f.close()
    return f.name


def test_iter_rows_filters_metrics():
    path = _write_log(
        'prometheus add rx_count{node="4032"} 40 1780401727073\r\n'
        'prometheus add ch_current{node="5382", idx="0"} 5 1780401727073\r\n'
        'some noisy non-prometheus line\r\n'
        'prometheus add rx_err{node="4032"} 195 1780401727074\r\n'
    )
    try:
        only = list(P.iter_rows(path, {"rx_count"}))
        assert len(only) == 1 and only[0]["metric"] == "rx_count"
        every = list(P.iter_rows(path, None))
        assert len(every) == 3   # the 3 prometheus lines, not the noise line
    finally:
        os.unlink(path)


def test_empty_file_no_crash():
    path = _write_log("")
    try:
        assert list(P.iter_rows(path, None)) == []
    finally:
        os.unlink(path)


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
