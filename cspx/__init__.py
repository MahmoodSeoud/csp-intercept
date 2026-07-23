"""cspx - CSP link-assurance tool.

The verdict engine (verdict.py) is a pure function; everything hardware-facing
is orchestration around it. See docs/cspx-assurance-tool-spec.md.
"""
from .verdict import Verdict, RunObservation, VerdictResult, judge  # noqa: F401
