"""The verdict engine.

A pure function: given the normalized facts of one run (RunObservation), decide
the verdict, localize where it broke, and explain cause + fix. No hardware, no
side effects, so it is fully unit-testable and can re-interpret captures we
already recorded.

Classification (spec section 3):

    check passed ................................. PASS
    check failed AND procedure claimed success ... FALSE_COMPLETE   (the dangerous one)
    check failed AND procedure claimed failure ... FAIL_LOUD        (safe failure)
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any, Optional


class Verdict(str, Enum):
    PASS = "PASS"
    FAIL_LOUD = "FAIL_LOUD"
    FALSE_COMPLETE = "FALSE_COMPLETE"


# Process exit codes so an agent/CI branches on the verdict without parsing.
# 1 is reserved for a harness error (raised before a verdict exists).
EXIT_CODES = {
    Verdict.PASS: 0,
    Verdict.FALSE_COMPLETE: 2,
    Verdict.FAIL_LOUD: 3,
}


@dataclass
class RunObservation:
    """Normalized facts about a single run. Adapters build this from an
    arm-specific capture row or from a live run; the engine only ever sees
    this shape.

    The two required fields decide the verdict. The rest is evidence used to
    localize (WHERE) and explain (CAUSE/FIX); leave unknown fields as None.
    """

    claimed_success: bool          # did the procedure REPORT success
    check_passed: bool             # did the independent check pass

    transport: str = ""            # 'dtp-push' | 'rdp' | 'svu' | 'param' | ...
    has_integrity: Optional[bool] = None   # does the transport verify before completing
    resumed: bool = False
    readback_matches: Optional[bool] = None  # None = no readback was done
    injected: Optional[int] = None
    dropped: Optional[int] = None
    label: str = ""
    detail: dict = field(default_factory=dict)


@dataclass
class VerdictResult:
    verdict: Verdict
    where: str
    proof: str
    cause_id: str
    cause: str
    fix_id: str
    fix: str

    @property
    def exit_code(self) -> int:
        return EXIT_CODES[self.verdict]

    def to_dict(self) -> dict:
        d = asdict(self)
        d["verdict"] = self.verdict.value
        d["exit_code"] = self.exit_code
        return d

    def to_json(self, **kw: Any) -> str:
        return json.dumps(self.to_dict(), **kw)


_RULES_PATH = os.path.join(os.path.dirname(__file__), "rules.json")


def load_rules(path: str = _RULES_PATH) -> list[dict]:
    with open(path) as fh:
        return json.load(fh)["rules"]


def _rule_matches(when: dict, obs: RunObservation) -> bool:
    """A rule matches when every key in `when` equals the observation field.
    A `when` value may be a scalar or a list of accepted scalars."""
    for key, want in when.items():
        got = getattr(obs, key, None)
        if isinstance(want, list):
            if got not in want:
                return False
        elif got != want:
            return False
    return True


def classify(obs: RunObservation) -> Verdict:
    if obs.check_passed:
        return Verdict.PASS
    if obs.claimed_success:
        return Verdict.FALSE_COMPLETE
    return Verdict.FAIL_LOUD


def localize(obs: RunObservation, verdict: Verdict) -> str:
    """One honest sentence on WHERE it broke, from available evidence only."""
    if verdict is Verdict.PASS:
        return "check passed end to end"
    drops = ""
    if obs.dropped is not None and obs.injected:
        drops = f"; {obs.dropped} of {obs.injected} packets dropped"
    if obs.readback_matches is False:
        return f"the checked write did not land (readback shows the prior value){drops}"
    if obs.transport in ("dtp-push", "rdp", "svu", "rawdtp"):
        return f"the received file failed its content check{drops}"
    return f"the procedure's success check failed{drops}"


def _proof(obs: RunObservation, verdict: Verdict) -> str:
    if verdict is Verdict.PASS:
        return "independent check passed"
    if obs.readback_matches is False:
        return "readback of the target shows the prior value - the write did not land"
    return "the independent content check failed (hash mismatch)"


def explain(obs: RunObservation, verdict: Verdict, rules: list[dict]) -> tuple[str, str, str, str]:
    """Return (cause_id, cause, fix_id, fix). First matching rule wins."""
    if verdict is Verdict.PASS:
        return ("none", "link survived the degraded profile", "none", "no action")
    for rule in rules:
        if _rule_matches(rule.get("when", {}), obs):
            return (rule["id"], rule["cause"], rule["fix_id"], rule["fix"])
    # Never fabricate a cause: report unknown and hand back the raw evidence.
    return (
        "unknown",
        "check failed but no known failure-mode rule matched; see evidence",
        "investigate",
        "capture drop-log + dissector and add a rule for this mode",
    )


def judge(obs: RunObservation, rules: Optional[list[dict]] = None) -> VerdictResult:
    """The engine entrypoint: RunObservation -> VerdictResult."""
    if rules is None:
        rules = load_rules()
    verdict = classify(obs)
    cause_id, cause, fix_id, fix = explain(obs, verdict, rules)
    return VerdictResult(
        verdict=verdict,
        where=localize(obs, verdict),
        proof=_proof(obs, verdict),
        cause_id=cause_id,
        cause=cause,
        fix_id=fix_id,
        fix=fix,
    )
