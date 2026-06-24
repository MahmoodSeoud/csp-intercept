"""_regime_common.py - shared definitions for the recorded-pass regime analysis.

Single source of truth for "what is a blackout" and the percentile helper, so
real_regime_annotation.py (blackout COUNT per session) and
within_pass_blackouts.py (blackout POSITION within a pass) can never drift on the
definition. Pulled out per the eng-review DRY finding (F1): before this, both
scripts carried their own copy of the median-gap threshold, and a tweak in one
would have silently desynced the two RQ1 numbers.

A blackout is a gap between consecutive telemetry samples that exceeds
max(gap_factor * median_interval, min_floor_s). The median makes the threshold
adapt to each session's own sample cadence; the floor stops a fast-cadence
session from flagging trivial sub-second gaps.
"""


def percentile(xs, p):
    """Linear-interpolated percentile (p in 0..100). [] -> None."""
    if not xs:
        return None
    s = sorted(xs)
    if len(s) == 1:
        return float(s[0])
    k = (len(s) - 1) * (p / 100.0)
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return float(s[lo] + (s[hi] - s[lo]) * (k - lo))


def blackout_threshold(epochs_s, gap_factor=5.0, min_floor_s=2.0):
    """Sorted seconds -> (median_interval_s, threshold_s). The threshold is the
    gap size above which a sample-to-sample interval counts as a blackout.
    Returns (None, None) when there are too few samples to define a cadence."""
    if len(epochs_s) < 3:
        return (None, None)
    deltas = [b - a for a, b in zip(epochs_s, epochs_s[1:]) if b > a]
    if not deltas:
        return (None, None)
    med = percentile(deltas, 50)
    return (med, max(gap_factor * med, min_floor_s))


def detect_blackouts(epochs_s, gap_factor=5.0, min_floor_s=2.0):
    """Sorted seconds -> (median_interval_s, [gap_s, ...]) for gaps over the
    threshold. <3 samples -> (None, []). This is the COUNT/duration view used by
    real_regime_annotation.py; within_pass_blackouts.py uses the same threshold
    via blackout_threshold() but keeps each gap's POSITION."""
    med, thresh = blackout_threshold(epochs_s, gap_factor, min_floor_s)
    if med is None:
        return (None, [])
    deltas = [b - a for a, b in zip(epochs_s, epochs_s[1:]) if b > a]
    return (med, [d for d in deltas if d > thresh])
