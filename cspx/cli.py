"""cspx CLI.

MVP subcommand: `explain` re-interprets a recorded capture with the verdict
engine (no hardware). `run` / `sweep` (which drive the flatsat) land next; they
wrap the proven scripts per the spec.
"""
from __future__ import annotations

import argparse
import json
import sys

from .adapters import observations_from_csv
from .verdict import Verdict, judge

_GLYPH = {Verdict.PASS: "PASS ", Verdict.FALSE_COMPLETE: "FALSE", Verdict.FAIL_LOUD: "LOUD "}


def _render(label: str, r) -> str:
    tag = _GLYPH[r.verdict]
    head = f"[{tag}] {r.verdict.value:14} {label}"
    return f"{head}\n         where: {r.where}\n         cause: {r.cause} -> fix: {r.fix}"


def cmd_explain(args: argparse.Namespace) -> int:
    rows = list(observations_from_csv(args.capture))
    worst = 0
    counts = {v: 0 for v in Verdict}
    out = []
    for obs in rows:
        r = judge(obs)
        counts[r.verdict] += 1
        worst = max(worst, r.exit_code)
        if args.json:
            d = r.to_dict()
            d["label"] = obs.label
            out.append(d)
        else:
            print(_render(obs.label or "(row)", r))
    if args.json:
        print(json.dumps(out, indent=2))
    else:
        print(f"\n{len(rows)} runs: "
              + ", ".join(f"{v.value}={counts[v]}" for v in Verdict if counts[v]))
    return worst


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="cspx", description="CSP link-assurance tool")
    sub = p.add_subparsers(dest="cmd", required=True)
    ex = sub.add_parser("explain", help="re-interpret a recorded capture (no hardware)")
    ex.add_argument("capture", help="path to a sweep capture CSV")
    ex.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    ex.set_defaults(func=cmd_explain)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
