"""Adapters: turn an arm-specific capture row into a normalized RunObservation.

Each sweep arm names its columns differently (rdp_sweep uses claimed/sha, rq3
uses client_reported_success/matches_original, rawdtp uses result/sha256_verdict).
This layer maps them all onto the one shape the verdict engine understands, so the
engine never learns about capture schemas. Add a schema here, not in the engine.
"""
from __future__ import annotations

import csv
from typing import Iterator

from .verdict import RunObservation

# Tokens a "did the procedure report success" column can carry.
_CLAIM_SUCCESS = {"delivered", "verified", "complete", "completed", "success", "ok", "yes"}
_CLAIM_FAIL = {"failed", "fail", "timeout", "aborted", "abort", "error", "no"}

# Transports that complete without their own content check (so a mismatch is
# silently accepted). Keyed by the `arm`/`mode` column when present.
_NO_INTEGRITY = {"dtp-push", "rawdtp", "dtp", "csp", "csp_sw"}


def _truthy_success(token: str) -> bool:
    t = token.strip().lower()
    if t in _CLAIM_SUCCESS:
        return True
    if t in _CLAIM_FAIL:
        return False
    raise ValueError(f"unrecognized success token: {token!r}")


def _first(row: dict, *names: str) -> str | None:
    for n in names:
        if n in row and row[n] != "":
            return row[n]
    return None


def _int_or_none(v: str | None) -> int | None:
    try:
        return int(v) if v not in (None, "") else None
    except (TypeError, ValueError):
        return None


def normalize_row(row: dict) -> RunObservation:
    """Map one capture row (as a dict) onto a RunObservation by sniffing the
    columns our real captures actually use."""
    # claimed_success: prefer an explicit reported-success column, else the
    # transfer-result column (claimed / result).
    claim_tok = _first(row, "client_reported_success", "claimed", "result", "status")
    if claim_tok is None:
        raise ValueError(f"no success column found in row: {sorted(row)}")
    claimed = _truthy_success(claim_tok)

    # check_passed: prefer an explicit content-match column, else a hash verdict.
    check_tok = _first(row, "matches_original", "sha256_verdict", "sha", "sha256")
    if check_tok is None:
        raise ValueError(f"no check column found in row: {sorted(row)}")
    t = check_tok.strip().lower()
    check_passed = t in ("match", "yes", "ok", "pass")

    # Transport comes from an explicit `arm` column, else is inferred from which
    # columns the schema carries. `mode` (stock/tuned) is a config qualifier, not
    # the transport, so it is NOT used here.
    arm = (_first(row, "arm") or "").strip().lower()
    label = (_first(row, "label") or "")
    if not arm:
        if "client_reported_success" in row or label.startswith("csp_sw"):
            arm = "dtp-push"
        elif "crc" in row:      # CRC-carrying arms are RDP
            arm = "rdp"
        elif "rounds" in row:   # round-counting arm is the SVU
            arm = "svu"
    transport = arm or "unknown"
    has_integrity = None
    if transport in _NO_INTEGRITY:
        transport = "dtp-push"
        has_integrity = False   # completes without a content check
    elif transport in ("rdp", "svu"):
        has_integrity = True    # verifies before reporting complete

    passes = _int_or_none(_first(row, "passes", "rounds"))
    resumed = passes is not None and passes > 1

    return RunObservation(
        claimed_success=claimed,
        check_passed=check_passed,
        transport=transport,
        has_integrity=has_integrity,
        resumed=resumed,
        injected=_int_or_none(_first(row, "injected", "total_injected")),
        dropped=_int_or_none(_first(row, "dropped", "total_dropped")),
        label=label,
    )


def observations_from_csv(path: str) -> Iterator[RunObservation]:
    """Stream RunObservations from a capture CSV (skips comment rows)."""
    with open(path, newline="") as fh:
        reader = csv.DictReader(r for r in fh if not r.startswith("#"))
        for row in reader:
            yield normalize_row(row)
