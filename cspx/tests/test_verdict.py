"""Verdict engine tests: unit coverage of every outcome + rule, and golden-file
tests that re-interpret REAL recorded captures and assert the class we know each
one is. The golden tests validate the interpreter against ground truth for free.
"""
import json
import os

import pytest

from cspx.adapters import normalize_row, observations_from_csv
from cspx.verdict import (
    RunObservation,
    Verdict,
    classify,
    judge,
    load_rules,
)

FIX = os.path.join(os.path.dirname(__file__), "fixtures")


# --- classification: the three outcomes --------------------------------------

def test_classify_pass():
    assert classify(RunObservation(claimed_success=True, check_passed=True)) is Verdict.PASS


def test_classify_false_complete():
    # reported success, but the check failed: the dangerous one.
    obs = RunObservation(claimed_success=True, check_passed=False)
    assert classify(obs) is Verdict.FALSE_COMPLETE


def test_classify_fail_loud():
    obs = RunObservation(claimed_success=False, check_passed=False)
    assert classify(obs) is Verdict.FAIL_LOUD


def test_pass_wins_even_if_claim_false():
    # a passing check is a pass regardless of what the procedure reported.
    obs = RunObservation(claimed_success=False, check_passed=True)
    assert classify(obs) is Verdict.PASS


# --- cause/fix rules ---------------------------------------------------------

def test_rule_no_integrity():
    obs = RunObservation(claimed_success=True, check_passed=False,
                         transport="dtp-push", has_integrity=False)
    r = judge(obs)
    assert r.verdict is Verdict.FALSE_COMPLETE
    assert r.cause_id == "no-integrity"
    assert r.fix_id == "integrity-gate"


def test_rule_drop_no_retry():
    obs = RunObservation(claimed_success=True, check_passed=False,
                         transport="param", readback_matches=False)
    r = judge(obs)
    assert r.verdict is Verdict.FALSE_COMPLETE
    assert r.cause_id == "drop-no-retry"
    assert r.fix_id == "confirm-retry"
    assert "readback" in r.proof.lower()


def test_rule_resume_skips_gap():
    obs = RunObservation(claimed_success=True, check_passed=False,
                         transport="dtp-resume", resumed=True)
    r = judge(obs)
    assert r.cause_id == "resume-skips-gap"
    assert r.fix_id == "verified-resume"


def test_unknown_cause_never_fabricated():
    obs = RunObservation(claimed_success=True, check_passed=False, transport="mystery")
    r = judge(obs)
    assert r.cause_id == "unknown"
    assert r.fix_id == "investigate"


def test_pass_has_no_cause():
    r = judge(RunObservation(claimed_success=True, check_passed=True))
    assert r.cause_id == "none"
    assert r.fix_id == "none"


# --- exit codes + serialization ---------------------------------------------

def test_exit_codes_distinct():
    codes = {
        judge(RunObservation(claimed_success=True, check_passed=True)).exit_code,
        judge(RunObservation(claimed_success=True, check_passed=False)).exit_code,
        judge(RunObservation(claimed_success=False, check_passed=False)).exit_code,
    }
    assert codes == {0, 2, 3}


def test_json_is_agent_readable():
    r = judge(RunObservation(claimed_success=True, check_passed=False,
                             transport="dtp-push", has_integrity=False))
    d = json.loads(r.to_json())
    assert d["verdict"] == "FALSE_COMPLETE"
    assert d["exit_code"] == 2
    assert d["cause_id"] == "no-integrity"
    assert set(d) >= {"verdict", "where", "proof", "cause_id", "fix_id", "exit_code"}


# --- golden-file: re-interpret real captures ---------------------------------

@pytest.mark.parametrize("fixture,expected", [
    ("rdp_pass.csv", Verdict.PASS),
    ("rq3_false_complete.csv", Verdict.FALSE_COMPLETE),
    ("rawdtp_false_complete.csv", Verdict.FALSE_COMPLETE),
    ("rdp_fail_loud.csv", Verdict.FAIL_LOUD),
])
def test_golden_captures(fixture, expected):
    path = os.path.join(FIX, fixture)
    obs = list(observations_from_csv(path))
    assert obs, f"{fixture} produced no rows"
    verdicts = [judge(o).verdict for o in obs]
    assert all(v is expected for v in verdicts), \
        f"{fixture}: expected all {expected.value}, got {[v.value for v in verdicts]}"


def test_rq3_row_is_false_complete_with_right_cause():
    # the headline: deployed DTP push reported success on a corrupt file.
    row = {
        "loss": "0.02", "seed": "1", "label": "csp_sw_L0.02_s1", "bytes": "262144",
        "delivered_sha256": "169fd3...", "matches_original": "no",
        "client_reported_success": "yes", "corrupt_but_accepted": "yes",
    }
    r = judge(normalize_row(row))
    assert r.verdict is Verdict.FALSE_COMPLETE
    assert r.cause_id == "no-integrity"


def test_rules_file_loads():
    rules = load_rules()
    ids = {r["id"] for r in rules}
    assert {"no-integrity", "resume-skips-gap", "drop-no-retry"} <= ids
